#include <platform/WindowZoom.h>

#if !defined(SEAD_PLATFORM_MACOSX)

#if defined(SEAD_PLATFORM_WINDOWS)
#include <basis/win/seadWindows.h>
#endif

namespace windowzoom
{

#if defined(SEAD_PLATFORM_WINDOWS)

void SetEnabled(void* window, bool isEnabled)
{
    if (!window)
        return;

    HWND hwnd = static_cast<HWND>(window);
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    const LONG_PTR newStyle = isEnabled ? (style | WS_MAXIMIZEBOX) : (style & ~static_cast<LONG_PTR>(WS_MAXIMIZEBOX));

    if (newStyle == style)
        return;

    SetWindowLongPtrW(hwnd, GWL_STYLE, newStyle);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

#else

void SetEnabled(void*, bool)
{
}

#endif

}

#endif
