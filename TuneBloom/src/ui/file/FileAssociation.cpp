#include <ui/file/FileAssociation.h>

#include <bfsar/model/ArchiveFormat.h>
#include <messages/Messages.h>
#include <ui/file/SystemShell.h>
#include <core/TestHooks.h>

#include <basis/seadTypes.h>
#include <prim/seadSafeString.h>

#if defined(SEAD_PLATFORM_WINDOWS)
#include <basis/win/seadWindows.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <cwctype>
#elif defined(SEAD_PLATFORM_LINUX)
#include <app/Utilll.h>
#include <ui/settings/ConfigFile.h>

#include <bfsar/io/FileIo.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#elif defined(SEAD_PLATFORM_MACOSX)
#include <platform/macos/bundle.h>
#include <platform/macos/defaultApp.h>
#endif

#include <cctype>
#include <filesystem>
#include <iterator>
#include <string>

namespace
{

enum class FileTypeName
{
    BfsarArchive,
    BcsarArchive,
    CmpbinFile,
    AudioResource,
    Wave,
    ZstdFile,
    BfstmStream,
    BcstmStream,
    BfwavWave,
    BcwavWave
};

struct FileType
{
    const char* extension;
    FileTypeName name;
};

}

static constexpr FileType cFileTypes[] = {
    { "bfsar",          FileTypeName::BfsarArchive  },
    { "bcsar",          FileTypeName::BcsarArchive  },
    { "bars",           FileTypeName::AudioResource },
    { "bwav",           FileTypeName::Wave          },
    { "bfstm",          FileTypeName::BfstmStream   },
    { "bcstm",          FileTypeName::BcstmStream   },
    { "bfwav",          FileTypeName::BfwavWave     },
    { "bcwav",          FileTypeName::BcwavWave     },
    { cCmpbinExtension, FileTypeName::CmpbinFile    },
    { cZstdExtension,   FileTypeName::ZstdFile      },
};

static std::string ToUpperText(const char* text)
{
    std::string upper = text;

    for (char& character : upper)
        character = static_cast<char>(std::toupper(static_cast<u8>(character)));

    return upper;
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
            text.format(messages::common::cCompressedFileTypeFormat, ToUpperText(cCmpbinExtension).c_str());
            break;

        case FileTypeName::AudioResource:
            text.format(messages::resource::cResourceTypeFormat, GetResourceFormatInfo(ResourceFormat::Bars).displayName);
            break;

        case FileTypeName::Wave:
            text.format(messages::resource::cWaveTypeFormat, GetResourceFormatInfo(ResourceFormat::Bwav).displayName);
            break;

        case FileTypeName::ZstdFile:
            text.format(messages::common::cCompressedFileTypeFormat, ToUpperText(cZstdExtension).c_str());
            break;

        case FileTypeName::BfstmStream:
            text.format(messages::stream::cStreamTypeFormat, GetInnerFileDisplayName(ArchiveFormat::BFSAR, InnerFileKind::Stream));
            break;

        case FileTypeName::BcstmStream:
            text.format(messages::stream::cStreamTypeFormat, GetInnerFileDisplayName(ArchiveFormat::BCSAR, InnerFileKind::Stream));
            break;

        case FileTypeName::BfwavWave:
            text.format(messages::resource::cWaveTypeFormat, GetInnerFileDisplayName(ArchiveFormat::BFSAR, InnerFileKind::Wave));
            break;

        case FileTypeName::BcwavWave:
            text.format(messages::resource::cWaveTypeFormat, GetInnerFileDisplayName(ArchiveFormat::BCSAR, InnerFileKind::Wave));
            break;
    }

    return text.cstr();
}

#if defined(SEAD_PLATFORM_WINDOWS)

static constexpr const wchar_t* cClassesKey    = L"Software\\Classes\\";
static constexpr const wchar_t* cFileExtsKey   = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\";
static constexpr const wchar_t* cProgIdPrefix  = L"TuneBloom.";
static constexpr const wchar_t* cLegacyProgId  = L"TuneBloom.SoundArchive";
static constexpr const wchar_t* cCommandKey    = L"\\shell\\open\\command";
static constexpr const wchar_t* cIconKey       = L"\\DefaultIcon";
static constexpr const wchar_t* cOpenWithKey   = L"\\OpenWithProgids";

static constexpr const wchar_t* cUserChoiceKeys[] = { L"\\UserChoice", L"\\UserChoiceLatest" };

static std::wstring ToWide(const std::string& text)
{
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);

    if (length <= 0)
        return std::wstring();

    std::wstring wide(static_cast<size_t>(length), L'\0');

    if (MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), length) == 0)
        return std::wstring();

    wide.resize(static_cast<size_t>(length - 1));

    return wide;
}

static std::string ToUtf8(const std::wstring& text)
{
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);

    if (length <= 0)
        return std::string();

    std::string utf8(static_cast<size_t>(length), '\0');

    if (WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, utf8.data(), length, nullptr, nullptr) == 0)
        return std::string();

    utf8.resize(static_cast<size_t>(length - 1));

    return utf8;
}

static std::wstring GetExtensionKey(const FileType& type)
{
    return L"." + ToWide(type.extension);
}

static std::wstring GetProgId(const FileType& type)
{
    std::wstring name = ToWide(type.extension);

    if (!name.empty())
        name[0] = static_cast<wchar_t>(std::towupper(name[0]));

    return cProgIdPrefix + name;
}

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

static std::wstring QueryAssociation(ASSOCSTR kind, const std::wstring& extension)
{
    wchar_t text[MAX_PATH * 4];
    DWORD size = static_cast<DWORD>(std::size(text));

    if (AssocQueryStringW(ASSOCF_INIT_IGNOREUNKNOWN, kind, extension.c_str(), nullptr, text, &size) != S_OK)
        return std::wstring();

    return text;
}

static std::wstring GetOpenCommand()
{
    const std::wstring path = GetExecutablePath().wstring();

    if (path.empty() || path.find(L'"') != std::wstring::npos)
        return std::wstring();

    return L"\"" + path + L"\" \"%1\"";
}

static bool RemoveProgId(const std::wstring& progId)
{
    for (const FileType& type : cFileTypes)
    {
        const std::wstring key = GetClassKey(GetExtensionKey(type));

        RegDeleteKeyValueW(HKEY_CURRENT_USER, (key + cOpenWithKey).c_str(), progId.c_str());

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

static bool IsOwnProgId(const FileType& type, const std::wstring& progId)
{
    return progId == GetProgId(type) || progId == cLegacyProgId;
}

static bool ClearUserChoice(const FileType& type, bool onlyOwn)
{
    bool success = true;

    for (const wchar_t* name : cUserChoiceKeys)
    {
        const std::wstring key = cFileExtsKey + GetExtensionKey(type) + name;

        if (onlyOwn && !IsOwnProgId(type, GetKeyText(key, L"ProgId")))
            continue;

        const LSTATUS status = RegDeleteKeyW(HKEY_CURRENT_USER, key.c_str());

        success &= status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
    }

    return success;
}

static void ReadPlatformState(const FileType& type, fileassoc::TypeState* state)
{
    const std::wstring extension = GetExtensionKey(type);
    const std::wstring executable = QueryAssociation(ASSOCSTR_EXECUTABLE, extension);

    state->isOn = !executable.empty() && _wcsicmp(executable.c_str(), GetExecutablePath().wstring().c_str()) == 0;
    state->appName = ToUtf8(QueryAssociation(ASSOCSTR_FRIENDLYAPPNAME, extension));
}

static bool SetPlatformType(const FileType& type, bool on)
{
    const std::wstring progId = GetProgId(type);
    const std::wstring command = GetOpenCommand();

    bool success = RemoveProgId(cLegacyProgId);

    if (!on)
    {
        success &= ClearUserChoice(type, true);
        success &= RemoveProgId(progId);
        NotifyShell();

        return success;
    }

    if (command.empty())
        return false;

    const std::wstring progKey = GetClassKey(progId);
    const std::wstring extensionKey = GetClassKey(GetExtensionKey(type));

    success &= ClearUserChoice(type, false);
    success &= SetKeyText(progKey, nullptr, ToWide(FormatFileTypeName(type.name)));
    success &= SetKeyText(progKey + cIconKey, nullptr, L"\"" + GetExecutablePath().wstring() + L"\",0");
    success &= SetKeyText(progKey + cCommandKey, nullptr, command);
    success &= SetKeyText(extensionKey + cOpenWithKey, progId.c_str(), std::wstring());
    success &= SetKeyText(extensionKey, nullptr, progId);

    NotifyShell();

    return success;
}

bool fileassoc::IsSupported()
{
    return true;
}

#elif defined(SEAD_PLATFORM_LINUX)

static constexpr const char* cDesktopFile      = "tunebloom.desktop";
static constexpr const char* cDesktopSuffix    = ".desktop";
static constexpr const char* cMimeFile         = "tunebloom.xml";
static constexpr const char* cMimePrefix       = "application/x-nintendo-";
static constexpr const char* cExecKey          = "Exec=";
static constexpr const char* cUnsafePathChars  = "\"'`$\\\n";

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

static std::string GetMimeType(const FileType& type)
{
    return cMimePrefix + std::string(type.extension);
}

static std::string GetExecLine()
{
    const std::string path = GetExecutablePath().string();

    if (path.empty() || path.find_first_of(cUnsafePathChars) != std::string::npos)
        return std::string();

    return std::string(cExecKey) + "\"" + path + "\" %f";
}

static std::string ReadText(const std::string& path)
{
    std::vector<u8> data;

    if (!fileio::ReadWholeFile(path.c_str(), &data))
        return std::string();

    return std::string(data.begin(), data.end());
}

static std::string QueryDefaultApp(const FileType& type)
{
    FILE* pipe = popen(("xdg-mime query default " + GetMimeType(type) + " 2>/dev/null").c_str(), "r");

    if (!pipe)
        return std::string();

    char line[256] = {};
    const bool read = fgets(line, sizeof(line), pipe) != nullptr;
    pclose(pipe);

    std::string app = read ? line : "";

    while (!app.empty() && (app.back() == '\n' || app.back() == '\r'))
        app.pop_back();

    return app;
}

static bool IsListed(const std::string& mimeText, const FileType& type)
{
    return mimeText.find("type=\"" + GetMimeType(type) + "\"") != std::string::npos;
}

static bool IsThisCopy(const std::string& dataDir)
{
    const std::string execLine = GetExecLine();

    return !execLine.empty() && ReadText(GetDesktopPath(dataDir)).find("\n" + execLine + "\n") != std::string::npos;
}

static void UpdateDatabases(const std::string& dataDir)
{
    system(("update-mime-database '" + dataDir + "/mime' >/dev/null 2>&1").c_str());
    system(("update-desktop-database '" + dataDir + "/applications' >/dev/null 2>&1").c_str());
}

static bool WriteFiles(const std::string& dataDir, const std::vector<const FileType*>& types)
{
    const std::string folder = GetExecutablePath().parent_path().string();

    std::string mime;
    std::string mimeTypes;

    mime += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    mime += "<mime-info xmlns=\"http://www.freedesktop.org/standards/shared-mime-info\">\n";

    for (const FileType* type : types)
    {
        mime += "  <mime-type type=\"" + GetMimeType(*type) + "\">\n";
        mime += "    <comment>" + FormatFileTypeName(type->name) + "</comment>\n";
        mime += std::string("    <glob pattern=\"*.") + type->extension + "\"/>\n";
        mime += "  </mime-type>\n";
        mimeTypes += GetMimeType(*type) + ";";
    }

    mime += "</mime-info>\n";

    std::string desktop;
    desktop += "[Desktop Entry]\n";
    desktop += "Type=Application\n";
    desktop += "Name=TuneBloom\n";
    desktop += GetExecLine() + "\n";
    desktop += "Path=" + folder + "\n";
    desktop += "Icon=" + folder + "/content/icon.png\n";
    desktop += "MimeType=" + mimeTypes + "\n";
    desktop += "Terminal=false\n";
    desktop += "Categories=AudioVideo;Audio;\n";

    return WriteTextFileAtomic(GetMimePath(dataDir), mime) && WriteTextFileAtomic(GetDesktopPath(dataDir), desktop);
}

static bool RemoveFiles(const std::string& dataDir)
{
    std::error_code ec;
    std::filesystem::remove(GetDesktopPath(dataDir), ec);
    std::filesystem::remove(GetMimePath(dataDir), ec);

    UpdateDatabases(dataDir);

    return !std::filesystem::exists(GetDesktopPath(dataDir), ec);
}

static void ReadPlatformState(const FileType& type, fileassoc::TypeState* state)
{
    const std::string dataDir = GetDataDir();
    std::string app = QueryDefaultApp(type);

    state->isOn = !dataDir.empty() && app == cDesktopFile && IsThisCopy(dataDir) && IsListed(ReadText(GetMimePath(dataDir)), type);

    if (app == cDesktopFile)
        app = util::cAppName.cstr();
    else if (app.size() > strlen(cDesktopSuffix) && app.compare(app.size() - strlen(cDesktopSuffix), std::string::npos, cDesktopSuffix) == 0)
        app.erase(app.size() - strlen(cDesktopSuffix));

    state->appName = app;
}

static bool SetPlatformType(const FileType& changed, bool on)
{
    const std::string dataDir = GetDataDir();

    if (dataDir.empty() || dataDir.find('\'') != std::string::npos || GetExecLine().empty())
        return false;

    const std::string mimeText = IsThisCopy(dataDir) ? ReadText(GetMimePath(dataDir)) : std::string();
    std::vector<const FileType*> listed;

    for (const FileType& type : cFileTypes)
    {
        if (&type == &changed ? on : IsListed(mimeText, type))
            listed.push_back(&type);
    }

    if (listed.empty())
        return RemoveFiles(dataDir);

    if (!WriteFiles(dataDir, listed))
        return false;

    UpdateDatabases(dataDir);

    if (!on)
        return true;

    return system((std::string("xdg-mime default ") + cDesktopFile + " " + GetMimeType(changed) + " >/dev/null 2>&1").c_str()) == 0;
}

bool fileassoc::IsSupported()
{
    return true;
}

#elif defined(SEAD_PLATFORM_MACOSX)

static void ReadPlatformState(const FileType& type, fileassoc::TypeState* state)
{
    state->isOn = macos::isDefaultApp(type.extension);
    state->appName = macos::getDefaultAppName(type.extension);

    if (state->isOn && !macos::hasOtherApp(type.extension))
        state->lock = fileassoc::Lock::OnlyApp;
}

static bool SetPlatformType(const FileType& type, bool on)
{
    return on ? macos::setDefaultToThisApp(type.extension) : macos::setDefaultToOtherApp(type.extension);
}

bool fileassoc::IsSupported()
{
    return macos::isAppBundle();
}

#else

static void ReadPlatformState(const FileType&, fileassoc::TypeState*)
{
}

static bool SetPlatformType(const FileType&, bool)
{
    return false;
}

bool fileassoc::IsSupported()
{
    return false;
}

#endif

u32 fileassoc::GetTypeCount()
{
    return static_cast<u32>(std::size(cFileTypes));
}

fileassoc::TypeState fileassoc::GetTypeState(u32 index)
{
    SEAD_ASSERT(index < GetTypeCount());

    const FileType& type = cFileTypes[index];

    TypeState state;
    state.extension = type.extension;
    state.typeName = FormatFileTypeName(type.name);

    ReadPlatformState(type, &state);

    return state;
}

bool fileassoc::SetTypeOn(u32 index, bool on)
{
    SEAD_ASSERT(index < GetTypeCount());

    if (testhooks::IsEnabled())
        return false;

    return SetPlatformType(cFileTypes[index], on);
}
