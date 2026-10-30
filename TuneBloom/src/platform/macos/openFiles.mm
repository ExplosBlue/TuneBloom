#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>

#include <platform/macos/openFiles.h>
#include <ui/file/File.h>

namespace
{

void handleOpenURLs(id self, SEL cmd, NSApplication* app, NSArray<NSURL*>* urls)
{
    for (NSURL* url in urls)
    {
        if ([url isFileURL])
            OpenExternalPath([[url path] fileSystemRepresentation]);
    }
}

}

namespace macos
{

void installOpenFileHandler()
{
    Class cls = objc_getClass("GLFWApplicationDelegate");
    if (!cls)
        return;

    class_addMethod(cls, @selector(application:openURLs:), reinterpret_cast<IMP>(handleOpenURLs), "v@:@@");
}

}
