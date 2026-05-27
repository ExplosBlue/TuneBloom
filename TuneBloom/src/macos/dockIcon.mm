#import <Cocoa/Cocoa.h>

namespace macos {

void setDockIcon(const char* path)
{
    @autoreleasepool
    {
        NSString* nsPath = [NSString stringWithUTF8String:path];
        NSImage* image = [[NSImage alloc] initWithContentsOfFile:nsPath];

        if (image)
        {
            [NSApp setApplicationIconImage:image];
        }
    }
}

} // namespace macos
