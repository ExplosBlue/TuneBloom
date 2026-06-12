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
#include <algorithm>

#include <bfsar/InnerFile.h>

#if defined(SEAD_PLATFORM_WINDOWS)
#include <basis/win/seadWindows.h>
#include <commdlg.h>
#include <GLFW/glfw3native.h>
#else
#include <portable-file-dialogs.h>
#endif

#if defined(SEAD_PLATFORM_WINDOWS)
static std::wstring Utf8ToWide(const char* str)
{
    if (!str || !*str) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, str, -1, nullptr, 0);
    std::wstring ret(static_cast<size_t>(len) - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str, -1, &ret[0], len);
    return ret;
}

static std::string WideToUtf8(const wchar_t* str)
{
    if (!str || !*str) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, str, -1, nullptr, 0, nullptr, nullptr);
    std::string ret(static_cast<size_t>(len) - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, str, -1, &ret[0], len, nullptr, nullptr);
    return ret;
}

static HWND GetWindowHandle()
{
    sead::GameFrameworkBaseGlfw* fw = sead::DynamicCast<sead::GameFrameworkBaseGlfw>(util::getFramework());
    if (!fw) return nullptr;
    GLFWwindow* glfwWindow = fw->getWindowHandle();
    if (!glfwWindow) return nullptr;
    return glfwGetWin32Window(glfwWindow);
}
#endif

static void AddToRecentFiles(const char* path)
{
    const size_t maxRecent = 10;

    auto it = std::find(GetRecentFiles().begin(), GetRecentFiles().end(), path);
    if (it != GetRecentFiles().end())
        GetRecentFiles().erase(it);

    GetRecentFiles().insert(GetRecentFiles().begin(), path);

    if (GetRecentFiles().size() > maxRecent)
        GetRecentFiles().resize(maxRecent);

    SaveRecentFiles();
}

bool OpenFileDialog(sead::BufferedSafeString* outPath, const char* title, u32 filterCount, FileFilter* filters)
{
    LOG_FMT("title=\"%s\" filterCount=%u", title ? title : "nullptr", filterCount);
    SEAD_ASSERT(outPath);

#if defined(SEAD_PLATFORM_WINDOWS)
    std::wstring filterStr;
    for (u32 i = 0; i < filterCount; i++)
    {
        auto nameW = Utf8ToWide(filters[i].name);
        filterStr.append(nameW.begin(), nameW.end());
        filterStr.push_back(L'\0');

        std::string pattern = filters[i].filter;
        for (char& c : pattern)
            if (c == ' ') c = ';';
        auto patternW = Utf8ToWide(pattern.c_str());
        filterStr.append(patternW.begin(), patternW.end());
        filterStr.push_back(L'\0');
    }
    filterStr += L"All Files (*.*)";
    filterStr.push_back(L'\0');
    filterStr += L"*.*";
    filterStr.push_back(L'\0');
    filterStr.push_back(L'\0');

    wchar_t path[4096] = {};
    auto titleW = title ? Utf8ToWide(title) : std::wstring();

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetWindowHandle();
    ofn.lpstrFilter = filterStr.c_str();
    ofn.lpstrFile = path;
    ofn.nMaxFile = 4096;
    ofn.lpstrTitle = titleW.empty() ? nullptr : titleW.c_str();
    ofn.Flags = OFN_NOCHANGEDIR | OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (!GetOpenFileNameW(&ofn))
        return false;

    LOG_STR(WideToUtf8(path).c_str());
    outPath->copy(WideToUtf8(path).c_str());
    return true;

#else
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
#endif
}

bool SaveFileDialog(sead::BufferedSafeString* outPath, const char* title, u32 filterCount, FileFilter* filters, const char* defaultExt, const char* defaultName)
{
    LOG_FMT("title=\"%s\" filterCount=%u defaultExt=\"%s\" defaultName=\"%s\"", title ? title : "nullptr", filterCount, defaultExt ? defaultExt : "nullptr", defaultName ? defaultName : "nullptr");
    SEAD_ASSERT(outPath);

#if defined(SEAD_PLATFORM_WINDOWS)
    std::wstring filterStr;
    if (filterCount > 0)
    {
        SEAD_ASSERT(filters);

        for (u32 i = 0; i < filterCount; i++)
        {
            auto nameW = Utf8ToWide(filters[i].name);
            filterStr.append(nameW.begin(), nameW.end());
            filterStr.push_back(L'\0');

            std::string pattern = filters[i].filter;
            for (char& c : pattern)
                if (c == ' ') c = ';';
            auto patternW = Utf8ToWide(pattern.c_str());
            filterStr.append(patternW.begin(), patternW.end());
            filterStr.push_back(L'\0');
        }
    }
    filterStr.push_back(L'\0');

    wchar_t path[4096] = {};
    if (defaultName && *defaultName)
    {
        auto defaultNameW = Utf8ToWide(defaultName);
        wcscpy_s(path, defaultNameW.c_str());
    }

    auto titleW = title ? Utf8ToWide(title) : std::wstring();
    auto defaultExtW = defaultExt ? Utf8ToWide(defaultExt) : std::wstring();

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetWindowHandle();
    ofn.lpstrFilter = filterStr.c_str();
    ofn.lpstrFile = path;
    ofn.nMaxFile = 4096;
    ofn.lpstrTitle = titleW.empty() ? nullptr : titleW.c_str();
    ofn.lpstrDefExt = defaultExtW.empty() ? nullptr : defaultExtW.c_str();
    ofn.Flags = OFN_NOCHANGEDIR | OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

    if (!GetSaveFileNameW(&ofn))
        return false;

    LOG_STR(WideToUtf8(path).c_str());
    outPath->copy(WideToUtf8(path).c_str());
    return true;

#else
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

    std::string result = pfd::save_file(title ? title : "", defaultName ? defaultName : "", filtersVec, pfd::opt::none).result();
    if (result.empty())
    {
        return false;
    }

    LOG_STR(result.c_str());
    outPath->copy(result.c_str());
    return true;
#endif
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
            SetUnsavedChanges(true);
        }
    }

    {
        CenteredTextX("Version");

        u32 version = mVersion;
        if (DrawVersionUI(&version, mFormat == ArchiveFormat::BCSAR ? 4 : 3))
        {
            mVersion = version;
            SetUnsavedChanges(true);
        }
    }
}

Bfsar sBfsar;

//const nw::snd::MemorySoundArchive* sSoundArchive = nullptr;

Item* sSelectedItemArr[(size_t)UIType::Max + 1] = {};
Item* sSubSelectedItemArr[(size_t)UIType::Max + 1] = {};
bool sSelectedItemIsSubWindowArr[(size_t)UIType::Max + 1] = {};

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

bool NewFile(ArchiveFormat format)
{
    CloseFile();

    sBfsar.create(format);

    const char* fmtName = format == ArchiveFormat::BCSAR ? "BCSAR" : "BFSAR";
    LOG_STR(sead::FormatFixedSafeString<64>("Creating new %s file", fmtName).cstr());
    util::updateTitle(sead::FormatFixedSafeString<64>("New.%s", format == ArchiveFormat::BCSAR ? "bcsar" : "bfsar").cstr(), true);

    SetUnsavedChanges(true);

    SetUITab(UIType::ProjectInfo);
    return true;
}

bool NewFile()
{
    return NewFile(sBfsar.getFormat());
}

bool OpenFile(const char* path)
{
    sRecentFileClick = path;
    return OpenFile();
}

bool OpenFile()
{
    sead::FixedSafeString<512> filePath;

    if (!sRecentFileClick.isEmpty())
    {
        filePath = sRecentFileClick;
        sRecentFileClick.clear();
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

    if (!sBfsar.open(bfsarFile, static_cast<u32>(arg.read_size), filePath, nullptr)) //? bfsarFile is freed here
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

    util::updateTitle(fileName.cstr(), false);

    SetUnsavedChanges(false);

    AddToRecentFiles(filePath.cstr());

    SetUITab(UIType::ProjectInfo);
    return true;
}

bool SaveFile()
{
    LOG_BOOL("isOpen", sBfsar.isOpen());
    if (sBfsar.isOpen() && sBfsar.save())
    {
        SetUnsavedChanges(false);
        return true;
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

    bool isBcsar = sBfsar.getFormat() == ArchiveFormat::BCSAR;
    const u32 filterCount = 1;
    FileFilter filters[filterCount] = {
        { isBcsar ? "CTR Sound Archive (*.bcsar)" : "Cafe Sound Archive (*.bfsar)", isBcsar ? "*.bcsar" : "*.bfsar" }
    };

    sead::FixedSafeString<512> defaultPath;
    if (sBfsar.getFilePath().isEmpty())
    {
        std::string cwd = std::filesystem::current_path().string();
        defaultPath.format("%s/%s", cwd.c_str(), isBcsar ? "Untitled.bcsar" : "Untitled.bfsar");
    }
    else
    {
        defaultPath = sBfsar.getFilePath();
    }

    if (SaveFileDialog(&path, nullptr, filterCount, filters, isBcsar ? "bcsar" : "bfsar", defaultPath.cstr()))
    {
        LOG_STR(path.cstr());
        if (sBfsar.saveAs(path))
        {
            SetUnsavedChanges(false);

            sead::FixedSafeString<512> fileName;
            sead::Path::getFileName(&fileName, path);

            util::updateTitle(fileName.cstr(), false);

            return true;
        }
    }

    return false;
}

bool CloseFile()
{
    PopupMgr::instance()->closeFile();

    for (size_t i = 0; i <= (size_t)UIType::Max; i++)
    {
        sSelectedItemArr[i] = nullptr;
        sSubSelectedItemArr[i] = nullptr;
        sSelectedItemIsSubWindowArr[i] = false;
    }
    sFileWindows.clear();
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
