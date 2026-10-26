#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>

#include <platform/macos/openFiles.h>
#include <ui/file/OpenQueue.h>

namespace {

bool isDocument(NSString* path)
{
    NSString* ext = [[path pathExtension] lowercaseString];
    return [ext isEqualToString:@"bfsar"] || [ext isEqualToString:@"bcsar"] || [ext isEqualToString:@"cmpbin"] ||
           [ext isEqualToString:@"bars"] || [ext isEqualToString:@"bwav"] || [ext isEqualToString:@"zs"];
}

void handleOpenURLs(id self, SEL cmd, NSApplication* app, NSArray<NSURL*>* urls)
{
    for (NSURL* url in urls)
    {
        if (![url isFileURL])
            continue;

        NSString* path = [url path];
        if (!isDocument(path))
            continue;

        openqueue::PushExternal([path fileSystemRepresentation]);
    }
}

} // namespace

namespace macos {

void installOpenFileHandler()
{
    Class cls = objc_getClass("GLFWApplicationDelegate");
    if (!cls)
        return;

    class_addMethod(cls, @selector(application:openURLs:), reinterpret_cast<IMP>(handleOpenURLs), "v@:@@");
}

} // namespace macos
