#include <ui/file/FileDialogs.h>

#include <ui/Messages.h>
#include <core/TestHooks.h>

#include <core/Debug.h>

#include <libs/portable-file-dialogs.h>

#if defined(SEAD_PLATFORM_MACOSX)
#include <platform/macos/fileDialogs.h>
#endif

#include <algorithm>
#include <filesystem>

static std::vector<std::string> BuildFilterList(u32 filterCount, const FileFilter* filters)
{
    std::vector<std::string> list;

    for (u32 i = 0; i < filterCount; i++)
    {
        SEAD_ASSERT(filters);
        list.push_back(filters[i].name);
        list.push_back(filters[i].filter);
    }

    list.push_back(messages::filedialog::cAllFilesFilter);
    list.push_back("*.*");

    return list;
}

bool OpenFileDialog(sead::BufferedSafeString* outPath, const char* title, u32 filterCount, FileFilter* filters)
{
    LOG_FMT("title=\"%s\" filterCount=%u", title ? title : "nullptr", filterCount);
    SEAD_ASSERT(outPath);

    if (testhooks::IsEnabled())
    {
        const char* capturePath = testhooks::GetOpenFilePath();

        if (!capturePath)
            return false;

        outPath->copy(capturePath);
        return true;
    }

    const std::vector<std::string> filtersVec = BuildFilterList(filterCount, filters);

    std::string resultPath;
#if defined(SEAD_PLATFORM_MACOSX)
    if (!macos::openFileDialog(title ? title : "", filtersVec, resultPath))
        return false;
#else
    std::vector<std::string> result = pfd::open_file(title ? title : "", "", filtersVec, pfd::opt::none).result();
    if (result.empty())
        return false;
    resultPath = result[0];
#endif

    LOG_STR(resultPath.c_str());
    outPath->copy(resultPath.c_str());
    return true;
}

bool OpenFilesDialog(std::vector<std::string>* outPaths, const char* title, u32 filterCount, FileFilter* filters)
{
    LOG_FMT("title=\"%s\" filterCount=%u", title ? title : "nullptr", filterCount);
    SEAD_ASSERT(outPaths);

    const std::vector<std::string> filtersVec = BuildFilterList(filterCount, filters);

    outPaths->clear();

    if (testhooks::IsEnabled())
    {
        const char* capturePaths = testhooks::GetOpenFilePath();

        if (!capturePaths)
            return false;

        std::string paths(capturePaths);
        size_t start = 0;

        while (start <= paths.size())
        {
            const size_t end = (std::min)(paths.find(';', start), paths.size());

            if (end > start)
                outPaths->push_back(paths.substr(start, end - start));

            start = end + 1;
        }

        return !outPaths->empty();
    }

#if defined(SEAD_PLATFORM_MACOSX)
    if (!macos::openFilesDialog(title ? title : "", filtersVec, *outPaths))
        return false;
#else
    *outPaths = pfd::open_file(title ? title : "", "", filtersVec, pfd::opt::multiselect).result();
#endif

    std::sort(outPaths->begin(), outPaths->end());

    return !outPaths->empty();
}

bool SelectFolderDialog(sead::BufferedSafeString* outPath, const char* title)
{
    LOG_FMT("title=\"%s\"", title ? title : "nullptr");
    SEAD_ASSERT(outPath);

    if (testhooks::IsEnabled())
        return false;

    std::string result;
#if defined(SEAD_PLATFORM_MACOSX)
    if (!macos::selectFolderDialog(title ? title : "", result))
        return false;
#else
    result = pfd::select_folder(title ? title : "", "").result();
    if (result.empty())
        return false;
#endif

    LOG_STR(result.c_str());
    outPath->copy(result.c_str());
    return true;
}

static bool HasFilterExtension(const std::string& path, u32 filterCount, const FileFilter* filters)
{
    for (u32 i = 0; i < filterCount; i++)
    {
        const std::string patterns = filters[i].filter;
        size_t start = 0;

        while (start < patterns.size())
        {
            size_t end = patterns.find_first_of(" ;", start);
            if (end == std::string::npos)
                end = patterns.size();

            const std::string pattern = patterns.substr(start, end - start);
            start = end + 1;

            if (pattern.size() < 3 || pattern.compare(0, 2, "*.") != 0)
                continue;

            const std::string ext = pattern.substr(1);

            if (ext.find('*') == std::string::npos && path.size() > ext.size() && path.compare(path.size() - ext.size(), ext.size(), ext) == 0)
                return true;
        }
    }

    return false;
}

bool SaveFileDialog(sead::BufferedSafeString* outPath, const char* title, u32 filterCount, FileFilter* filters, const char* defaultExt, const char* defaultName)
{
    LOG_FMT("title=\"%s\" filterCount=%u defaultExt=\"%s\" defaultName=\"%s\"", title ? title : "nullptr", filterCount, defaultExt ? defaultExt : "nullptr", defaultName ? defaultName : "nullptr");
    SEAD_ASSERT(outPath);

    if (testhooks::IsEnabled())
        return false;

    std::string defaultPath;
    if (defaultName && *defaultName)
        defaultPath = std::filesystem::path(defaultName).make_preferred().string();

    std::string result;
#if defined(SEAD_PLATFORM_MACOSX)
    if (!macos::saveFileDialog(title ? title : "", defaultPath, result))
        return false;
#else
    const std::vector<std::string> filtersVec = BuildFilterList(filterCount, filters);

    result = pfd::save_file(title ? title : "", defaultPath, filtersVec, pfd::opt::none).result();
    if (result.empty())
        return false;
#endif

    if (defaultExt && *defaultExt && !HasFilterExtension(result, filterCount, filters))
    {
        std::string extWithDot = ".";
        extWithDot += defaultExt;
        if (result.size() < extWithDot.size() ||
            result.compare(result.size() - extWithDot.size(), extWithDot.size(), extWithDot) != 0)
        {
            result += extWithDot;
        }
    }

    LOG_STR(result.c_str());
    outPath->copy(result.c_str());
    return true;
}

bool OpenFileDialogWithExtension(sead::BufferedSafeString* outPath, const char* filterName, const char* ext)
{
    const sead::FormatFixedSafeString<32> pattern("*.%s", ext);
    FileFilter filter(filterName, pattern.cstr());

    return OpenFileDialog(outPath, nullptr, 1, &filter);
}

bool SaveFileDialogWithExtension(sead::BufferedSafeString* outPath, const char* filterName, const char* ext, const char* defaultPath)
{
    const sead::FormatFixedSafeString<32> pattern("*.%s", ext);
    FileFilter filter(filterName, pattern.cstr());

    return SaveFileDialog(outPath, nullptr, 1, &filter, ext, defaultPath);
}
