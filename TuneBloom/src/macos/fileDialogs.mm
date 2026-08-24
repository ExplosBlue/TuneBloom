#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <macos/fileDialogs.h>

#include <sstream>

namespace {

NSArray<UTType*>* buildContentTypes(const std::vector<std::string>& filters)
{
    NSMutableArray<UTType*>* types = [NSMutableArray array];

    for (size_t i = 1; i < filters.size(); i += 2)
    {
        std::istringstream stream(filters[i]);
        std::string pattern;
        while (stream >> pattern)
        {
            if (pattern == "*" || pattern == "*.*")
                return nil;

            if (pattern.rfind("*.", 0) != 0)
                continue;

            UTType* type = [UTType typeWithFilenameExtension:@(pattern.substr(2).c_str())];
            if (type)
                [types addObject:type];
        }
    }

    return types.count > 0 ? types : nil;
}

} // namespace

namespace macos {

bool openFileDialog(const std::string& title, const std::vector<std::string>& filters, std::string& outPath)
{
    @autoreleasepool
    {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.canChooseFiles = YES;
        panel.canChooseDirectories = NO;
        panel.allowsMultipleSelection = NO;

        if (!title.empty())
            panel.message = @(title.c_str());

        NSArray<UTType*>* types = buildContentTypes(filters);
        if (types)
            panel.allowedContentTypes = types;

        if ([panel runModal] != NSModalResponseOK || !panel.URL)
            return false;

        outPath = panel.URL.fileSystemRepresentation;
        return true;
    }
}

bool selectFolderDialog(const std::string& title, std::string& outPath)
{
    @autoreleasepool
    {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.canChooseFiles = NO;
        panel.canChooseDirectories = YES;
        panel.allowsMultipleSelection = NO;
        panel.canCreateDirectories = YES;

        if (!title.empty())
            panel.message = @(title.c_str());

        if ([panel runModal] != NSModalResponseOK || !panel.URL)
            return false;

        outPath = panel.URL.fileSystemRepresentation;
        return true;
    }
}

bool saveFileDialog(const std::string& title, const std::string& defaultPath, std::string& outPath)
{
    @autoreleasepool
    {
        [[NSUserDefaults standardUserDefaults] setBool:YES forKey:@"NSNavPanelExpandedStateForSaveMode"];

        NSSavePanel* panel = [NSSavePanel savePanel];
        panel.canCreateDirectories = YES;

        if (!title.empty())
            panel.message = @(title.c_str());

        if (!defaultPath.empty())
        {
            NSString* path = @(defaultPath.c_str());
            NSString* dir = [path stringByDeletingLastPathComponent];
            NSString* name = [path lastPathComponent];

            if (dir.length > 0)
                panel.directoryURL = [NSURL fileURLWithPath:dir isDirectory:YES];
            if (name.length > 0)
                panel.nameFieldStringValue = name;
        }

        if ([panel runModal] != NSModalResponseOK || !panel.URL)
            return false;

        outPath = panel.URL.fileSystemRepresentation;
        return true;
    }
}

} // namespace macos
