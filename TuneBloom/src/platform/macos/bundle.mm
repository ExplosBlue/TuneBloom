#import <Cocoa/Cocoa.h>
#include <platform/macos/bundle.h>

#include <unistd.h>

namespace macos {

void useBundleResources()
{
    @autoreleasepool
    {
        NSBundle* bundle = [NSBundle mainBundle];
        NSString* executablePath = [bundle executablePath];

        if (!executablePath || ![executablePath containsString:@".app/Contents/MacOS/"])
        {
            return;
        }

        NSString* resourcePath = [bundle resourcePath];

        if (resourcePath)
        {
            chdir([resourcePath fileSystemRepresentation]);
        }
    }
}

} // namespace macos
