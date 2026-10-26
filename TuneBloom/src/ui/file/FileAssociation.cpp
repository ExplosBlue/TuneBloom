#include <ui/file/FileAssociation.h>

#include <bfsar/model/ArchiveFormat.h>
#include <ui/Messages.h>
#include <ui/file/SystemShell.h>
#include <core/TestHooks.h>

#include <basis/seadTypes.h>
#include <prim/seadSafeString.h>

#if defined(SEAD_PLATFORM_WINDOWS)
#include <basis/win/seadWindows.h>
#include <shlobj.h>
#elif defined(SEAD_PLATFORM_LINUX)
#include <ui/settings/ConfigFile.h>

#include <bfsar/io/FileIo.h>

#include <cstdlib>
#include <vector>
#endif

#include <filesystem>
#include <iterator>
#include <string>

#if defined(SEAD_PLATFORM_WINDOWS) || defined(SEAD_PLATFORM_LINUX)

namespace
{

enum class FileTypeName
{
    BfsarArchive,
    BcsarArchive,
    CmpbinFile,
    AudioResource,
    CompressedAudioResource,
    Wave,
    ZstdFile
};

}

static std::string FormatFileTypeName(FileTypeName name)
{
    sead::FixedSafeString<128> text;

    switch (name)
    {
        case FileTypeName::BfsarArchive:
            text.format(messages::archive::cSoundArchiveTypeFormat, GetInnerFileDisplayName(ArchiveFormat::BFSAR, InnerFileKind::SoundArchive));
            break;

        case FileTypeName::BcsarArchive:
            text.format(messages::archive::cSoundArchiveTypeFormat, GetInnerFileDisplayName(ArchiveFormat::BCSAR, InnerFileKind::SoundArchive));
            break;

        case FileTypeName::CmpbinFile:
            text.format(messages::common::cCompressedFileTypeFormat, cCmpbinExtension);
            break;

        case FileTypeName::AudioResource:
            text.format(messages::resource::cResourceTypeFormat, GetResourceFormatInfo(ResourceFormat::Bars).displayName);
            break;

        case FileTypeName::CompressedAudioResource:
            text.format(messages::resource::cCompressedResourceTypeFormat, GetResourceFormatInfo(ResourceFormat::Bars).displayName);
            break;

        case FileTypeName::Wave:
            text.format(messages::resource::cWaveTypeFormat, GetResourceFormatInfo(ResourceFormat::Bwav).displayName);
            break;

        case FileTypeName::ZstdFile:
            text.format(messages::common::cCompressedFileTypeFormat, cZstdExtension);
            break;
    }

    return text.cstr();
}

#endif

#if defined(SEAD_PLATFORM_WINDOWS)

namespace
{

struct FileType
{
    const wchar_t* extension;
    const wchar_t* progId;
    FileTypeName name;
    bool isDefault;
};

}

static constexpr const wchar_t* cClassesKey   = L"Software\\Classes\\";
static constexpr const wchar_t* cLegacyProgId = L"TuneBloom.SoundArchive";
static constexpr const wchar_t* cCommandKey   = L"\\shell\\open\\command";
static constexpr const wchar_t* cIconKey      = L"\\DefaultIcon";
static constexpr const wchar_t* cOpenWithKey  = L"\\OpenWithProgids";

static constexpr FileType cFileTypes[] = {
    { L".bfsar",  L"TuneBloom.Bfsar",  FileTypeName::BfsarArchive,  true  },
    { L".bcsar",  L"TuneBloom.Bcsar",  FileTypeName::BcsarArchive,  true  },
    { L".cmpbin", L"TuneBloom.Cmpbin", FileTypeName::CmpbinFile,    false },
    { L".bars",   L"TuneBloom.Bars",   FileTypeName::AudioResource, false },
    { L".bwav",   L"TuneBloom.Bwav",   FileTypeName::Wave,          false },
    { L".zs",     L"TuneBloom.Zs",     FileTypeName::ZstdFile,      false },
};

static std::wstring GetClassKey(const std::wstring& name)
{
    return std::wstring(cClassesKey) + name;
}

static std::wstring GetKeyText(const std::wstring& key, const wchar_t* name)
{
    wchar_t text[MAX_PATH * 4 + 16];
    DWORD size = sizeof(text);

    if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), name, RRF_RT_REG_SZ, nullptr, text, &size) != ERROR_SUCCESS)
        return std::wstring();

    return text;
}

static bool SetKeyText(const std::wstring& key, const wchar_t* name, const std::wstring& text)
{
    const DWORD size = static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t));

    return RegSetKeyValueW(HKEY_CURRENT_USER, key.c_str(), name, REG_SZ, text.c_str(), size) == ERROR_SUCCESS;
}

static std::wstring GetOpenCommand()
{
    const std::wstring path = GetExecutablePath().wstring();

    if (path.empty() || path.find(L'"') != std::wstring::npos)
        return std::wstring();

    return L"\"" + path + L"\" \"%1\"";
}

static std::wstring ToWide(const std::string& text)
{
    wchar_t wide[128];

    if (MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide, static_cast<int>(std::size(wide))) == 0)
        return std::wstring();

    return wide;
}

static bool RemoveProgId(const wchar_t* progId)
{
    for (const FileType& type : cFileTypes)
    {
        const std::wstring key = GetClassKey(type.extension);

        RegDeleteKeyValueW(HKEY_CURRENT_USER, (key + cOpenWithKey).c_str(), progId);

        if (GetKeyText(key, nullptr) == progId)
            RegDeleteKeyValueW(HKEY_CURRENT_USER, key.c_str(), nullptr);
    }

    const LSTATUS status = RegDeleteTreeW(HKEY_CURRENT_USER, GetClassKey(progId).c_str());

    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
}

static void NotifyShell()
{
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

bool fileassoc::IsSupported()
{
    return true;
}

fileassoc::State fileassoc::GetState()
{
    const std::wstring command = GetKeyText(GetClassKey(cFileTypes[0].progId) + cCommandKey, nullptr);

    if (command.empty())
        return GetKeyText(GetClassKey(cLegacyProgId) + cCommandKey, nullptr).empty() ? State::None : State::OtherCopy;

    return _wcsicmp(command.c_str(), GetOpenCommand().c_str()) == 0 ? State::ThisCopy : State::OtherCopy;
}

bool fileassoc::Register()
{
    const std::wstring command = GetOpenCommand();

    if (testhooks::IsEnabled() || command.empty())
        return false;

    const std::wstring icon = L"\"" + GetExecutablePath().wstring() + L"\",0";
    bool success = RemoveProgId(cLegacyProgId);

    for (const FileType& type : cFileTypes)
    {
        const std::wstring progKey = GetClassKey(type.progId);
        const std::wstring extensionKey = GetClassKey(type.extension);

        success &= SetKeyText(progKey, nullptr, ToWide(FormatFileTypeName(type.name)));
        success &= SetKeyText(progKey + cIconKey, nullptr, icon);
        success &= SetKeyText(progKey + cCommandKey, nullptr, command);
        success &= SetKeyText(extensionKey + cOpenWithKey, type.progId, std::wstring());

        if (type.isDefault)
            success &= SetKeyText(extensionKey, nullptr, type.progId);
    }

    NotifyShell();

    return success;
}

bool fileassoc::Remove()
{
    if (testhooks::IsEnabled())
        return false;

    bool success = RemoveProgId(cLegacyProgId);

    for (const FileType& type : cFileTypes)
        success &= RemoveProgId(type.progId);

    NotifyShell();

    return success;
}

#elif defined(SEAD_PLATFORM_LINUX)

namespace
{

struct MimeType
{
    const char* type;
    const char* glob;
    FileTypeName name;
    bool isDefault;
};

}

static constexpr const char* cDesktopFile      = "tunebloom.desktop";
static constexpr const char* cMimeFile         = "tunebloom.xml";
static constexpr const char* cExecKey          = "Exec=";
static constexpr const char* cUnsafePathChars  = "\"'`$\\\n";

static constexpr MimeType cMimeTypes[] = {
    { "application/x-nintendo-bfsar",     "*.bfsar",   FileTypeName::BfsarArchive,            true  },
    { "application/x-nintendo-bcsar",     "*.bcsar",   FileTypeName::BcsarArchive,            true  },
    { "application/x-nintendo-bars",      "*.bars",    FileTypeName::AudioResource,           false },
    { "application/x-nintendo-bars-zstd", "*.bars.zs", FileTypeName::CompressedAudioResource, false },
    { "application/x-nintendo-bwav",      "*.bwav",    FileTypeName::Wave,                    false },
};

static std::string GetDataDir()
{
    const char* xdg = getenv("XDG_DATA_HOME");
    if (xdg && xdg[0])
        return xdg;

    const char* home = getenv("HOME");
    if (home && home[0])
        return std::string(home) + "/.local/share";

    return std::string();
}

static std::string GetDesktopPath(const std::string& dataDir)
{
    return dataDir + "/applications/" + cDesktopFile;
}

static std::string GetMimePath(const std::string& dataDir)
{
    return dataDir + "/mime/packages/" + cMimeFile;
}

static std::string GetExecLine()
{
    const std::string path = GetExecutablePath().string();

    if (path.empty() || path.find_first_of(cUnsafePathChars) != std::string::npos)
        return std::string();

    return std::string(cExecKey) + "\"" + path + "\" %f";
}

static void UpdateDatabases(const std::string& dataDir)
{
    system(("update-mime-database '" + dataDir + "/mime' >/dev/null 2>&1").c_str());
    system(("update-desktop-database '" + dataDir + "/applications' >/dev/null 2>&1").c_str());
}

bool fileassoc::IsSupported()
{
    return true;
}

fileassoc::State fileassoc::GetState()
{
    const std::string dataDir = GetDataDir();
    std::vector<u8> data;

    if (dataDir.empty() || !fileio::ReadWholeFile(GetDesktopPath(dataDir).c_str(), &data))
        return State::None;

    const std::string text(data.begin(), data.end());
    const std::string execLine = GetExecLine();

    if (!execLine.empty() && text.find("\n" + execLine + "\n") != std::string::npos)
        return State::ThisCopy;

    return State::OtherCopy;
}

bool fileassoc::Register()
{
    const std::string dataDir = GetDataDir();
    const std::string execLine = GetExecLine();

    if (testhooks::IsEnabled() || dataDir.empty() || dataDir.find('\'') != std::string::npos || execLine.empty())
        return false;

    const std::string folder = GetExecutablePath().parent_path().string();

    std::string mime;
    std::string mimeTypes;

    mime += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    mime += "<mime-info xmlns=\"http://www.freedesktop.org/standards/shared-mime-info\">\n";

    for (const MimeType& type : cMimeTypes)
    {
        mime += std::string("  <mime-type type=\"") + type.type + "\">\n";
        mime += "    <comment>" + FormatFileTypeName(type.name) + "</comment>\n";
        mime += std::string("    <glob pattern=\"") + type.glob + "\"/>\n";
        mime += "  </mime-type>\n";
        mimeTypes += std::string(type.type) + ";";
    }

    mime += "</mime-info>\n";

    std::string desktop;
    desktop += "[Desktop Entry]\n";
    desktop += "Type=Application\n";
    desktop += "Name=TuneBloom\n";
    desktop += execLine + "\n";
    desktop += "Path=" + folder + "\n";
    desktop += "Icon=" + folder + "/content/icon.png\n";
    desktop += "MimeType=" + mimeTypes + "\n";
    desktop += "Terminal=false\n";
    desktop += "Categories=AudioVideo;Audio;\n";

    if (!WriteTextFileAtomic(GetMimePath(dataDir), mime) || !WriteTextFileAtomic(GetDesktopPath(dataDir), desktop))
        return false;

    UpdateDatabases(dataDir);

    for (const MimeType& type : cMimeTypes)
    {
        if (type.isDefault)
            system((std::string("xdg-mime default ") + cDesktopFile + " " + type.type + " >/dev/null 2>&1").c_str());
    }

    return true;
}

bool fileassoc::Remove()
{
    const std::string dataDir = GetDataDir();

    if (testhooks::IsEnabled() || dataDir.empty() || dataDir.find('\'') != std::string::npos)
        return false;

    std::error_code ec;
    std::filesystem::remove(GetDesktopPath(dataDir), ec);
    std::filesystem::remove(GetMimePath(dataDir), ec);

    UpdateDatabases(dataDir);

    return !std::filesystem::exists(GetDesktopPath(dataDir), ec);
}

#else

bool fileassoc::IsSupported()
{
    return false;
}

fileassoc::State fileassoc::GetState()
{
    return State::None;
}

bool fileassoc::Register()
{
    return false;
}

bool fileassoc::Remove()
{
    return false;
}

#endif
