#include <ui/backup/BackupInternal.h>

#include <ui/file/File.h>
#include <ui/Messages.h>
#include <ui/app/PopupMgr.h>
#include <ui/file/RecentFiles.h>
#include <ui/file/UnsavedChanges.h>
#include <ui/app/Workspace.h>

#include <bfsar/archive/Cmpbin.h>

#include <filedevice/seadFileDeviceMgr.h>

#include <app/Utilll.h>

#include <cstring>

namespace backup
{

static std::string sRestorePointDir;

static u8* LoadArchiveFile(const std::string& path, u32* outSize, bool* outCmpbin, cmpbin::Codec* outCodec)
{
    sead::FileDevice* device = sead::FileDeviceMgr::instance()->findDevice("native");

    if (!device)
        return nullptr;

    sead::FileDevice::LoadArg arg;
    arg.path = path.c_str();

    u8* file = device->tryLoad(arg);

    if (!file)
        return nullptr;

    u32 size = static_cast<u32>(arg.read_size);

    *outCmpbin = cmpbin::IsCompressed(file, size);

    if (*outCmpbin)
    {
        *outCodec = cmpbin::DetectCodec(file, size);

        std::vector<u8> decompressed;
        const bool decoded = cmpbin::Decompress(file, size, decompressed);

        delete file;

        if (!decoded || decompressed.empty())
            return nullptr;

        file = new u8[decompressed.size()];
        std::memcpy(file, decompressed.data(), decompressed.size());
        size = static_cast<u32>(decompressed.size());
    }

    *outSize = size;

    return file;
}

bool SaveRestorePoint(const Snapshot& target)
{
    sRestorePointDir.clear();

    if (!sBfsar.isOpen() || !gUnsavedChanges || !IsActive())
        return true;

    PinSnapshot(target.dir);

    Bfsar::BackupResult result;

    return WriteSnapshot(Kind::Restore, &result, &sRestorePointDir);
}

static void OfferRestorePoint()
{
    const std::string dir = sRestorePointDir;
    sRestorePointDir.clear();

    Recovery recovery;

    if (dir.empty() || !ReadInfoFile(dir, &recovery.snapshot))
        return;

    WriteMarker(dir);

    recovery.markerPath = GetMarkerPath().string();
    AddRecovery(std::move(recovery));
}

static void RestoreStreams(const Snapshot& snapshot)
{
    const fs::path dir(snapshot.dir);

    for (const Bfsar::BackupStream& stream : snapshot.streams)
    {
        const std::string streamFile = (dir / stream.path).string();

        if (!sBfsar.restoreStreamFromFile(stream.path.c_str(), streamFile.c_str()))
        {
            sead::FormatFixedSafeString<1024> msg(messages::backup::cRestoreStreamFormat, stream.path.c_str());
            PopupMgr::instance()->addPopup({msg, nullptr});
        }
    }
}

bool RestoreSnapshot(const Snapshot& snapshot)
{
    const fs::path dir(snapshot.dir);
    const std::string archivePath = (dir / snapshot.archiveName).string();

    u32 fileSize = 0;
    bool wasCmpbin = false;
    cmpbin::Codec codec = cmpbin::Codec::Lz4Frame;

    u8* file = LoadArchiveFile(archivePath, &fileSize, &wasCmpbin, &codec);

    if (!file || !HasCompleteArchive(file, fileSize))
    {
        delete file;
        PopupMgr::instance()->addPopup({messages::backup::cRestoreReadFailed, nullptr});
        return false;
    }

    if (!ValidBFSARHeader(file, fileSize))
    {
        delete file;
        return false;
    }

    const bool untitled = snapshot.originalPath.empty();
    const std::string openPath = untitled ? archivePath : snapshot.originalPath;

    CloseFile();

    const sead::FixedSafeString<1100> metadataSource(archivePath.c_str());

    if (!sBfsar.open(file, fileSize, openPath.c_str(), nullptr, &metadataSource))
    {
        sead::FormatFixedSafeString<1024> msg(messages::archive::cCorruptFormat, GetInnerFileDisplayName(sBfsar.getFormat(), InnerFileKind::SoundArchive), PopupMgr::instance()->getCorruptInfo().cstr());
        PopupMgr::instance()->addPopup({msg, nullptr});
        CloseFile();
        OfferRestorePoint();
        return false;
    }

    sRestorePointDir.clear();

    if (wasCmpbin)
        sBfsar.setCmpbinPreferZstd(codec == cmpbin::Codec::Zstd);

    RestoreStreams(snapshot);

    if (snapshot.saveNames >= 0)
        sBfsar.setSaveMetadata(snapshot.saveNames != 0);

    if (untitled)
        sBfsar.clearFilePath();
    else
        AddRecentFile(openPath.c_str());

    util::updateTitle(fs::path(openPath).filename().string().c_str(), true);

    SetUnsavedChanges(false);

    PinSnapshot(snapshot.dir);

    if (untitled)
        SetUntitledKey(dir.parent_path().filename().string());

    SetUnsavedChanges(true);

    MarkEditsBackedUp();

    if (IsActive())
        WriteMarker(snapshot.dir);

    SetUITab(UIType::ProjectInfo);

    return true;
}

}
