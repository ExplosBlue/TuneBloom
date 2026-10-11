#define IMGUI_DEFINE_MATH_OPERATORS
#include <ui/file/FileFlow.h>

#include <ui/backup/Backup.h>
#include <ui/bank/BankBundle.h>
#include <ui/settings/Config.h>
#include <ui/widgets/Dialogs.h>
#include <ui/export/Export.h>
#include <ui/file/File.h>
#include <ui/file/GroupFileLocate.h>
#include <ui/Messages.h>
#include <ui/player/PlayerViews.h>
#include <ui/app/PopupMgr.h>
#include <ui/items/Selection.h>
#include <ui/app/Shortcuts.h>
#include <ui/file/UnsavedChanges.h>
#include <ui/wave/WaveImportPanel.h>
#include <ui/widgets/Widgets.h>

#include <libs/IconsLucide.h>

#include <framework/glfw/seadGameFrameworkBaseGlfw.h>

#include <app/Utilll.h>

#include <cstdio>
#include <vector>

static bool sWantsNew = false;
static bool sWantsOpen = false;
static bool sWantsClose = false;
static bool sWantsExit = false;
static bool sNeedsNewFileFormat = false;
static bool sWantsSaveDirect = false;
static bool (*sPendingFileAction)() = nullptr;
static bool sSaveInUseRemember = false;
static bool sSaveInUseRetryFailed = false;

void RequestSaveFile()
{
    sWantsSaveDirect = true;
}

void RequestNewFile()
{
    sWantsNew = true;
}

void RequestOpenFile()
{
    sWantsOpen = true;
}

void RequestOpenFile(const char* path)
{
    SetNextOpenPath(path);
    sWantsOpen = true;
}

void RequestCloseFile()
{
    sWantsClose = true;
}

void RequestNewFileFormat()
{
    sNeedsNewFileFormat = true;
}

static bool NewFileFormatTrigger()
{
    sNeedsNewFileFormat = true;
    return true;
}

bool TryExit()
{
    sWantsExit = true;
    return false;
}

static bool (*sFileAction)() = nullptr;

static bool ShouldWarnAboutMissingMetadata()
{
    return sBfsar.isOpen() && !sBfsar.getSaveMetadata() && sBfsar.hasMetadataNames();
}

static std::vector<WaveFile*> sPendingUnusedWaves;

void CancelPendingExportsAndImports()
{
    CancelSoundExport();
    CancelFileExports();
    CancelBankBundleTransfers();
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
    if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
    {
        if (shortcuts::Pressed(shortcuts::Action::SaveProject) && sBfsar.isOpen())
            sWantsSaveDirect = true;

        if (shortcuts::Pressed(shortcuts::Action::OpenProject))
        {
            if (sBfsar.isOpen())
                sWantsOpen = true;
            else
                OpenFile();
        }

        if (shortcuts::Pressed(shortcuts::Action::NewProject))
        {
            if (sBfsar.isOpen())
                sWantsNew = true;
            else
                sNeedsNewFileFormat = true;
        }

        if (shortcuts::Pressed(shortcuts::Action::SaveProjectAs) && sBfsar.isOpen())
            SaveFileAs();
    }
}

static void OpenDroppedFile()
{
    if (!sDroppedFilePath.isEmpty())
    {
        if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
        {
            sead::GameFrameworkBaseGlfw* fw = sead::DynamicCast<sead::GameFrameworkBaseGlfw>(util::getFramework());
            if (fw)
            {
                glfwFocusWindow(fw->getWindowHandle());
            }

            if (sBfsar.isOpen())
            {
                sWantsOpen = true; // open with save prompt
            }
            else
            {
                OpenFile(); // open directly
            }
        }
    }
}

static bool TakeRequest(bool* request)
{
    const bool requested = *request;
    *request = false;

    return requested;
}

static void StartFileAction()
{
    bool (*action)() = nullptr;

    if (TakeRequest(&sWantsNew))
        action = &NewFileFormatTrigger;
    else if (TakeRequest(&sWantsOpen))
        action = &OpenFile;
    else if (TakeRequest(&sWantsClose))
        action = &CloseFile;
    else if (TakeRequest(&sWantsExit))
        action = &Exit;

    if (!action)
        return;

    if (!gUnsavedChanges)
    {
        action();
        return;
    }

    ImGui::OpenPopup("###Save");
    sFileAction = action;
}

static void DrawSaveChangesDialog()
{
    CenterNextDialog();

    if (BeginDialog(sead::FormatFixedSafeString<64>(ICON_LC_SAVE " %s###Save", messages::saving::cSaveTitle).cstr(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        BeginDialogMessage(ICON_LC_CIRCLE_HELP);
        DialogNamedText(messages::saving::cSaveQuestionFormat, backup::GetArchiveName().c_str());
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
                {
                    sFileAction();
                }
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
            ImGui::CloseCurrentPopup();

            sDroppedFilePath.clear(); // clear dropped file path to prevent multiple popups
            ClearNextOpenPath();
        }

        ImGui::EndPopup();
    }
}

static void StartDirectSave()
{
    if (sWantsSaveDirect)
    {
        sWantsSaveDirect = false;
        sPendingUnusedWaves = sBfsar.findUnusedWaveFiles();

        if (!sPendingUnusedWaves.empty())
            ImGui::OpenPopup("###RemoveUnusedFiles");
        else
            ProceedDirectSave();
    }
}

static void DrawMetadataWarningDialog()
{
    {
        CenterNextDialog();

        if (BeginDialog(messages::archive::cMetadataWarningTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text(ICON_LC_TRIANGLE_ALERT "%s", messages::archive::cMetadataWarningDetail);
            ImGui::Separator();

            bool saveMeta = sBfsar.getSaveMetadata();
            ImGui::Checkbox(messages::archive::cMetadataWarningCheckbox, &saveMeta);
            if (saveMeta != sBfsar.getSaveMetadata())
                sBfsar.setSaveMetadata(saveMeta);

            BeginDialogFooter({messages::common::cSave, messages::common::cCancel});

            if (DialogButton(messages::common::cSave))
            {
                if (SaveFile())
                {
                    if (sPendingFileAction)
                    {
                        sPendingFileAction();
                        sPendingFileAction = nullptr;
                    }
                }
                ImGui::CloseCurrentPopup();
            }

            if (DialogButton(messages::common::cCancel))
            {
                sPendingFileAction = nullptr;
                sDroppedFilePath.clear();
                ClearNextOpenPath();
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }
}

static void DrawMetadataUpgradeDialog()
{
    if (sBfsar.hasPendingMetadataUpgrade() && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
        ImGui::OpenPopup("###MetadataUpgrade");

    {
        CenterNextDialog();

        if (BeginDialog(sead::FormatFixedSafeString<96>(ICON_LC_FILE_JSON " %s###MetadataUpgrade", messages::archive::cMetadataUpgradeTitle).cstr(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            const u32 loaded = sBfsar.getMetadataUpgradeLoadedCount();
            const u32 missing = sBfsar.getMetadataUpgradeMissingCount();

            ImGui::TextUnformatted(messages::archive::cMetadataUpgradeDetail);

            if (missing > 0)
                ImGui::TextDisabled(messages::archive::cMetadataUpgradeMissingFormat, loaded, missing);
            else
                ImGui::TextDisabled(messages::archive::cMetadataUpgradeLoadedFormat, loaded);

            BeginDialogFooter({messages::dialog::cUpdate, messages::dialog::cLater});

            if (DialogButton(messages::dialog::cUpdate))
            {
                if (!sBfsar.upgradeMetadata())
                {
                    sead::FormatFixedSafeString<1024> msg(messages::archive::cMetadataUpgradeFailedFormat, sead::FormatFixedSafeString<1100>("%s.metadata.json", sBfsar.getFilePath().cstr()).cstr());
                    PopupMgr::instance()->addPopup({msg});
                }

                sBfsar.dismissMetadataUpgrade();
                ImGui::CloseCurrentPopup();
            }

            DetailTooltip(messages::archive::cMetadataUpgradeButtonDetail);

            if (DialogButton(messages::dialog::cLater))
            {
                sBfsar.dismissMetadataUpgrade();
                ImGui::CloseCurrentPopup();
            }

            DetailTooltip(messages::archive::cMetadataUpgradeLaterDetail);

            ImGui::EndPopup();
        }
    }
}

static void DrawSaveInUseDialog()
{
    if (sBfsar.hasPendingSaveFinalize() && !ImGui::IsPopupOpen("###SaveFileInUse"))
    {
        sSaveInUseRemember = false;
        sSaveInUseRetryFailed = false;
        ImGui::OpenPopup("###SaveFileInUse");
    }

    {
        CenterNextDialog();

        if (BeginDialog(sead::FormatFixedSafeString<64>("%s###SaveFileInUse", messages::saving::cInUseTitle).cstr(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text(ICON_LC_TRIANGLE_ALERT " %s", messages::saving::cInUseDetail);

            ImGui::Separator();

            ImGui::TextDisabled("%s", sBfsar.getPendingSaveFinalizePath().cstr());

            if (sSaveInUseRetryFailed)
                ImGui::TextColored(ImVec4(0.90f, 0.45f, 0.45f, 1.0f), "%s", messages::saving::cInUseStill);

            ImGui::Separator();

            ImGui::Checkbox(messages::saving::cInUseAlwaysSave, &sSaveInUseRemember);

            BeginDialogFooter({messages::dialog::cSaveAnyway, messages::dialog::cTryAgain, messages::common::cCancel});

            if (DialogButton(messages::dialog::cSaveAnyway))
            {
                if (sBfsar.forcePendingSaveFinalize())
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
                if (sBfsar.retryPendingSaveFinalize())
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
                sBfsar.cancelPendingSaveFinalize();
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }
}

static void DrawRemoveUnusedFilesDialog()
{
    {
        CenterNextDialog();
        ImGui::SetNextWindowSize(ImVec2(480.0f, 420.0f), ImGuiCond_Appearing);

        if (BeginDialog(sead::FormatFixedSafeString<64>("%s###RemoveUnusedFiles", messages::saving::cUnusedTitle).cstr(), nullptr, ImGuiWindowFlags_NoResize))
        {
            ImGui::Text(sPendingUnusedWaves.size() == 1 ? messages::saving::cUnusedCountOneFormat : messages::saving::cUnusedCountFormat, sPendingUnusedWaves.size());

            ImGui::Separator();
            ImGui::BeginChild("UnusedList", ImVec2(0.0f, -CalcDialogFooterHeight()), true);

            const ImVec4 removeColor(0.90f, 0.45f, 0.45f, 1.0f);
            for (WaveFile *wave : sPendingUnusedWaves)
                ImGui::TextColored(removeColor, "[%u] %s", wave->getId(), wave->getNameOrNull().cstr());

            ImGui::EndChild();

            BeginDialogFooter({messages::dialog::cRemoveAllAndSave, messages::dialog::cSaveAnyway}, false);

            if (DialogButton(messages::dialog::cRemoveAllAndSave))
            {
                sSoundPlayer.reset();

                ForgetRemovedItems(std::vector<Item*>(sPendingUnusedWaves.begin(), sPendingUnusedWaves.end()));

                if (sBfsar.removeUnusedWaveFiles(sPendingUnusedWaves) > 0)
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
}

static void DrawNewFileFormatDialog()
{
    if (sNeedsNewFileFormat)
    {
        ImGui::OpenPopup("###NewFileFormat");
        sNeedsNewFileFormat = false;
    }

    {
        CenterNextDialog();

        if (BeginDialog(sead::FormatFixedSafeString<64>("%s###NewFileFormat", messages::common::cNewArchive).cstr(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            static int sSelected = 0;
            static u32 sVersion = 0;
            static bool sIncludeStringTable = true;

            if (ImGui::IsWindowAppearing())
            {
                sSelected = 0;
                sVersion = cFormatTable[sSelected].defaultVersion;
                sIncludeStringTable = true;
            }

            static char sLabels[IM_ARRAYSIZE(cFormatTable)][64];
            const char *labels[IM_ARRAYSIZE(cFormatTable)];

            for (int i = 0; i < IM_ARRAYSIZE(cFormatTable); i++)
            {
                snprintf(sLabels[i], sizeof(sLabels[i]), "%s (%s)", cFormatTable[i].fmtName, cFormatTable[i].systemName);
                labels[i] = sLabels[i];
            }

            ImGui::SetNextItemWidth(240.0f);
            if (ComboScroll(messages::project::cPlatformTitle, &sSelected, labels, IM_ARRAYSIZE(cFormatTable)))
                sVersion = cFormatTable[sSelected].defaultVersion;

            const ArchiveFormatInfo &info = cFormatTable[sSelected];

            DrawVersionUI(&sVersion, info.format);

            ImGui::Checkbox(messages::archive::cIncludeStringTable, &sIncludeStringTable);
            DetailTooltip(messages::project::cStringTableDetail);

            BeginDialogFooter({messages::dialog::cCreate, messages::common::cCancel});

            bool create = DialogButton(messages::dialog::cCreate);

            if (DialogButton(messages::common::cCancel))
                ImGui::CloseCurrentPopup();

            if (create)
            {
                NewFile(info.format, info.platform);
                sBfsar.setVersion(sVersion);
                sBfsar.setIncludeStringTable(sIncludeStringTable);
                SetUnsavedChanges(true);
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }
}

void DrawFileFlow()
{
    OpenDroppedFile();
    StartFileAction();
    DrawSaveChangesDialog();
    StartDirectSave();
    DrawGroupFileLocateDialog();
    DrawMetadataWarningDialog();
    DrawMetadataUpgradeDialog();
    DrawSaveInUseDialog();
    DrawRemoveUnusedFilesDialog();
    DrawNewFileFormatDialog();
}
