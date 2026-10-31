#include <ui/file/File.h>

#include <ui/backup/Backup.h>
#include <ui/browser/FilesPanel.h>
#include <ui/settings/Config.h>
#include <ui/file/DocumentMgr.h>
#include <ui/file/DocumentState.h>
#include <ui/file/FileDialogs.h>
#include <ui/file/FileFlow.h>
#include <ui/file/GroupFileLocate.h>
#include <ui/file/LoadProgress.h>
#include <messages/Messages.h>
#include <ui/file/OpenQueue.h>
#include <bfsar/model/MessageLog.h>
#include <ui/player/PlayerFrame.h>
#include <ui/project/Sidebar.h>
#include <ui/file/RecentFiles.h>
#include <core/TestHooks.h>
#include <ui/file/UnsavedChanges.h>
#include <ui/items/Selection.h>
#include <ui/app/Workspace.h>

#include <bfsar/files/InnerFile.h>
#include <bfsar/io/FileIo.h>
#include <bfsar/io/PackFile.h>
#include <scan/FileProbe.h>
#include <core/Debug.h>

#include <filedevice/seadPath.h>
#include <framework/glfw/seadGameFrameworkBaseGlfw.h>

#include <app/Utilll.h>

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

bool IsDocumentOpen()
{
    return ActiveDocument() != nullptr;
}

const sead::SafeString& GetDocumentPath()
{
    const Document* document = ActiveDocument();

    return document ? document->filePath() : sead::SafeString::cEmptyString;
}

const char* GetDocumentExtension()
{
    const Document* document = ActiveDocument();

    return document ? document->extension() : GetInnerFileExtension(ArchiveFormat::BFSAR, InnerFileKind::SoundArchive);
}

void RenumberDocumentList(Item::List& list)
{
    if (Document* document = ActiveDocument())
        document->renumber(list);
    else
        Bfsar::updateList(list);
}

std::string GetDocumentName()
{
    const Document* document = ActiveDocument();

    return document ? document->displayName() : backup::GetArchiveName();
}

static bool IsDocumentKind(BrowserFileKind kind)
{
    return kind == BrowserFileKind::SoundArchive || kind == BrowserFileKind::AudioResource || kind == BrowserFileKind::ResourceList || kind == BrowserFileKind::Stream || kind == BrowserFileKind::Wave;
}

static bool IsDocumentFile(const char* path)
{
    BrowserFileKind kind;

    return ProbeFileKind(path, &kind) && IsDocumentKind(kind);
}

static bool IsPinnableFile(const char* path)
{
    BrowserFileKind kind;

    return ProbeFileKind(path, &kind) && (IsDocumentKind(kind) || kind == BrowserFileKind::Wav);
}

bool PinExternalPath(const char* path)
{
    std::error_code error;

    if (!std::filesystem::is_directory(path, error) && !IsPinnableFile(path))
        return false;

    RequestFilesPanelRoot(path, true);

    return true;
}

bool OpenExternalPath(const char* path)
{
    std::error_code error;

    if (std::filesystem::is_directory(path, error))
    {
        RequestFilesPanelRoot(path);
        return true;
    }

    if (!IsDocumentFile(path))
        return false;

    openqueue::PushExternal(path);

    return true;
}

bool IsValidDocumentName(const sead::SafeString& name)
{
    const Document* document = ActiveDocument();

    return document && document->isValidName(name);
}

const char* GetDocumentNameRuleDetail()
{
    const Document* document = ActiveDocument();

    return document ? document->getNameRuleDetail() : messages::item::cNameInvalidDetail;
}

bool IsUniqueDocumentName(const sead::SafeString& name, const Item* ignoreItem)
{
    const Document* document = ActiveDocument();

    return document && document->isUniqueName(name, ignoreItem);
}

void AddNewDocument(Document* document)
{
    AddDocument(document, false);
    SetUnsavedChanges(true);
    SetUITab(document->defaultTab());

    if (Item* item = document->defaultSelection())
        sSelectedItem = item;
}

bool NewFile(ArchiveFormat format, ArchivePlatform platform)
{
    AddNewDocument(Document::Create(format, platform));

    const Bfsar& archive = ActiveSoundArchive();
    const ArchiveFormatInfo* info = getFormatInfo(archive.getFormat(), archive.getPlatform());

    LOG_STR(sead::FormatFixedSafeString<64>("Creating new %s file", info->fmtName).cstr());

    ShowSidebarProject();
    return true;
}

static constexpr f64 cPendingOpenDelay      = 0.12;
static constexpr s32 cPendingOpenFirstFrame = 4;

static openqueue::Request sPendingOpen;
static bool sHasPendingOpen = false;
static f64 sPendingOpenTime = 0.0;
static sead::FixedSafeString<512> sStartupFile;
static bool sIsStartupPlayer = false;

void SetStartupFile(const char* path)
{
    if (path && std::strlen(path) < static_cast<size_t>(sStartupFile.getBufferSize()))
        sStartupFile = path;
}

void SetStartupPlayer()
{
    sIsStartupPlayer = true;
}

static FileFilter sDocumentFilters[] = {
    { messages::archive::cOpenFilterAll, "*.bfsar *.bcsar *.cmpbin *.bea *.aras *.bars *.barslist *.bwav *.bfstm *.bcstm *.opus *.bfwav *.bcwav *.zs *.pack" },
    { messages::archive::cOpenFilter, "*.bfsar *.bcsar *.cmpbin *.bea *.aras" },
    { messages::resource::cOpenFilter, "*.bars *.bwav *.zs *.pack" },
    { messages::barslist::cOpenFilter, "*.barslist" },
    { messages::stream::cOpenFilter, "*.bfstm *.bcstm *.opus" },
    { messages::wavefile::cOpenFilter, "*.bfwav *.bcwav" }
};

bool OpenFile()
{
    sead::FixedSafeString<512> filePath;

    if (!OpenFileDialog(&filePath, nullptr, static_cast<u32>(std::size(sDocumentFilters)), sDocumentFilters))
        return false;

    openqueue::Push(filePath.cstr());

    return true;
}

bool ChooseDocumentFiles(std::vector<std::string>* paths, const char* title)
{
    return OpenFilesDialog(paths, title, static_cast<u32>(std::size(sDocumentFilters)), sDocumentFilters);
}

static void RecordDocumentPath()
{
    Document* document = ActiveDocument();

    RefreshWindowTitle();

    if (!document)
        return;

    RefreshDocumentPath(document);

    if (document->filePath().isEmpty())
        return;

    if (IsRememberedDocument(document))
        AddRecentFile(document->filePath().cstr());

    RecordDocumentSession();
}

void MarkArchiveInSync()
{
    SetUnsavedChanges(false);
    RecordDocumentPath();
}

void RunStartupAction()
{
    if (!sStartupFile.isEmpty())
    {
        openqueue::Push(sStartupFile.cstr(), openqueue::Mode::Pin, openqueue::Origin::System);
        sStartupFile.clear();
    }

    openqueue::Request launch;

    if (openqueue::Peek(&launch))
    {
        PreparePlayerFrame(launch);
        return;
    }

    if (sIsStartupPlayer)
    {
        PrepareEmptyPlayerFrame();
        return;
    }

    const StartupSettings& settings = GetStartupSettings();

    if (settings.action == StartupAction::StartPage || testhooks::IsEnabled())
        return;

    std::vector<std::string> documents = settings.sessionDocuments;

    if (documents.empty() && !settings.lastArchive.empty())
        documents.push_back(settings.lastArchive);

    for (const std::string& document : documents)
    {
        if (packfile::IsOnDisk(document))
            openqueue::Push(document.c_str());
    }

    if (documents.size() > 1 && packfile::IsOnDisk(settings.lastArchive))
        openqueue::Push(settings.lastArchive.c_str());
}

static bool IsSystemFileTarget(const char* path, singleinstance::Target target)
{
    switch (target)
    {
        case singleinstance::Target::OpenDocument:
            return FindOpenDocument(path) != nullptr;

        case singleinstance::Target::Player:
            return IsPlayerFrameShown() && IsPlayerFile(path);

        case singleinstance::Target::Editor:
            return !IsPlayerFrameShown() && !IsFileEditorShown() && (!singleinstance::IsSupported() || GetStartupSettings().systemOpen == SystemOpen::NewTab);
    }

    return false;
}

bool NeedsNewWindow(const char* path)
{
    if (!IsDocumentFile(path))
        return false;

    for (singleinstance::Target target : { singleinstance::Target::OpenDocument, singleinstance::Target::Player, singleinstance::Target::Editor })
    {
        if (IsSystemFileTarget(path, target))
            return false;
    }

    return true;
}

bool TakeSystemFile(const char* path, singleinstance::Target target)
{
    if (!IsSystemFileTarget(path, target))
        return false;

    openqueue::PushExternal(path);

    return true;
}

static bool FinishSave(Document* document, SaveResult result, const char* error)
{
    if (result == SaveResult::Failed)
    {
        if (error)
            MessageLog::instance()->addPopup({ error, nullptr });

        return false;
    }

    PinDocument(document);

    if (result != SaveResult::Saved)
    {
        SetUnsavedChanges(true);
        RecordDocumentPath();
        MessageLog::instance()->addPopup({ error, nullptr });

        return false;
    }

    MarkArchiveInSync();
    document->finishSave();

    return true;
}

static bool LoadDocumentFile(const sead::SafeString& filePath, bool isPreview, Document* replaced)
{
    Document::File file;

    if (const char* error = Document::Load(filePath, &file))
    {
        Document::Free(&file);
        MessageLog::instance()->addPopup({ error, nullptr });
        return false;
    }

    if (!Document::CheckBeforeOpen(file))
    {
        Document::Free(&file);
        return false;
    }

    Document* document = nullptr;

    {
        DocumentFocus focus(nullptr);
        document = Document::Open(&file, filePath);
    }

    if (!document)
    {
        MessageLog::instance()->discardProcessedErrors();
        return false;
    }

    MessageLog::instance()->moveProcessedErrors(&document->state().itemErrors);

    if (replaced)
        ReplaceDocument(replaced, document);
    else
        AddDocument(document, isPreview);

    MarkArchiveInSync();

    SetUITab(document->defaultTab());

    if (Item* item = document->defaultSelection())
        sSelectedItem = item;

    return true;
}

static void ShowOpenedDocument(const openqueue::Request& request)
{
    if (request.origin != openqueue::Origin::Browser && request.mode != openqueue::Mode::Reload)
        ShowSidebarProject();
}

static openqueue::Request ResolveSingleEntryPack(const openqueue::Request& request)
{
    std::vector<packfile::Resource> resources;

    if (packfile::IsPackPath(request.path) || !packfile::ListResources(request.path, &resources) || resources.size() != 1)
        return request;

    openqueue::Request entryRequest = request;
    entryRequest.path = packfile::MakePath(request.path, resources.front().name);

    return entryRequest;
}

static void OpenRequestedFile(const openqueue::Request& queuedRequest)
{
    const openqueue::Request request = ResolveSingleEntryPack(queuedRequest);
    Document* previous = ActiveDocument();
    Document* openDocument = FindOpenDocument(request.path.c_str());

    if (openDocument && request.mode != openqueue::Mode::Reload)
    {
        ActivateDocument(openDocument);
        ShowOpenedDocument(request);

        if (request.mode == openqueue::Mode::Pin)
            PinDocument(openDocument);

        NotifyPlayerFrameOpen(request, previous, openDocument);
        return;
    }

    if (request.path.size() > static_cast<size_t>(fileio::cPathLengthMax))
    {
        MessageLog::instance()->addPopup({ messages::archive::cOpenFailed, nullptr });
        return;
    }

    if (packfile::IsSoundPackFile(request.path))
    {
        RequestFilesPanelRoot(std::filesystem::path(request.path).parent_path().string().c_str());
        return;
    }

    const sead::FixedSafeString<512> filePath(request.path.c_str());

    sead::FixedSafeString<512> fileName;
    sead::Path::getFileName(&fileName, filePath);

    loadprogress::Begin(fileName.cstr());
    const bool loaded = LoadDocumentFile(filePath, request.mode == openqueue::Mode::Preview, openDocument);
    loadprogress::End();

    NotifyPlayerFrameOpen(request, previous, loaded ? ActiveDocument() : nullptr);

    if (!loaded)
        return;

    ShowOpenedDocument(request);
    OpenGroupFileLocate();
}

bool IsOpenPending()
{
    return sHasPendingOpen || !openqueue::IsEmpty();
}

void UpdatePendingOpen()
{
    if (ImGui::GetFrameCount() < cPendingOpenFirstFrame || IsDocumentSwitchLocked())
        return;

    UpdatePendingActivation();

    if (!sHasPendingOpen)
    {
        if (!openqueue::Take(&sPendingOpen))
            return;

        sHasPendingOpen = true;
        sPendingOpenTime = ImGui::GetTime();
    }

    const bool waitsForFade = sPendingOpen.origin != openqueue::Origin::System && !testhooks::IsEnabled();

    if (waitsForFade && ImGui::GetTime() - sPendingOpenTime < cPendingOpenDelay)
        return;

    const openqueue::Request request = sPendingOpen;
    sHasPendingOpen = false;

    OpenRequestedFile(request);
}

bool SaveFile()
{
    LOG_BOOL("isOpen", IsDocumentOpen());

    if (testhooks::IsEnabled())
        return false;

    Document* document = ActiveDocument();

    if (!document || !document->prepareSave())
        return false;

    if (document->filePath().isEmpty())
        return SaveFileAs(document);

    backup::KeepPreviousVersion();

    const char* error = nullptr;

    return FinishSave(document, document->save(&error), error);
}

bool SaveFileAs(Document* document)
{
    if (!document)
        return false;

    DocumentFocus focus(document);

    if (!document->prepareSaveAs())
        return false;

    sead::FixedSafeString<512> path;

    if (!document->chooseSaveAsPath(&path))
        return false;

    const Document* openDocument = FindOpenDocument(path.cstr());

    if (openDocument && openDocument != document)
    {
        MessageLog::instance()->addPopup({ messages::saving::cOpenInOtherTab, nullptr });
        return false;
    }

    backup::KeepPreviousVersion(path.cstr());

    const char* error = nullptr;

    return FinishSave(document, document->saveAs(path, &error), error);
}

bool Exit()
{
    CloseAllDocuments();

    GetRecentFiles().clear();

    sead::GameFrameworkBaseGlfw* fw = sead::DynamicCast<sead::GameFrameworkBaseGlfw>(util::getFramework());
    SEAD_ASSERT(fw);
    fw->requestExit();

    return true;
}

bool CheckBlockCorrupt(const char* fileName, const char* blockName, const void* block)
{
    if (sead::MemUtil::compare(block, blockName, 4) != 0)
    {
        MessageLog::instance()->setCorruptInfo(sead::FormatFixedSafeString<64>(messages::archive::cBlockNotFoundFormat,fileName, blockName));
        return false;
    }

    return true;
}

bool CheckBlockCorruptError(const char* fileName, const char* blockName, const void* block)
{
    if (sead::MemUtil::compare(block, blockName, 4) != 0)
    {
        MessageLog::instance()->pushCurrentItemError(sead::FormatFixedSafeString<64>(messages::archive::cBlockNotFoundFormat,fileName, blockName));
        return false;
    }

    return true;
}
