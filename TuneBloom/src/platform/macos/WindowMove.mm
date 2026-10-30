#import <Cocoa/Cocoa.h>
#include <platform/WindowMove.h>

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

@interface TuneBloomFrameTimer : NSObject
- (void)drawFrame:(NSTimer*)timer;
@end

namespace windowmove
{

static constexpr NSTimeInterval cFrameInterval = 1.0 / 60.0;

static NSView* sView = nil;
static NSTimer* sTimer = nil;
static TuneBloomFrameTimer* sTimerTarget = nil;

static void StartTimer()
{
    if (sTimer || !sView)
        return;

    sTimer = [[NSTimer timerWithTimeInterval:cFrameInterval target:sTimerTarget selector:@selector(drawFrame:) userInfo:nil repeats:YES] retain];
    [[NSRunLoop currentRunLoop] addTimer:sTimer forMode:NSEventTrackingRunLoopMode];
}

static void StopTimer()
{
    [sTimer invalidate];
    [sTimer release];
    sTimer = nil;
}

static void DrawFrame()
{
    [sView setNeedsDisplay:YES];
}

static void InstallOnMainThread(NSWindow* window)
{
    if (sView)
        return;

    sView = [window.contentView retain];
    sTimerTarget = [[TuneBloomFrameTimer alloc] init];

    NSNotificationCenter* center = [NSNotificationCenter defaultCenter];

    [center addObserverForName:NSMenuDidBeginTrackingNotification object:nil queue:nil usingBlock:^(NSNotification*)
    {
        StartTimer();
    }];

    [center addObserverForName:NSMenuDidEndTrackingNotification object:nil queue:nil usingBlock:^(NSNotification*)
    {
        StopTimer();
    }];
}

void KeepDrawing(GLFWwindow* window)
{
    NSWindow* nsWindow = window ? glfwGetCocoaWindow(window) : nil;

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

}

@implementation TuneBloomFrameTimer

- (void)drawFrame:(NSTimer*)timer
{
    windowmove::DrawFrame();
}

@end
