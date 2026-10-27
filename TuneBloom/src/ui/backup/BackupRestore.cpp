#include <ui/backup/BackupInternal.h>

#include <ui/file/Document.h>
#include <ui/file/DocumentMgr.h>
#include <ui/file/DocumentState.h>
#include <ui/file/File.h>
#include <ui/Messages.h>
#include <ui/app/PopupMgr.h>
#include <ui/project/Sidebar.h>
#include <ui/file/RecentFiles.h>
#include <ui/file/UnsavedChanges.h>
#include <ui/app/Workspace.h>

namespace backup
{

bool SaveRestorePoint(const Snapshot& target)
{
    if (!IsDocumentOpen() || !HasUnsavedChanges() || !IsActive())
        return true;

    PinSnapshot(target.dir);

    Bfsar::BackupResult result;

    return WriteSnapshot(Kind::Restore, &result);
}

static std::string GetSnapshotUntitledKey(const Snapshot& snapshot)
{
    return fs::path(snapshot.dir).parent_path().filename().string();
}

static Document* FindUntitledDocument(const std::string& key)
{
    for (Document* document : GetOpenDocuments())
    {
        if (document->filePath().isEmpty() && document->state().backup.untitledKey == key)
            return document;
    }

    return nullptr;
}

static void FinishRestore(const Snapshot& snapshot, UIType tab)
{
    SetUnsavedChanges(false);

    PinSnapshot(snapshot.dir);

    if (snapshot.originalPath.empty())
        SetUntitledKey(GetSnapshotUntitledKey(snapshot));

    SetUnsavedChanges(true);

    MarkEditsBackedUp();

    if (IsActive())
        WriteMarker(snapshot.dir);

    SetUITab(tab);
}

static bool ReadSnapshotFile(const std::string& path, Document::File* outFile)
{
    if (!Document::Load(sead::FixedSafeString<1100>(path.c_str()), outFile) && Document::IsCompleteFile(outFile->data, outFile->size))
        return true;

    PopupMgr::instance()->addPopup({messages::backup::cRestoreReadFailed, nullptr});
    return false;
}

static bool CheckSnapshotFile(const Document::File& file, bool untitled)
{
    const char* error = nullptr;

    if (!Document::Check(file, &error))
    {
        if (error)
            PopupMgr::instance()->addPopup({messages::backup::cRestoreReadFailed, nullptr});

        return false;
    }

    if (untitled && file.kind == Document::Kind::AudioResource)
    {
        PopupMgr::instance()->addPopup({messages::backup::cRestoreReadFailed, nullptr});
        return false;
    }

    sead::Endian::Types endian = sead::Endian::eBig;

    if (Document::PeekEndian(file, &endian))
        PrepareParseEndian(endian);

    return true;
}

bool RestoreSnapshot(const Snapshot& snapshot)
{
    const std::string archivePath = (fs::path(snapshot.dir) / snapshot.archiveName).string();
    const bool untitled = snapshot.originalPath.empty();

    Document::File file;

    if (!ReadSnapshotFile(archivePath, &file) || !CheckSnapshotFile(file, untitled))
    {
        Document::Free(&file);
        return false;
    }

    const std::string openPath = untitled ? archivePath : snapshot.originalPath;
    const sead::FixedSafeString<1100> metadataSource(archivePath.c_str());

    Document* replaced = untitled ? FindUntitledDocument(GetSnapshotUntitledKey(snapshot)) : FindOpenDocument(snapshot.originalPath.c_str());
    Document* document = nullptr;

    {
        DocumentFocus focus(nullptr);
        document = Document::Open(&file, sead::FixedSafeString<1100>(openPath.c_str()), &metadataSource);
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
        AddDocument(document, false);

    document->restoreSidecars(snapshot);
    document->applyRestoredSnapshot(snapshot, untitled);

    if (!untitled)
        AddRecentFile(openPath.c_str());

    ShowSidebarProject();
    FinishRestore(snapshot, document->defaultTab());

    return true;
}

}
