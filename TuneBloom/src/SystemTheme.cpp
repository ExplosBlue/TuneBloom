#include <theme/SystemTheme.h>

#if defined(SEAD_PLATFORM_MACOSX)

// macOS uses the Obj-C implementation in src/macos/SystemTheme.mm
// This file is still compiled on macOS but the Obj-C TU provides isDark().

#elif defined(SEAD_PLATFORM_WINDOWS)

#include <basis/win/seadWindows.h>

namespace systemtheme {

bool isDark()
{
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return true;

    DWORD value = 1;
    DWORD size = sizeof(value);
    LONG result = RegQueryValueExA(hKey, "AppsUseLightTheme", NULL, NULL, (LPBYTE)&value, &size);
    RegCloseKey(hKey);

    if (result != ERROR_SUCCESS)
        return true;

    return value == 0;
}

}

#else

// Linux / other: no reliable cross-DE detection
namespace systemtheme {

bool isDark()
{
    return true;
}

}

#endif
