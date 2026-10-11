#include <ui/file/FileAssociation.h>

#include <ui/Messages.h>
#include <core/TestHooks.h>
#include <ui/file/SystemShell.h>

#include <basis/seadTypes.h>

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

#if defined(SEAD_PLATFORM_WINDOWS)

namespace
{

struct Extension
{
    const wchar_t* name;
    bool isDefault;
};

}

static constexpr const wchar_t* cClassesKey  = L"Software\\Classes\\";
static constexpr const wchar_t* cProgId      = L"TuneBloom.SoundArchive";
static constexpr const wchar_t* cCommandKey  = L"\\shell\\open\\command";
static constexpr const wchar_t* cIconKey     = L"\\DefaultIcon";
static constexpr const wchar_t* cOpenWithKey = L"\\OpenWithProgids";

static constexpr Extension cExtensions[] = {
    { L".bfsar",  true  },
    { L".bcsar",  true  },
    { L".cmpbin", false },
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

static std::wstring GetTypeName()
{
    wchar_t name[128];

    if (MultiByteToWideChar(CP_UTF8, 0, messages::archive::cFileTypeName, -1, name, static_cast<int>(std::size(name))) == 0)
        return std::wstring();

    return name;
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
    const std::wstring command = GetKeyText(GetClassKey(cProgId) + cCommandKey, nullptr);

    if (command.empty())
        return State::None;

    return _wcsicmp(command.c_str(), GetOpenCommand().c_str()) == 0 ? State::ThisCopy : State::OtherCopy;
}

bool fileassoc::Register()
{
    const std::wstring command = GetOpenCommand();

    if (testhooks::IsEnabled() || command.empty())
        return false;

    const std::wstring progKey = GetClassKey(cProgId);
    const std::wstring path = GetExecutablePath().wstring();

    bool success = SetKeyText(progKey, nullptr, GetTypeName());
    success &= SetKeyText(progKey + cIconKey, nullptr, L"\"" + path + L"\",0");
    success &= SetKeyText(progKey + cCommandKey, nullptr, command);

    for (const Extension& extension : cExtensions)
    {
        const std::wstring key = GetClassKey(extension.name);

        success &= SetKeyText(key + cOpenWithKey, cProgId, std::wstring());

        if (extension.isDefault)
            success &= SetKeyText(key, nullptr, cProgId);
    }

    NotifyShell();

    return success;
}

bool fileassoc::Remove()
{
    if (testhooks::IsEnabled())
        return false;

    for (const Extension& extension : cExtensions)
    {
        const std::wstring key = GetClassKey(extension.name);

        RegDeleteKeyValueW(HKEY_CURRENT_USER, (key + cOpenWithKey).c_str(), cProgId);

        if (extension.isDefault && GetKeyText(key, nullptr) == cProgId)
            RegDeleteKeyValueW(HKEY_CURRENT_USER, key.c_str(), nullptr);
    }

    const LSTATUS status = RegDeleteTreeW(HKEY_CURRENT_USER, GetClassKey(cProgId).c_str());

    NotifyShell();

    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
}

#elif defined(SEAD_PLATFORM_LINUX)

static constexpr const char* cMimeType        = "application/x-nintendo-sound-archive";
static constexpr const char* cDesktopFile     = "tunebloom.desktop";
static constexpr const char* cMimeFile        = "tunebloom.xml";
static constexpr const char* cExecKey         = "Exec=";
static constexpr const char* cUnsafePathChars = "\"'`$\\\n";

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
    mime += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    mime += "<mime-info xmlns=\"http://www.freedesktop.org/standards/shared-mime-info\">\n";
    mime += std::string("  <mime-type type=\"") + cMimeType + "\">\n";
    mime += std::string("    <comment>") + messages::archive::cFileTypeName + "</comment>\n";
    mime += "    <glob pattern=\"*.bfsar\"/>\n";
    mime += "    <glob pattern=\"*.bcsar\"/>\n";
    mime += "  </mime-type>\n";
    mime += "</mime-info>\n";

    std::string desktop;
    desktop += "[Desktop Entry]\n";
    desktop += "Type=Application\n";
    desktop += "Name=TuneBloom\n";
    desktop += execLine + "\n";
    desktop += "Path=" + folder + "\n";
    desktop += "Icon=" + folder + "/content/icon.png\n";
    desktop += std::string("MimeType=") + cMimeType + ";\n";
    desktop += "Terminal=false\n";
    desktop += "Categories=AudioVideo;Audio;\n";

    if (!WriteTextFileAtomic(GetMimePath(dataDir), mime) || !WriteTextFileAtomic(GetDesktopPath(dataDir), desktop))
        return false;

    UpdateDatabases(dataDir);
    system((std::string("xdg-mime default ") + cDesktopFile + " " + cMimeType + " >/dev/null 2>&1").c_str());

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
