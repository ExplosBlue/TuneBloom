#import <Cocoa/Cocoa.h>
#include <platform/WindowZoom.h>

namespace windowzoom
{

void SetEnabled(void* window, bool isEnabled)
{
    if (!window)
        return;

    NSWindow* nsWindow = static_cast<NSWindow*>(window);
    NSWindowCollectionBehavior behavior = [nsWindow collectionBehavior];

    behavior &= ~(NSWindowCollectionBehaviorFullScreenPrimary | NSWindowCollectionBehaviorFullScreenNone);
    behavior |= isEnabled ? NSWindowCollectionBehaviorFullScreenPrimary : NSWindowCollectionBehaviorFullScreenNone;

    [nsWindow setCollectionBehavior:behavior];
}

}
