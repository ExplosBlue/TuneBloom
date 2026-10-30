#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <platform/macos/defaultApp.h>

namespace
{

constexpr int64_t cChangeTimeout = 10 * NSEC_PER_SEC;

UTType* getContentType(const char* extension)
{
    return [UTType typeWithFilenameExtension:[NSString stringWithUTF8String:extension]];
}

NSURL* findDefaultApp(const char* extension)
{
    UTType* type = getContentType(extension);

    return type ? [[NSWorkspace sharedWorkspace] URLForApplicationToOpenContentType:type] : nil;
}

bool isThisApp(NSURL* app)
{
    NSString* identifier = [[NSBundle bundleWithURL:app] bundleIdentifier];

    return identifier && [identifier isEqualToString:[[NSBundle mainBundle] bundleIdentifier]];
}

NSURL* findOtherApp(UTType* type)
{
    for (NSURL* app in [[NSWorkspace sharedWorkspace] URLsForApplicationsToOpenContentType:type])
    {
        if (!isThisApp(app))
            return app;
    }

    return nil;
}

bool setDefaultApp(NSURL* app, UTType* type)
{
    if (!app || !type)
        return false;

    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    __block bool success = false;

    [[NSWorkspace sharedWorkspace] setDefaultApplicationAtURL:app toOpenContentType:type completionHandler:^(NSError* error) {
        success = error == nil;
        dispatch_semaphore_signal(done);
    }];

    const bool finished = dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, cChangeTimeout)) == 0;
    dispatch_release(done);

    return finished && success;
}

}

namespace macos
{

std::string getDefaultAppName(const char* extension)
{
    @autoreleasepool
    {
        NSURL* app = findDefaultApp(extension);

        return app ? [[[app URLByDeletingPathExtension] lastPathComponent] UTF8String] : std::string();
    }
}

bool isDefaultApp(const char* extension)
{
    @autoreleasepool
    {
        NSURL* app = findDefaultApp(extension);

        return app && isThisApp(app);
    }
}

bool hasOtherApp(const char* extension)
{
    @autoreleasepool
    {
        UTType* type = getContentType(extension);

        return type && findOtherApp(type);
    }
}

bool setDefaultToThisApp(const char* extension)
{
    @autoreleasepool
    {
        return setDefaultApp([[NSBundle mainBundle] bundleURL], getContentType(extension));
    }
}

bool setDefaultToOtherApp(const char* extension)
{
    @autoreleasepool
    {
        UTType* type = getContentType(extension);

        return setDefaultApp(type ? findOtherApp(type) : nil, type);
    }
}

}
