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
#include <ui/Messages.h>
#include <ui/file/OpenQueue.h>
#include <ui/app/PopupMgr.h>
#include <ui/project/Sidebar.h>
#include <ui/file/RecentFiles.h>
#include <core/TestHooks.h>
#include <ui/file/UnsavedChanges.h>
#include <ui/app/Workspace.h>

#include <bfsar/files/InnerFile.h>
#include <bfsar/io/PackFile.h>
#include <browser/FileProbe.h>
#include <core/Debug.h>

#include <filedevice/seadPath.h>
#include <framework/glfw/seadGameFrameworkBaseGlfw.h>

#include <app/Utilll.h>

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

bool CreateDirectoryRecursively(const std::string& directory)
{
    LOG_STR(directory.c_str());
    namespace fs = std::filesystem;

    std::error_code ec;
    if (fs::exists(directory, ec))
    {
        return fs::is_directory(directory, ec);
    }

    return fs::create_directories(directory, ec);
}

bool IsDocumentOpen()
{
    return ActiveDocument() != nullptr;
}

bool IsAudioResourceOpen()
{
    return FindActiveAudioResource() != nullptr;
}

const sead::SafeString& GetDocumentPath()
{
    const Document* document = ActiveDocument();

    return document ? document->filePath() : sead::SafeString::cEmptyString;
}

const sead::SafeString& GetDocumentLoadedPath()
{
    const Document* document = ActiveDocument();

    return document ? document->loadedPath() : sead::SafeString::cEmptyString;
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

static bool IsDocumentFile(const char* path)
{
    std::error_code error;
    const u64 fileSize = std::filesystem::file_size(path, error);

    FileProbe probe;

    if (error || !ProbeFile(path, fileSize, ProbeDepth::Kind, &probe))
        return false;

    return probe.kind == BrowserFileKind::SoundArchive || probe.kind == BrowserFileKind::AudioResource;
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

bool IsUniqueDocumentName(const sead::SafeString& name, const Item* ignoreItem)
{
    const Document* document = ActiveDocument();

    return document && document->isUniqueName(name, ignoreItem);
}

bool NewFile(ArchiveFormat format, ArchivePlatform platform)
{
    Document* document = Document::Create(format, platform);

    AddDocument(document, false);

    const Bfsar& archive = ActiveSoundArchive();
    const ArchiveFormatInfo* info = getFormatInfo(archive.getFormat(), archive.getPlatform());

    LOG_STR(sead::FormatFixedSafeString<64>("Creating new %s file", info->fmtName).cstr());

    SetUnsavedChanges(true);
    ShowSidebarProject();

    SetUITab(UIType::ProjectInfo);
    return true;
}

static constexpr f64 cPendingOpenDelay      = 0.12;
static constexpr s32 cPendingOpenFirstFrame = 4;

static openqueue::Request sPendingOpen;
static bool sHasPendingOpen = false;
static f64 sPendingOpenTime = 0.0;
static sead::FixedSafeString<512> sStartupFile;

void SetStartupFile(const char* path)
{
    if (path && std::strlen(path) < static_cast<size_t>(sStartupFile.getBufferSize()))
        sStartupFile = path;
}

bool OpenFile()
{
    sead::FixedSafeString<512> filePath;

    const u32 filterCount = 3;
    FileFilter filters[filterCount] = {
        { messages::archive::cOpenFilterAll, "*.bfsar *.bcsar *.cmpbin *.bea *.bars *.bwav *.zs *.pack" },
        { messages::archive::cOpenFilter, "*.bfsar *.bcsar *.cmpbin *.bea" },
        { messages::resource::cOpenFilter, "*.bars *.bwav *.zs *.pack" }
    };

    if (!OpenFileDialog(&filePath, nullptr, filterCount, filters))
        return false;

    openqueue::Push(filePath.cstr());

    return true;
}

void MarkArchiveInSync()
{
    Document* document = ActiveDocument();

    SetUnsavedChanges(false);
    RefreshWindowTitle();

    if (!document)
        return;

    RefreshDocumentPath(document);

    if (document->filePath().isEmpty())
        return;

    if (!document->state().isPreview)
        AddRecentFile(document->filePath().cstr());

    RecordDocumentSession();
}

void RunStartupAction()
{
    if (!sStartupFile.isEmpty())
    {
        openqueue::Push(sStartupFile.cstr());
        sStartupFile.clear();
        return;
    }

    const StartupSettings& settings = GetStartupSettings();

    if (settings.action == StartupAction::StartPage || testhooks::IsEnabled() || !openqueue::IsEmpty())
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

static bool FinishSave(SaveResult result, const char* error)
{
    if (result == SaveResult::Failed)
    {
        if (error)
            PopupMgr::instance()->addPopup({ error, nullptr });

        return false;
    }

    PinDocument(ActiveDocument());
    MarkArchiveInSync();

    if (result == SaveResult::Saved)
        return true;

    SetUnsavedChanges(true);
    PopupMgr::instance()->addPopup({ error, nullptr });

    return false;
}

static bool CheckDocumentFile(const Document::File& file)
{
    const char* error = nullptr;

    if (Document::Check(file, &error))
    {
        sead::Endian::Types endian = sead::Endian::eBig;

        if (Document::PeekEndian(file, &endian))
            PrepareParseEndian(endian);

        return true;
    }

    if (error)
        PopupMgr::instance()->addPopup({ error, nullptr });

    return false;
}

static bool LoadDocumentFile(const sead::SafeString& filePath, bool isPreview, Document* replaced)
{
    Document::File file;

    if (const char* error = Document::Load(filePath, &file))
    {
        Document::Free(&file);
        PopupMgr::instance()->addPopup({ error, nullptr });
        return false;
    }

    if (!CheckDocumentFile(file))
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
        PopupMgr::instance()->discardProcessedErrors();
        return false;
    }

    PopupMgr::instance()->moveProcessedErrors(&document->state().itemErrors);

    if (replaced)
        ReplaceDocument(replaced, document);
    else
        AddDocument(document, isPreview);

    MarkArchiveInSync();

    SetUITab(document->defaultTab());
    return true;
}

static void ShowOpenedDocument(const openqueue::Request& request)
{
    if (request.origin == openqueue::Origin::App && request.mode != openqueue::Mode::Reload)
        ShowSidebarProject();
}

static void OpenRequestedFile(const openqueue::Request& request)
{
    Document* openDocument = FindOpenDocument(request.path.c_str());

    if (openDocument && request.mode != openqueue::Mode::Reload)
    {
        ActivateDocument(openDocument);
        ShowOpenedDocument(request);

        if (request.mode == openqueue::Mode::Pin)
            PinDocument(openDocument);

        return;
    }

    if (request.path.size() >= static_cast<size_t>(sStartupFile.getBufferSize()))
    {
        PopupMgr::instance()->addPopup({ messages::archive::cOpenFailed, nullptr });
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

    if (!testhooks::IsEnabled() && ImGui::GetTime() - sPendingOpenTime < cPendingOpenDelay)
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

    backup::KeepPreviousVersion();

    const char* error = nullptr;

    if (!FinishSave(document->save(&error), error))
        return false;

    document->finishSave();
    return true;
}

bool SaveFileAs()
{
    Document* document = ActiveDocument();

    if (!document || !document->prepareSaveAs())
        return false;

    sead::FixedSafeString<512> path;

    if (!document->chooseSaveAsPath(&path))
        return false;

    backup::KeepPreviousVersion(path.cstr());

    const char* error = nullptr;

    return FinishSave(document->saveAs(path, &error), error);
}

bool CloseFile()
{
    if (Document* document = ActiveDocument())
        CloseDocument(document);

    return true;
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
        PopupMgr::instance()->setCorruptInfo(sead::FormatFixedSafeString<64>(messages::archive::cBlockNotFoundFormat,fileName, blockName));
        return false;
    }

    return true;
}

bool CheckBlockCorruptError(const char* fileName, const char* blockName, const void* block)
{
    if (sead::MemUtil::compare(block, blockName, 4) != 0)
    {
        PopupMgr::instance()->pushCurrentItemError(sead::FormatFixedSafeString<64>(messages::archive::cBlockNotFoundFormat,fileName, blockName));
        return false;
    }

    return true;
}
