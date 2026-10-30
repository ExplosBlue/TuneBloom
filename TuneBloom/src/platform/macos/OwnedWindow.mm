#import <Cocoa/Cocoa.h>
#include <platform/OwnedWindow.h>

namespace ownedwindow
{

void prepare(void* window)
{
    if (!window)
        return;

    NSWindow* nsWindow = static_cast<NSWindow*>(window);

    [nsWindow setHasShadow:YES];
    [nsWindow setAnimationBehavior:NSWindowAnimationBehaviorAlertPanel];
}

void attach(void* window, void* owner)
{
    if (!window || !owner)
        return;

    NSWindow* nsWindow = static_cast<NSWindow*>(window);
    NSWindow* nsOwner = static_cast<NSWindow*>(owner);

    if ([nsWindow parentWindow] != nsOwner)
        [nsOwner addChildWindow:nsWindow ordered:NSWindowAbove];
}

void detach(void* window)
{
    if (!window)
        return;

    NSWindow* nsWindow = static_cast<NSWindow*>(window);

    if (NSWindow* parent = [nsWindow parentWindow])
        [parent removeChildWindow:nsWindow];
}

}
