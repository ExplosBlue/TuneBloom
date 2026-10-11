#pragma once

#include <basis/seadTypes.h>
#include <prim/seadSafeString.h>

#include <string>
#include <vector>

struct FileFilter
{
    FileFilter(const char* name_, const char* filter_)
        : name(name_)
        , filter(filter_)
    {
    }

    const char* name;
    const char* filter;
};

bool OpenFileDialog(sead::BufferedSafeString* outPath, const char* title = nullptr, u32 filterCount = 0, FileFilter* filters = nullptr);
bool OpenFilesDialog(std::vector<std::string>* outPaths, const char* title = nullptr, u32 filterCount = 0, FileFilter* filters = nullptr);
bool SaveFileDialog(sead::BufferedSafeString* outPath, const char* title = nullptr, u32 filterCount = 0, FileFilter* filters = nullptr, const char* defaultExt = nullptr, const char* defaultName = "");
bool SelectFolderDialog(sead::BufferedSafeString* outPath, const char* title = nullptr);

bool OpenFileDialogWithExtension(sead::BufferedSafeString* outPath, const char* filterName, const char* ext);
bool SaveFileDialogWithExtension(sead::BufferedSafeString* outPath, const char* filterName, const char* ext, const char* defaultPath);
