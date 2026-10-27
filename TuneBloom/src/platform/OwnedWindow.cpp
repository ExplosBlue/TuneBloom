#include <platform/OwnedWindow.h>

#if defined(SEAD_PLATFORM_MACOSX)

#elif defined(SEAD_PLATFORM_WINDOWS)

#include <basis/win/seadWindows.h>

namespace ownedwindow {

void prepare(void*)
{
}

void attach(void* window, void* owner)
{
    if (!window || !owner)
        return;

    SetWindowLongPtrW(static_cast<HWND>(window), GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
}

void detach(void*)
{
}

}

#else

namespace ownedwindow {

void prepare(void*)
{
}

void attach(void*, void*)
{
}

void detach(void*)
{
}

}

#endif
