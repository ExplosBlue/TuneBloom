#include <ui/file/File.h>

#include <ui/backup/Backup.h>
#include <ui/settings/Config.h>
#include <ui/file/FileDialogs.h>
#include <ui/file/FileFlow.h>
#include <ui/file/FileWindow.h>
#include <ui/file/GroupFileLocate.h>
#include <ui/project/ItemViews.h>
#include <ui/widgets/ListFilter.h>
#include <ui/file/LoadProgress.h>
#include <ui/Messages.h>
#include <ui/player/PlayerViews.h>
#include <ui/app/PopupMgr.h>
#include <ui/file/RecentFiles.h>
#include <core/TestHooks.h>
#include <ui/items/Selection.h>
#include <ui/file/UnsavedChanges.h>
#include <ui/app/Workspace.h>

#include <filedevice/seadFileDeviceMgr.h>
#include <filedevice/seadPath.h>
#include <framework/glfw/seadGameFrameworkBaseGlfw.h>

#include <app/Utilll.h>

#include <core/Debug.h>

#include <string>
#include <filesystem>
#include <vector>
#include <cstring>

#include <bfsar/files/InnerFile.h>
#include <bfsar/archive/Cmpbin.h>

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

Bfsar sBfsar;

sead::FixedSafeString<512> sDroppedFilePath;

bool ValidBFSARHeader(const void* file)
{
    return ValidBFSARHeader(file, sizeof(nw::snd::internal::SoundArchiveFile::FileHeader));
}

bool ValidBFSARHeader(const void* file, u32 size)
{
    const bool hasHeader = size >= sizeof(nw::snd::internal::SoundArchiveFile::FileHeader);

    bool isFSAR = hasHeader && sead::MemUtil::compare(file, "FSAR", 4) == 0;
    bool isCSAR = hasHeader && sead::MemUtil::compare(file, "CSAR", 4) == 0;

    const u8* byteOrderMark = static_cast<const u8*>(file) + offsetof(nw::ut::BinaryFileHeader, byteOrder);
    const bool hasByteOrder = hasHeader && ((byteOrderMark[0] == 0xFF && byteOrderMark[1] == 0xFE) || (byteOrderMark[0] == 0xFE && byteOrderMark[1] == 0xFF));

    if ((!isFSAR && !isCSAR) || !hasByteOrder)
    {
        PopupMgr::instance()->addPopup({ messages::archive::cNotArchive, nullptr });
        return false;
    }

    const nw::snd::internal::SoundArchiveFile::FileHeader& header = *reinterpret_cast<const nw::snd::internal::SoundArchiveFile::FileHeader*>(file);

    //? Setup global file endian
    {
        const void* byteOrder = sead::PtrUtil::addOffset(&header, offsetof(nw::ut::BinaryFileHeader, byteOrder));
        sFileEndian = sead::Endian::markToEndian(*(u16*)byteOrder);
    }

    //? Only check version for FSAR (BFSAR) files
    if (isFSAR)
    {
        bool inLegacyRange = header.version >= 0x00010000 && header.version < 0x00030000;
        bool isV3 = header.version == 0x03000000;

        if (!(inLegacyRange || isV3))
        {
            sead::FormatFixedSafeString<64> msg(messages::archive::cBfsarVersionUnsupportedFormat,(u32)header.version);
            PopupMgr::instance()->addPopup({msg, nullptr});
            return false;
        }
    }

    return true;
}

bool NewFile(ArchiveFormat format)
{
    return NewFile(format, getDefaultPlatformForFormat(format));
}

bool NewFile(ArchiveFormat format, ArchivePlatform platform)
{
    CloseFile();

    sBfsar.create(format, platform);

    const ArchiveFormatInfo* info = getFormatInfo(sBfsar.getFormat(), sBfsar.getPlatform());

    LOG_STR(sead::FormatFixedSafeString<64>("Creating new %s file", info->fmtName).cstr());
    util::updateTitle(sead::FormatFixedSafeString<64>(messages::archive::cNewFileNameFormat, info->extension).cstr(), true);

    SetUnsavedChanges(true);

    SetUITab(UIType::ProjectInfo);
    return true;
}

bool NewFile()
{
    return NewFile(sBfsar.getFormat(), sBfsar.getPlatform());
}

static constexpr f64 cPendingOpenDelay      = 0.12;
static constexpr s32 cPendingOpenFirstFrame = 4;

static sead::FixedSafeString<512> sNextOpenPath;
static sead::FixedSafeString<512> sPendingOpenPath;
static f64 sPendingOpenTime = 0.0;

void SetNextOpenPath(const char* path)
{
    sNextOpenPath = path;
}

void ClearNextOpenPath()
{
    sNextOpenPath.clear();
}

bool OpenFile(const char* path)
{
    SetNextOpenPath(path);
    return OpenFile();
}

bool OpenFile()
{
    sead::FixedSafeString<512> filePath;

    if (!sNextOpenPath.isEmpty())
    {
        filePath = sNextOpenPath;
        sNextOpenPath.clear();
    }
    else if (!sDroppedFilePath.isEmpty())
    {
        filePath = sDroppedFilePath;
        sDroppedFilePath.clear();
    }
    else
    {
        const u32 filterCount = 1;
        FileFilter filters[filterCount] = {
            { messages::archive::cOpenFilter, "*.bfsar *.bcsar *.cmpbin" }
        };

        if (!OpenFileDialog(&filePath, nullptr, filterCount, filters))
        {
            return false;
        }
    }

    sPendingOpenPath = filePath;
    sPendingOpenTime = ImGui::GetTime();

    return true;
}

void MarkArchiveInSync()
{
    const sead::SafeString& filePath = sBfsar.getFilePath();

    sead::FixedSafeString<512> fileName;
    sead::Path::getFileName(&fileName, filePath);

    util::updateTitle(fileName.cstr(), false);

    SetUnsavedChanges(false);

    AddRecentFile(filePath.cstr());

    if (!filePath.isEmpty())
        SetLastArchive(filePath.cstr());
}

void OpenLastArchiveAtStartup()
{
    const StartupSettings& settings = GetStartupSettings();

    if (!settings.reopenLastArchive || testhooks::IsEnabled() || !sDroppedFilePath.isEmpty())
        return;

    if (settings.lastArchive.empty() || settings.lastArchive.size() >= static_cast<size_t>(sDroppedFilePath.getBufferSize()))
        return;

    std::error_code ec;

    if (std::filesystem::is_regular_file(settings.lastArchive, ec))
        sDroppedFilePath = settings.lastArchive.c_str();
}

static bool LoadArchiveFile(const sead::SafeString& filePath)
{
    sead::FileDevice* device = sead::FileDeviceMgr::instance()->findDevice("native");
    SEAD_ASSERT(device);

    LOG_STR(filePath.cstr());
    sead::FileDevice::LoadArg arg;
    arg.path = filePath;

    u8* bfsarFile = device->tryLoad(arg);
    if (!bfsarFile)
    {
        PopupMgr::instance()->addPopup({ messages::archive::cOpenFailed });
        return false;
    }

    u32 fileSize = static_cast<u32>(arg.read_size);

    bool wasCmpbin = false;
    cmpbin::Codec cmpbinCodec = cmpbin::Codec::Lz4Frame;

    if (cmpbin::IsCompressed(bfsarFile, fileSize))
    {
        wasCmpbin = true;
        cmpbinCodec = cmpbin::DetectCodec(bfsarFile, fileSize);

        std::vector<u8> decompressed;
        if (!cmpbin::Decompress(bfsarFile, fileSize, decompressed))
        {
            delete bfsarFile;
            PopupMgr::instance()->addPopup({messages::archive::cCmpbinDecompressFailed});
            return false;
        }

        delete bfsarFile;

        u8 *buf = new u8[decompressed.size()];
        std::memcpy(buf, decompressed.data(), decompressed.size());
        bfsarFile = buf;
        fileSize = static_cast<u32>(decompressed.size());
    }

    if (!ValidBFSARHeader(bfsarFile, fileSize))
    {
        delete bfsarFile;
        return false;
    }

    CloseFile();

    if (!sBfsar.open(bfsarFile, fileSize, filePath, nullptr)) //? bfsarFile is freed here
    {
        sead::FormatFixedSafeString<1024> msg(messages::archive::cCorruptFormat, GetInnerFileDisplayName(sBfsar.getFormat(), InnerFileKind::SoundArchive), PopupMgr::instance()->getCorruptInfo().cstr());
        PopupMgr::instance()->addPopup({msg, nullptr});
        CloseFile();
        return false;
    }

    if (wasCmpbin)
        sBfsar.setCmpbinPreferZstd(cmpbinCodec == cmpbin::Codec::Zstd);

    MarkArchiveInSync();

    SetUITab(UIType::ProjectInfo);
    return true;
}

bool IsOpenPending()
{
    return !sPendingOpenPath.isEmpty();
}

void UpdatePendingOpen()
{
    if (sPendingOpenPath.isEmpty())
        return;

    if (ImGui::GetFrameCount() < cPendingOpenFirstFrame)
        return;

    if (!testhooks::IsEnabled() && ImGui::GetTime() - sPendingOpenTime < cPendingOpenDelay)
        return;

    const sead::FixedSafeString<512> filePath(sPendingOpenPath);
    sPendingOpenPath.clear();

    sead::FixedSafeString<512> fileName;
    sead::Path::getFileName(&fileName, filePath);

    loadprogress::Begin(fileName.cstr());
    LoadArchiveFile(filePath);
    loadprogress::End();

    OpenGroupFileLocate();
}

bool SaveFile()
{
    LOG_BOOL("isOpen", sBfsar.isOpen());

    if (testhooks::IsEnabled())
        return false;

    backup::KeepPreviousVersion();

    if (sBfsar.isOpen() && sBfsar.save())
    {
        MarkArchiveInSync();
        NotifyDeletedGroupFiles();
        return true;
    }
    return false;
}

bool SaveFileAs()
{
    if (!sBfsar.isOpen())
        return false;

    if (!sBfsar.validate_())
        return false;

    const ArchiveFormatInfo* info = getFormatInfo(sBfsar.getFormat(), sBfsar.getPlatform());

    if (!info)
    {
        LOG_STR("SaveFileAs: invalid archive format info");
        return false;
    }

    sead::FixedSafeString<64> filterPattern;
    filterPattern.format("*.%s", info->extension);

    sead::FixedSafeString<128> filterName;
    filterName.format("%s (%s)", info->label, filterPattern.cstr());

    sead::FixedSafeString<128> cmpbinName;
    FileFilter filters[2] = {{filterName.cstr(), filterPattern.cstr()}, {"", ""}};
    u32 filterCount = 1;

    if (sBfsar.isV3Bfsar())
    {
        cmpbinName.format(messages::archive::cCompressedFilterFormat, info->label);
        filters[1] = {cmpbinName.cstr(), "*.cmpbin"};
        filterCount = 2;
    }

    sead::FixedSafeString<512> defaultPath;

    if (sBfsar.getFilePath().isEmpty())
    {
        std::error_code ec;
        std::filesystem::path cwd = std::filesystem::current_path(ec);

        if (ec)
            defaultPath.format(messages::archive::cUntitledFileNameFormat, info->extension);
        else
            defaultPath = (cwd / sead::FormatFixedSafeString<64>(messages::archive::cUntitledFileNameFormat, info->extension).cstr()).string().c_str();
    }
    else
        defaultPath = sBfsar.getFilePath();

    sead::FixedSafeString<512> path;

    if (!SaveFileDialog(&path, nullptr, filterCount, filters, info->extension, defaultPath.cstr()))
        return false;

    LOG_STR(path.cstr());

    backup::KeepPreviousVersion(path.cstr());

    if (!sBfsar.saveAs(path))
    {
        LOG_STR("SaveFileAs failed to write archive");
        return false;
    }

    MarkArchiveInSync();

    return true;
}

bool CloseFile()
{
    PopupMgr::instance()->closeFile();

    CancelPendingExportsAndImports();

    ClearSelection();
    CloseAllFileWindows();
    CloseFilter();

    sSoundPlayer.reset();

    sBfsar.close();

    SetUnsavedChanges(false);

    util::updateTitle(nullptr, false);

    return true;
}

bool Exit()
{
    CloseFile();

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
