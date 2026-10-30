#import <Cocoa/Cocoa.h>
#include <platform/WindowFrame.h>

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

namespace windowframe
{

static NSWindow* sWindow = nil;
static CaptionArea sCaptionArea;

static NSWindow* GetNativeWindow(GLFWwindow* window)
{
    return window ? glfwGetCocoaWindow(window) : nil;
}

static bool IsFullScreen(NSWindow* window)
{
    return (window.styleMask & NSWindowStyleMaskFullScreen) != 0;
}

static bool CanZoom(NSWindow* window)
{
    return (window.collectionBehavior & NSWindowCollectionBehaviorFullScreenNone) == 0;
}

static f32 CalcNativeTitleBarHeight(NSWindow* window)
{
    return static_cast<f32>(window.frame.size.height - NSMaxY(window.contentLayoutRect));
}

static void RunDoubleClickAction(NSWindow* window)
{
    NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
    NSString* action = [defaults stringForKey:@"AppleActionOnDoubleClick"];

    if (!action)
        action = [defaults boolForKey:@"AppleMiniaturizeOnDoubleClick"] ? @"Minimize" : @"Maximize";

    if ([action isEqualToString:@"Minimize"])
        [window miniaturize:nil];
    else if (![action isEqualToString:@"None"] && CanZoom(window))
        [window zoom:nil];
}

static NSEvent* OnMouseDown(NSEvent* event)
{
    NSWindow* window = event.window;

    if (!window || window != sWindow)
        return event;

    NSView* contentView = window.contentView;
    const NSPoint location = event.locationInWindow;
    NSView* hit = [contentView.superview hitTest:location];

    if (!hit || ![hit isDescendantOf:contentView])
        return event;

    const s32 x = static_cast<s32>(location.x);
    const s32 y = static_cast<s32>(contentView.frame.size.height - location.y);

    if (!IsDragPoint(sCaptionArea, x, y))
        return event;

    if (event.clickCount == 2)
        RunDoubleClickAction(window);
    else
        [window performWindowDragWithEvent:event];

    return nil;
}

static void ObserveFullScreen(NSWindow* window)
{
    NSNotificationCenter* center = [NSNotificationCenter defaultCenter];

    [center addObserverForName:NSWindowWillEnterFullScreenNotification object:window queue:nil usingBlock:^(NSNotification*)
    {
        window.toolbar.visible = NO;
    }];

    [center addObserverForName:NSWindowWillExitFullScreenNotification object:window queue:nil usingBlock:^(NSNotification*)
    {
        window.toolbar.visible = YES;
    }];
}

static void InstallOnMainThread(NSWindow* window)
{
    if (sWindow)
        return;

    window.styleMask |= NSWindowStyleMaskFullSizeContentView;
    window.titlebarAppearsTransparent = YES;
    window.titleVisibility = NSWindowTitleHidden;

    NSToolbar* toolbar = [[NSToolbar alloc] initWithIdentifier:@"TuneBloomTitleBar"];
    window.toolbar = toolbar;
    [toolbar release];
    window.toolbarStyle = NSWindowToolbarStyleUnifiedCompact;
    ObserveFullScreen(window);

    [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskLeftMouseDown handler:^NSEvent*(NSEvent* event)
    {
        return OnMouseDown(event);
    }];

    sWindow = window;
}

void Install(GLFWwindow* window)
{
    NSWindow* nsWindow = GetNativeWindow(window);

    if (!nsWindow)
        return;

    if ([NSThread isMainThread])
    {
        InstallOnMainThread(nsWindow);
        return;
    }

    dispatch_async(dispatch_get_main_queue(), ^
    {
        InstallOnMainThread(nsWindow);
    });
}

bool IsCustom()
{
    return sWindow != nil;
}

void SetDark(GLFWwindow* window, bool isDark)
{
    NSWindow* nsWindow = GetNativeWindow(window);

    if (!nsWindow)
        return;

    [nsWindow setAppearance:[NSAppearance appearanceNamed:isDark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua]];
}

void SetRoundCorners(GLFWwindow*, u32)
{
}

void SetCaptionArea(const CaptionArea& area)
{
    sCaptionArea = area;
}

ButtonState GetButtonState(WindowButton)
{
    return ButtonState::Idle;
}

bool HasDrawnButtons()
{
    return false;
}

f32 GetTitleBarInsetLeft()
{
    if (!sWindow || IsFullScreen(sWindow))
        return 0.0f;

    NSButton* close = [sWindow standardWindowButton:NSWindowCloseButton];
    NSButton* zoom = [sWindow standardWindowButton:NSWindowZoomButton];

    if (!close || !zoom)
        return 0.0f;

    const NSRect closeRect = [close convertRect:close.bounds toView:nil];
    const NSRect zoomRect = [zoom convertRect:zoom.bounds toView:nil];

    return static_cast<f32>(NSMaxX(zoomRect) + NSMinX(closeRect));
}

f32 FitTitleBarHeight(f32 minHeight)
{
    if (!sWindow || IsFullScreen(sWindow))
        return 0.0f;

    if (CalcNativeTitleBarHeight(sWindow) < minHeight && sWindow.toolbarStyle == NSWindowToolbarStyleUnifiedCompact)
        sWindow.toolbarStyle = NSWindowToolbarStyleUnified;

    return CalcNativeTitleBarHeight(sWindow);
}

bool IsWindowMaximized(GLFWwindow* window)
{
    return window && glfwGetWindowAttrib(window, GLFW_MAXIMIZED);
}

bool CanMaximize(GLFWwindow* window)
{
    NSWindow* nsWindow = GetNativeWindow(window);

    return nsWindow && CanZoom(nsWindow);
}

void SetSize(GLFWwindow* window, s32 width, s32 height)
{
    glfwSetWindowSize(window, width, height);
}

void SetSizeLimits(GLFWwindow* window, s32 minWidth, s32 minHeight, s32 maxWidth, s32 maxHeight)
{
    glfwSetWindowSizeLimits(window, minWidth, minHeight, maxWidth, maxHeight);
}

}
