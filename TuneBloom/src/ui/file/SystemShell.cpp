#include <ui/file/SystemShell.h>

#include <core/TestHooks.h>

#include <basis/seadTypes.h>

#if defined(SEAD_PLATFORM_WINDOWS)
#include <basis/win/seadWindows.h>
#include <shellapi.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <system_error>

void ShowInFileManager(const std::string& path)
{
    if (testhooks::IsEnabled())
        return;

#if defined(SEAD_PLATFORM_WINDOWS)
    if (path.find('"') != std::string::npos)
        return;

    std::string file = path;
    std::replace(file.begin(), file.end(), '/', '\\');

    const std::string args = "/select,\"" + file + "\"";
    ShellExecuteA(nullptr, "open", "explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
#else
    if (path.find('\'') != std::string::npos)
        return;

#if defined(SEAD_PLATFORM_MACOSX)
    const std::string command = "open -R '" + path + "'";
#else
    const size_t separator = path.find_last_of("/\\");
    const std::string folder = separator == std::string::npos ? std::string() : path.substr(0, separator);
    const std::string command = "xdg-open '" + folder + "'";
#endif
    system(command.c_str());
#endif
}

void OpenURL(const char* url)
{
    if (testhooks::IsEnabled())
        return;

#if defined(SEAD_PLATFORM_WINDOWS)
    ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(SEAD_PLATFORM_LINUX)
    std::string command = "xdg-open \"" + std::string(url) + "\"";
    system(command.c_str());
#elif defined(SEAD_PLATFORM_MACOSX)
    std::string command = "open \"" + std::string(url) + "\"";
    system(command.c_str());
#else
    #error "Unsupported platform"
#endif
}

std::filesystem::path GetExecutablePath()
{
#if defined(SEAD_PLATFORM_WINDOWS)
    wchar_t path[MAX_PATH * 4];
    const DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));

    if (length == 0 || length >= std::size(path))
        return std::filesystem::path();

    return std::filesystem::path(path);
#elif defined(SEAD_PLATFORM_LINUX)
    std::error_code ec;
    return std::filesystem::read_symlink("/proc/self/exe", ec);
#else
    return std::filesystem::path();
#endif
}

void UseExecutableFolder()
{
    static constexpr const char* cContentFolder = "content";

    std::error_code ec;

    if (std::filesystem::is_directory(cContentFolder, ec))
        return;

    const std::filesystem::path folder = GetExecutablePath().parent_path();

    if (folder.empty() || !std::filesystem::is_directory(folder / cContentFolder, ec))
        return;

    std::filesystem::current_path(folder, ec);
}
