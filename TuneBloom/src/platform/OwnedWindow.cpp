#include <platform/OwnedWindow.h>

#if !defined(SEAD_PLATFORM_MACOSX)

#if defined(SEAD_PLATFORM_WINDOWS)
#include <basis/win/seadWindows.h>
#endif

namespace ownedwindow
{

void prepare(void*)
{
}

#if defined(SEAD_PLATFORM_WINDOWS)

void attach(void* window, void* owner)
{
    if (!window || !owner)
        return;

    SetWindowLongPtrW(static_cast<HWND>(window), GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
}

#else

void attach(void*, void*)
{
}

#endif

void detach(void*)
{
}

}

#endif
