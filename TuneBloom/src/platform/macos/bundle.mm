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

} // namespace macos
