#include <platform/NativeMenu.h>

#if !defined(SEAD_PLATFORM_MACOSX)

namespace nativemenu
{

bool IsSupported()
{
    return false;
}

bool IsInstalled()
{
    return false;
}

void Install(const Setup&)
{
}

void Refresh()
{
}

}

#endif
