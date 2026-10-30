#include <ui/app/NativeMenuBar.h>

#include <core/MenuTree.h>
#include <core/TestHooks.h>
#include <platform/NativeMenu.h>

#include <ui/app/MenuBar.h>
#include <messages/Messages.h>

#include <app/Utilll.h>
#include <heap/seadHeapMgr.h>

static bool sIsBuilt = false;
static bool sWasSlim = false;

static void RecordNativeMenus(MenuTree* tree)
{
    sead::CurrentHeapSetter heapSetter(sead::HeapMgr::getUnboundHeap());

    RecordMenuBar(tree);
}

static bool IsNativeMenuWanted()
{
    return nativemenu::IsSupported() && (!testhooks::IsEnabled() || testhooks::IsNativeMenuOn());
}

static void InstallNativeMenus()
{
    const char* appName = util::cAppName.cstr();
    const sead::FormatFixedSafeString<64> about(messages::mainmenu::cAppAboutFormat, appName);
    const sead::FormatFixedSafeString<64> hide(messages::mainmenu::cAppHideFormat, appName);
    const sead::FormatFixedSafeString<64> quit(messages::mainmenu::cAppQuitFormat, appName);

    nativemenu::Setup setup;
    setup.record = &RecordNativeMenus;
    setup.activate = &RequestMenuActivation;
    setup.texts.appName = appName;
    setup.texts.about = about.cstr();
    setup.texts.settings = messages::mainmenu::cAppSettings;
    setup.texts.services = messages::mainmenu::cAppServices;
    setup.texts.hide = hide.cstr();
    setup.texts.hideOthers = messages::mainmenu::cAppHideOthers;
    setup.texts.showAll = messages::mainmenu::cAppShowAll;
    setup.texts.quit = quit.cstr();
    setup.texts.window = messages::mainmenu::cWindowMenu;
    setup.texts.minimize = messages::common::cMinimize;
    setup.texts.zoom = messages::mainmenu::cWindowZoom;
    setup.texts.bringAllToFront = messages::mainmenu::cBringAllToFront;

    nativemenu::Install(setup);
}

bool IsNativeMenuBar()
{
    return nativemenu::IsInstalled();
}

bool IsTabStripInTitleRow()
{
    return IsNativeMenuBar() && !IsSlimMenuBar();
}

void UpdateNativeMenuBar()
{
    if (!nativemenu::IsInstalled())
    {
        if (!IsNativeMenuWanted())
            return;

        InstallNativeMenus();
    }

    const bool isSlim = IsSlimMenuBar();

    if (sIsBuilt && isSlim == sWasSlim)
        return;

    sIsBuilt = true;
    sWasSlim = isSlim;
    nativemenu::Refresh();
}
