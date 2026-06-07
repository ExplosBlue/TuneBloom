#include <ui/UI.h>

#include <ui/PopupMgr.h>

#include <snd/SoundThread.h>

#include <filedevice/seadFileDeviceMgr.h>
#include <filedevice/seadPath.h>
#include <framework/glfw/seadGameFrameworkBaseGlfw.h>
#include <stream/seadFileDeviceStream.h>

#include <Utilll.h>

#include <Debug.h>

#include <string>
#include <filesystem>

#include <portable-file-dialogs.h>

#include <bfsar/InnerFile.h>

bool OpenFileDialog(sead::BufferedSafeString* outPath, const char* title, u32 filterCount, FileFilter* filters)
{
    LOG_FMT("title=\"%s\" filterCount=%u", title ? title : "nullptr", filterCount);
    SEAD_ASSERT(outPath);

    std::vector<std::string> filtersVec;

    if (filterCount > 0)
    {
        SEAD_ASSERT(filters);

        for (u32 i = 0; i < filterCount; i++)
        {
            filtersVec.push_back(filters[i].name);
            filtersVec.push_back(filters[i].filter);
        }
    }

    filtersVec.push_back("All Files (*.*)");
    filtersVec.push_back("*.*");

    std::vector<std::string> result = pfd::open_file(title ? title : "", "", filtersVec, pfd::opt::none).result();
    if (result.empty())
    {
        return false;
    }

    LOG_STR(result[0].c_str());
    outPath->copy(result[0].c_str());
    return true;
}

bool SaveFileDialog(sead::BufferedSafeString* outPath, const char* title, u32 filterCount, FileFilter* filters, const char* defaultExt)
{
    LOG_FMT("title=\"%s\" filterCount=%u defaultExt=\"%s\"", title ? title : "nullptr", filterCount, defaultExt ? defaultExt : "nullptr");
    SEAD_ASSERT(outPath);

    std::vector<std::string> filtersVec;

    if (filterCount > 0)
    {
        SEAD_ASSERT(filters);

        for (u32 i = 0; i < filterCount; i++)
        {
            filtersVec.push_back(filters[i].name);
            filtersVec.push_back(filters[i].filter);
        }
    }

    std::string result = pfd::save_file(title ? title : "", "", filtersVec, pfd::opt::none).result();
    if (result.empty())
    {
        return false;
    }

    LOG_STR(result.c_str());
    outPath->copy(result.c_str());
    return true;
}

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

void InnerFile::drawUI()
{
    {
        static const char* sEndianTypes[] = {
            "Big Endian",
            "Little Endian"
        };

        sead::Endian::Types endian = mEndian;
        if (ImGui::Combo("Byte Order", (s32*)&endian, sEndianTypes, IM_ARRAYSIZE(sEndianTypes)))
        {
            mEndian = endian;
        }
    }

    {
        CenteredTextX("Version");

        u32 version = mVersion;
        if (DrawVersionUI(&version, mFormat == ArchiveFormat::BCSAR ? 4 : 3))
        {
            mVersion = version;
        }
    }
}

Bfsar sBfsar;

//const nw::snd::MemorySoundArchive* sSoundArchive = nullptr;

Item* sSelectedItem = nullptr;
Item* sSubSelectedItem = nullptr;

bool ValidBFSARHeader(const void* file)
{
    bool isFSAR = sead::MemUtil::compare(file, "FSAR", 4) == 0;
    bool isCSAR = sead::MemUtil::compare(file, "CSAR", 4) == 0;

    if (!isFSAR && !isCSAR)
    {
        PopupMgr::instance()->addPopup({ "Selected file is not a valid archive file", nullptr });
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
        if (!(0x00010000 <= header.version && header.version <= 0x00020200))
        {
            sead::FormatFixedSafeString<64> msg("BFSAR version not supported (0x%08X)", (u32)header.version);
            PopupMgr::instance()->addPopup({ msg, nullptr });
            return false;
        }
    }

    return true;
}

bool ValidBCSARHeader(const void* file)
{
    if (sead::MemUtil::compare(file, "CSAR", 4) != 0)
    {
        PopupMgr::instance()->addPopup({ "Selected file is not a valid BCSAR file", nullptr });
        return false;
    }

    const nw::snd::internal::SoundArchiveFile::FileHeader& header = *reinterpret_cast<const nw::snd::internal::SoundArchiveFile::FileHeader*>(file);

    if (!(makeVersion(2, 0, 0) <= header.version && header.version <= makeVersion(2, 3, 2)))
    {
        sead::FormatFixedSafeString<64> msg("BCSAR version not supported (0x%08X)", (u32)header.version);
        PopupMgr::instance()->addPopup({ msg, nullptr });
        return false;
    }

    return true;
}

bool NewFile()
{
    ArchiveFormat prevFormat = sBfsar.getFormat();
    CloseFile();

    sBfsar.create(prevFormat);

    const char* fmtName = prevFormat == ArchiveFormat::BCSAR ? "BCSAR" : "BFSAR";
    LOG_STR(sead::FormatFixedSafeString<64>("Creating new %s file", fmtName).cstr());
    util::updateTitle(sead::FormatFixedSafeString<64>("*New.%s", prevFormat == ArchiveFormat::BCSAR ? "bcsar" : "bfsar").cstr());

    return true;
}

bool OpenFile()
{
    sead::FixedSafeString<512> filePath;

    if (!sDroppedFilePath.isEmpty())
    {
        filePath = sDroppedFilePath;
        sDroppedFilePath.clear();
    }
    else
    {
        const u32 filterCount = 1;
        FileFilter filters[filterCount] = {
            { "Sound Archive (*.bfsar, *.bcsar)", "*.bfsar *.bcsar" }
        };

        if (!OpenFileDialog(&filePath, nullptr, filterCount, filters))
        {
            return false;
        }
    }

    sead::FileDevice* device = sead::FileDeviceMgr::instance()->findDevice("native");
    SEAD_ASSERT(device);

    LOG_STR(filePath.cstr());
    sead::FileDevice::LoadArg arg;
    arg.path = filePath;

    u8* bfsarFile = device->tryLoad(arg);
    if (!bfsarFile)
    {
        PopupMgr::instance()->addPopup({ "Couldn't open the selected file" });
        return false;
    }

    if (!ValidBFSARHeader(bfsarFile))
    {
        return false;
    }

    CloseFile();

    if (!sBfsar.open(bfsarFile, filePath, nullptr)) //? bfsarFile is freed here
    {
        sead::FormatFixedSafeString<1024> msg(
            "Your BFSAR file is corrupted beyond repair :(\n%s", PopupMgr::instance()->getCorruptInfo().cstr()
        );
        PopupMgr::instance()->addPopup({ msg, nullptr });
        CloseFile();
        return false;
    }

    //sSoundArchive = sBfsar.getSoundArchive();

    sead::FixedSafeString<512> fileName;
    sead::Path::getFileName(&fileName, filePath);

    util::updateTitle(fileName.cstr());

    return true;
}

bool SaveFile()
{
    LOG_BOOL("isOpen", sBfsar.isOpen());
    if (sBfsar.isOpen())
    {
        return sBfsar.save();
    }

    return false;
}

bool SaveFileAs()
{
    if (!sBfsar.isOpen())
    {
        return false;
    }

    if (!sBfsar.validate_())
    {
        return false;
    }

    sead::FixedSafeString<512> path;

    const u32 filterCount = 2;
    FileFilter filters[filterCount] = {
        { "Cafe Sound Archive (*.bfsar)", "*.bfsar" },
        { "CTR Sound Archive (*.bcsar)", "*.bcsar" }
    };

    if (SaveFileDialog(&path, nullptr, filterCount, filters, sBfsar.getFormat() == ArchiveFormat::BCSAR ? "bcsar" : "bfsar"))
    {
        LOG_STR(path.cstr());
        if (sBfsar.saveAs(path))
        {
            sead::FixedSafeString<512> fileName;
            sead::Path::getFileName(&fileName, path);

            util::updateTitle(fileName.cstr());

            return true;
        }
    }

    return false;
}

bool CloseFile()
{
    PopupMgr::instance()->closeFile();

    sSelectedItem = nullptr;
    sSubSelectedItem = nullptr;
    sSelectedItemIsSubWindow = false;
    sFileWindows.clear();
    CloseFilter();

    sSoundPlayer.reset();

    sBfsar.close();

    util::updateTitle(nullptr);

    return true;
}

bool Exit()
{
    CloseFile();

    sead::GameFrameworkBaseGlfw* fw = sead::DynamicCast<sead::GameFrameworkBaseGlfw>(util::getFramework());
    SEAD_ASSERT(fw);
    fw->requestExit();

    return true;
}

bool CheckBlockCorrupt(const char* fileName, const char* blockName, const void* block)
{
    if (sead::MemUtil::compare(block, blockName, 4) != 0)
    {
        PopupMgr::instance()->setCorruptInfo(sead::FormatFixedSafeString<64>("%s: %s block not found", fileName, blockName));
        return false;
    }

    return true;
}

bool CheckBlockCorruptError(const char* fileName, const char* blockName, const void* block)
{
    if (sead::MemUtil::compare(block, blockName, 4) != 0)
    {
        PopupMgr::instance()->pushCurrentItemError(sead::FormatFixedSafeString<64>("%s: %s block not found", fileName, blockName));
        return false;
    }

    return true;
}
