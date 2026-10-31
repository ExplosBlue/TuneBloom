#import <Cocoa/Cocoa.h>
#include <platform/macos/bundle.h>

#include <unistd.h>

namespace macos {

bool isAppBundle()
{
    @autoreleasepool
    {
        NSString* executablePath = [[NSBundle mainBundle] executablePath];

        return executablePath && [executablePath containsString:@".app/Contents/MacOS/"];
    }
}

void useBundleResources()
{
    if (!isAppBundle())
        return;

    @autoreleasepool
    {
        NSString* resourcePath = [[NSBundle mainBundle] resourcePath];

        if (resourcePath)
        {
            chdir([resourcePath fileSystemRepresentation]);
        }
    }
}

void openInNewInstance(const char* path)
{
    @autoreleasepool
    {
        NSURL* file = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path]];

        if (!isAppBundle())
        {
            NSURL* executable = [[NSBundle mainBundle] executableURL];

            [NSTask launchedTaskWithExecutableURL:executable arguments:@[ [file path] ] error:nil terminationHandler:nil];
            return;
        }

        NSWorkspaceOpenConfiguration* configuration = [NSWorkspaceOpenConfiguration configuration];
        configuration.createsNewApplicationInstance = YES;

        [[NSWorkspace sharedWorkspace] openURLs:@[ file ] withApplicationAtURL:[[NSBundle mainBundle] bundleURL] configuration:configuration completionHandler:nil];
    }
}

} // namespace macos
