#include <ui/file/FileFlow.h>

#include <ui/backup/Backup.h>
#include <ui/bank/BankBundle.h>
#include <ui/settings/Config.h>
#include <ui/widgets/Dialogs.h>
#include <ui/file/DocumentMgr.h>
#include <ui/file/DocumentState.h>
#include <ui/export/Export.h>
#include <ui/file/File.h>
#include <ui/file/FileWindow.h>
#include <ui/file/GroupFileLocate.h>
#include <ui/file/NewFileDialog.h>
#include <ui/resource/ResourceViews.h>
#include <ui/Messages.h>
#include <ui/file/OpenQueue.h>
#include <ui/player/PlayerViews.h>
#include <ui/app/PopupMgr.h>
#include <ui/items/Selection.h>
#include <ui/app/Shortcuts.h>
#include <ui/settings/UiColors.h>
#include <ui/file/UnsavedChanges.h>
#include <ui/wave/WaveImportPanel.h>
#include <ui/widgets/Widgets.h>

#include <libs/IconsLucide.h>

#include <algorithm>
#include <vector>

static bool sWantsOpenDialog = false;
static bool sWantsExit = false;
static bool sWantsExitWithoutSaving = false;
static bool sWantsSaveDirect = false;
static bool (*sPendingFileAction)() = nullptr;
static bool sSaveInUseRemember = false;
static bool sSaveInUseRetryFailed = false;

struct CloseRequest
{
    Document* document = nullptr;
    bool reopens = false;
};

static std::vector<CloseRequest> sCloseQueue;
static Document* sClosingDocument = nullptr;
static bool sClosingDocumentReopens = false;

static std::vector<Document*> sSaveQueue;
static Document* sSavingDocument = nullptr;
static Document* sSaveAsRequest = nullptr;
static bool sExitAfterSaving = false;

void RequestSaveFile()
{
    sWantsSaveDirect = true;
}

void RequestOpenFile()
{
    sWantsOpenDialog = true;
}

void RequestOpenFile(const char* path, openqueue::Origin origin)
{
    openqueue::Push(path, openqueue::Mode::Pin, origin);
}

void RequestPreviewFile(const char* path)
{
    openqueue::Push(path, openqueue::Mode::Preview, openqueue::Origin::Browser);
}

void RequestCloseFile()
{
    RequestCloseDocument(ActiveDocument());
}

static void QueueDocumentClose(Document* document, bool reopens)
{
    if (!document)
        return;

    const auto isSameDocument = [document](const CloseRequest& request) { return request.document == document; };

    if (std::none_of(sCloseQueue.begin(), sCloseQueue.end(), isSameDocument))
        sCloseQueue.push_back({ document, reopens });
}

void RequestCloseDocument(Document* document)
{
    QueueDocumentClose(document, false);
}

void RequestReloadDocument(Document* document)
{
    QueueDocumentClose(document, true);
}

void RequestSaveDocument(Document* document)
{
    if (document->state().unsaved && std::find(sSaveQueue.begin(), sSaveQueue.end(), document) == sSaveQueue.end())
        sSaveQueue.push_back(document);
}

void RequestSaveDocumentAs(Document* document)
{
    sSaveAsRequest = document;
}

void RequestSaveAll()
{
    sSaveQueue.clear();

    for (Document* document : GetOpenDocuments())
        RequestSaveDocument(document);
}

bool IsSaveQueueBusy()
{
    return !sSaveQueue.empty() || sSavingDocument || sWantsSaveDirect;
}

void RequestExitAfterSaveAll()
{
    RequestSaveAll();
    sExitAfterSaving = true;
}

void RequestExitWithoutSaving()
{
    sWantsExitWithoutSaving = true;
}

bool TryExit()
{
    sWantsExit = true;
    return false;
}

void ForgetDocumentRequests(const Document* document)
{
    const auto isDocument = [document](const CloseRequest& request) { return request.document == document; };
    sCloseQueue.erase(std::remove_if(sCloseQueue.begin(), sCloseQueue.end(), isDocument), sCloseQueue.end());

    ForgetNewFileDocument(document);

    if (sSaveAsRequest == document)
        sSaveAsRequest = nullptr;

    if (sClosingDocument == document)
        sClosingDocument = nullptr;

    if (sSavingDocument == document)
        sSavingDocument = nullptr;

    sSaveQueue.erase(std::remove(sSaveQueue.begin(), sSaveQueue.end(), document), sSaveQueue.end());
}

static bool IsOpenDocument(const Document* document)
{
    const std::vector<Document*>& documents = GetOpenDocuments();

    return document && std::find(documents.begin(), documents.end(), document) != documents.end();
}

static bool (*sFileAction)() = nullptr;

static bool ShouldWarnAboutMissingMetadata()
{
    const Bfsar* archive = FindActiveSoundArchive();

    return archive && !archive->getSaveMetadata() && archive->hasMetadataNames();
}

static std::vector<WaveFile*> sPendingUnusedWaves;

void CancelPendingExportsAndImports()
{
    CancelSoundExport();
    CancelFileExports();
    CancelBankBundleTransfers();
    CancelAudioResourceRequests();
    ResetWaveImport();

    sPendingUnusedWaves.clear();
}

static void ProceedDirectSave()
{
    if (ShouldWarnAboutMissingMetadata())
    {
        sPendingFileAction = nullptr;
        ImGui::OpenPopup("###MetadataWarning");
    }
    else
        SaveFile();
}

void HandleFileShortcuts()
{
    if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
        return;

    if (shortcuts::Pressed(shortcuts::Action::SaveProject) && IsDocumentOpen())
        sWantsSaveDirect = true;

    if (shortcuts::Pressed(shortcuts::Action::SaveAllProjects) && HasUnsavedDocuments())
        RequestSaveAll();

    if (shortcuts::Pressed(shortcuts::Action::OpenProject))
        sWantsOpenDialog = true;

    if (shortcuts::Pressed(shortcuts::Action::NewProject))
        OpenNewFileDialog();

    if (shortcuts::Pressed(shortcuts::Action::SaveProjectAs) && IsDocumentOpen())
        RequestSaveDocumentAs(ActiveDocument());

    if (IsDocumentOpen() && !IsFileWindowFocused() && shortcuts::Pressed(shortcuts::Action::CloseProject))
        RequestCloseFile();
}

static bool TakeRequest(bool* request)
{
    const bool requested = *request;
    *request = false;

    return requested;
}

static bool FinishDocumentClose()
{
    Document* document = sClosingDocument;
    sClosingDocument = nullptr;

    if (!IsOpenDocument(document))
        return false;

    if (sClosingDocumentReopens)
        openqueue::Push(document->filePath().cstr(), openqueue::Mode::Reload);
    else
        CloseDocument(document);

    return true;
}

static void CancelDocumentClose()
{
    sClosingDocument = nullptr;
    sPendingFileAction = nullptr;
    sCloseQueue.clear();
}

static void StartDocumentClose()
{
    while (!sCloseQueue.empty())
    {
        const CloseRequest request = sCloseQueue.front();
        sCloseQueue.erase(sCloseQueue.begin());

        if (!IsOpenDocument(request.document))
            continue;

        sClosingDocument = request.document;
        sClosingDocumentReopens = request.reopens;

        if (!request.document->state().unsaved)
        {
            FinishDocumentClose();
            continue;
        }

        ActivateDocument(request.document);

        sFileAction = &FinishDocumentClose;
        ImGui::OpenPopup("###Save");
        return;
    }
}

static void StartSaveAs()
{
    Document* document = sSaveAsRequest;

    if (!document || IsDocumentSwitchLocked())
        return;

    if (document != ActiveDocument())
    {
        if (!IsOpenDocument(document) || !ActivateDocument(document))
            sSaveAsRequest = nullptr;

        return;
    }

    sSaveAsRequest = nullptr;
    SaveFileAs(document);
}

static Document* FindOnlyUnsavedPlayableDocument()
{
    Document* unsaved = nullptr;

    for (Document* document : GetOpenDocuments())
    {
        if (!document->state().unsaved)
            continue;

        if (unsaved)
            return nullptr;

        unsaved = document;
    }

    return unsaved && unsaved->playableFile().isValid() ? unsaved : nullptr;
}

static void AskSaveBeforeExit(Document* document)
{
    ActivateDocument(document);

    sFileAction = &Exit;
    ImGui::OpenPopup("###Save");
}

static void StartFileAction()
{
    if (IsDocumentSwitchLocked())
        return;

    if (TakeRequest(&sWantsOpenDialog))
    {
        OpenFile();
        return;
    }

    if (TakeRequest(&sWantsExitWithoutSaving))
    {
        Exit();
        return;
    }

    if (TakeRequest(&sWantsExit))
    {
        if (Document* playable = FindOnlyUnsavedPlayableDocument())
            AskSaveBeforeExit(playable);
        else if (HasUnsavedDocuments())
            OpenExitChangesDialog();
        else
            Exit();

        return;
    }

    StartDocumentClose();
}

static void UpdateSaveAll()
{
    if (sSaveQueue.empty() && !sSavingDocument && !sExitAfterSaving)
        return;

    if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) || sWantsSaveDirect)
        return;

    if (sSavingDocument)
    {
        const bool failed = IsOpenDocument(sSavingDocument) && sSavingDocument->state().unsaved;
        sSavingDocument = nullptr;

        if (failed)
        {
            sSaveQueue.clear();
            sExitAfterSaving = false;
            return;
        }
    }

    while (!sSaveQueue.empty())
    {
        Document* document = sSaveQueue.front();
        sSaveQueue.erase(sSaveQueue.begin());

        if (!IsOpenDocument(document) || !document->state().unsaved)
            continue;

        ActivateDocument(document);

        sSavingDocument = document;
        sWantsSaveDirect = true;
        return;
    }

    if (TakeRequest(&sExitAfterSaving))
        Exit();
}

static void DrawSaveChangesDialog()
{
    CenterNextDialog();

    if (BeginDialog(sead::FormatFixedSafeString<64>(ICON_LC_SAVE " %s###Save", messages::saving::cSaveTitle).cstr(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        BeginDialogMessage(ICON_LC_CIRCLE_HELP);
        DialogNamedText(messages::saving::cSaveQuestionFormat, GetDocumentName().c_str());
        EndDialogMessage();

        BeginDialogFooter({messages::dialog::cYes, messages::dialog::cNo, messages::common::cCancel});

        if (DialogButton(messages::dialog::cYes))
        {
            if (ShouldWarnAboutMissingMetadata())
            {
                sPendingFileAction = sFileAction;
                ImGui::CloseCurrentPopup();
                ImGui::OpenPopup("###MetadataWarning");
            }
            else
            {
                if (SaveFile())
                    sFileAction();
                else
                    CancelDocumentClose();

                ImGui::CloseCurrentPopup();
            }
        }

        if (DialogButton(messages::dialog::cNo))
        {
            sFileAction();
            ImGui::CloseCurrentPopup();
        }

        if (DialogButton(messages::common::cCancel))
        {
            CancelDocumentClose();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

static void StartDirectSave()
{
    if (sWantsSaveDirect)
    {
        sWantsSaveDirect = false;

        if (Bfsar* archive = FindActiveSoundArchive())
            sPendingUnusedWaves = archive->findUnusedWaveFiles();

        if (!sPendingUnusedWaves.empty())
            ImGui::OpenPopup("###RemoveUnusedFiles");
        else
            ProceedDirectSave();
    }
}

static void DrawMetadataWarningDialog()
{
    CenterNextDialog();

    if (BeginDialog(messages::archive::cMetadataWarningTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text(ICON_LC_TRIANGLE_ALERT "%s", messages::archive::cMetadataWarningDetail);
        ImGui::Separator();

        Bfsar& archive = ActiveSoundArchive();
        bool saveMeta = archive.getSaveMetadata();

        if (ImGui::Checkbox(messages::archive::cMetadataWarningCheckbox, &saveMeta))
            archive.setSaveMetadata(saveMeta);

        BeginDialogFooter({messages::common::cSave, messages::common::cCancel});

        if (DialogButton(messages::common::cSave))
        {
            bool (*pendingAction)() = sPendingFileAction;
            sPendingFileAction = nullptr;

            if (!SaveFile())
                CancelDocumentClose();
            else if (pendingAction)
                pendingAction();

            ImGui::CloseCurrentPopup();
        }

        if (DialogButton(messages::common::cCancel))
        {
            CancelDocumentClose();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

static void DrawMetadataUpgradeDialog()
{
    Bfsar* archive = FindActiveSoundArchive();

    if (!archive)
        return;

    if (archive->hasPendingMetadataUpgrade() && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
        ImGui::OpenPopup("###MetadataUpgrade");

    CenterNextDialog();

    if (BeginDialog(sead::FormatFixedSafeString<96>(ICON_LC_FILE_JSON " %s###MetadataUpgrade", messages::archive::cMetadataUpgradeTitle).cstr(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        const u32 loaded = archive->getMetadataUpgradeLoadedCount();
        const u32 missing = archive->getMetadataUpgradeMissingCount();

        ImGui::TextUnformatted(messages::archive::cMetadataUpgradeDetail);

        if (missing > 0)
            ImGui::TextDisabled(messages::archive::cMetadataUpgradeMissingFormat, loaded, missing);
        else
            ImGui::TextDisabled(messages::archive::cMetadataUpgradeLoadedFormat, loaded);

        BeginDialogFooter({messages::dialog::cUpdate, messages::dialog::cLater});

        if (DialogButton(messages::dialog::cUpdate))
        {
            if (!archive->upgradeMetadata())
            {
                sead::FormatFixedSafeString<1024> msg(messages::archive::cMetadataUpgradeFailedFormat, sead::FormatFixedSafeString<1100>("%s.metadata.json", archive->getFilePath().cstr()).cstr());
                PopupMgr::instance()->addPopup({msg});
            }

            archive->dismissMetadataUpgrade();
            ImGui::CloseCurrentPopup();
        }

        DetailTooltip(messages::archive::cMetadataUpgradeButtonDetail);

        if (DialogButton(messages::dialog::cLater))
        {
            archive->dismissMetadataUpgrade();
            ImGui::CloseCurrentPopup();
        }

        DetailTooltip(messages::archive::cMetadataUpgradeLaterDetail);

        ImGui::EndPopup();
    }
}

static void DrawSaveInUseDialog()
{
    Bfsar* archive = FindActiveSoundArchive();

    if (!archive)
        return;

    if (archive->hasPendingSaveFinalize() && !ImGui::IsPopupOpen("###SaveFileInUse"))
    {
        sSaveInUseRemember = false;
        sSaveInUseRetryFailed = false;
        ImGui::OpenPopup("###SaveFileInUse");
    }

    CenterNextDialog();

    if (BeginDialog(sead::FormatFixedSafeString<64>("%s###SaveFileInUse", messages::saving::cInUseTitle).cstr(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text(ICON_LC_TRIANGLE_ALERT " %s", messages::saving::cInUseDetail);

        ImGui::Separator();

        ImGui::TextDisabled("%s", archive->getPendingSaveFinalizePath().cstr());

        if (sSaveInUseRetryFailed)
            ImGui::TextColored(uicolors::Marker(), "%s", messages::saving::cInUseStill);

        ImGui::Separator();

        ImGui::Checkbox(messages::saving::cInUseAlwaysSave, &sSaveInUseRemember);

        BeginDialogFooter({messages::dialog::cSaveAnyway, messages::dialog::cTryAgain, messages::common::cCancel});

        if (DialogButton(messages::dialog::cSaveAnyway))
        {
            if (archive->forcePendingSaveFinalize())
            {
                if (sSaveInUseRemember)
                {
                    sForceSaveWhenInUse = true;
                    SaveArchiveConfig();
                }

                MarkArchiveInSync();
                ImGui::CloseCurrentPopup();
            }
            else
            {
                PopupMgr::instance()->addPopup({messages::saving::cInUseWriteFailed});
            }
        }

        if (DialogButton(messages::dialog::cTryAgain))
        {
            if (archive->retryPendingSaveFinalize())
            {
                MarkArchiveInSync();
                ImGui::CloseCurrentPopup();
            }
            else
            {
                sSaveInUseRetryFailed = true;
            }
        }

        if (DialogButton(messages::common::cCancel))
        {
            archive->cancelPendingSaveFinalize();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

static void DrawRemoveUnusedFilesDialog()
{
    CenterNextDialog();
    ImGui::SetNextWindowSize(ImVec2(480.0f, 420.0f), ImGuiCond_Appearing);

    if (BeginDialog(sead::FormatFixedSafeString<64>("%s###RemoveUnusedFiles", messages::saving::cUnusedTitle).cstr(), nullptr, ImGuiWindowFlags_NoResize))
    {
        ImGui::Text(sPendingUnusedWaves.size() == 1 ? messages::saving::cUnusedCountOneFormat : messages::saving::cUnusedCountFormat, sPendingUnusedWaves.size());

        ImGui::Separator();
        ImGui::BeginChild("UnusedList", ImVec2(0.0f, -CalcDialogFooterHeight()), true);

        for (WaveFile* wave : sPendingUnusedWaves)
            ImGui::TextColored(uicolors::Marker(), "[%u] %s", wave->getId(), wave->getNameOrNull().cstr());

        ImGui::EndChild();

        BeginDialogFooter({messages::dialog::cRemoveAllAndSave, messages::dialog::cSaveAnyway}, false);

        if (DialogButton(messages::dialog::cRemoveAllAndSave))
        {
            sSoundPlayer.reset();

            ForgetRemovedItems(std::vector<Item*>(sPendingUnusedWaves.begin(), sPendingUnusedWaves.end()));

            if (ActiveSoundArchive().removeUnusedWaveFiles(sPendingUnusedWaves) > 0)
                SetUnsavedChanges(true);

            sPendingUnusedWaves.clear();
            ImGui::CloseCurrentPopup();
            ProceedDirectSave();
        }

        if (DialogButton(messages::dialog::cSaveAnyway))
        {
            sPendingUnusedWaves.clear();
            ImGui::CloseCurrentPopup();
            ProceedDirectSave();
        }

        ImGui::EndPopup();
    }
}

void DrawFileFlow()
{
    StartFileAction();
    StartSaveAs();
    DrawSaveChangesDialog();
    UpdateSaveAll();
    StartDirectSave();
    DrawGroupFileLocateDialog();
    DrawMetadataWarningDialog();
    DrawMetadataUpgradeDialog();
    DrawSaveInUseDialog();
    DrawRemoveUnusedFilesDialog();
    DrawNewFileDialog();
}
