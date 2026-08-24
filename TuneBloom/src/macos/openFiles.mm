#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>

#include <macos/openFiles.h>
#include <prim/seadSafeString.h>

extern sead::FixedSafeString<512> sDroppedFilePath;

namespace {

bool isSoundArchive(NSString* path)
{
    NSString* ext = [[path pathExtension] lowercaseString];
    return [ext isEqualToString:@"bfsar"] || [ext isEqualToString:@"bcsar"] || [ext isEqualToString:@"cmpbin"];
}

void handleOpenURLs(id self, SEL cmd, NSApplication* app, NSArray<NSURL*>* urls)
{
    for (NSURL* url in urls)
    {
        if (![url isFileURL])
            continue;

        NSString* path = [url path];
        if (!isSoundArchive(path))
            continue;

        sDroppedFilePath = [path fileSystemRepresentation];
        break;
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
