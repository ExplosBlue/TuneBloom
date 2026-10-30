#include <bfsar/io/TitleFolder.h>

#include <bfsar/io/FileIo.h>

#include <basis/seadTypes.h>

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace titlefolder
{

static constexpr char cAppFolder[]      = "code";
static constexpr char cAppFile[]        = "app.xml";
static constexpr char cTitleIdTag[]     = "<title_id";
static constexpr u32 cTitleKindShift    = 32;
static constexpr u64 cTitleLowMask      = 0xFFFFFFFF;
static constexpr u64 cUpdateTitleKind   = 0x0005000E;
static constexpr u64 cBaseTitleKind     = 0x00050000;

static fs::path GetAppFile(const fs::path& titleRoot)
{
    return titleRoot / cAppFolder / cAppFile;
}

static bool ParseHex(const std::string& text, u64* out)
{
    char* last = nullptr;
    *out = std::strtoull(text.c_str(), &last, 16);

    if (last == text.c_str())
        return false;

    while (*last != '\0' && std::isspace(static_cast<unsigned char>(*last)))
        last++;

    return *last == '\0';
}

static bool ReadTitleId(const fs::path& titleRoot, u64* out)
{
    std::string appFile;
    std::vector<u8> bytes;

    if (!fileio::GetNarrowPath(GetAppFile(titleRoot), &appFile) || !fileio::ReadWholeFile(appFile.c_str(), &bytes))
        return false;

    const std::string text(bytes.begin(), bytes.end());
    const size_t tag = text.find(cTitleIdTag);
    const size_t start = tag == std::string::npos ? std::string::npos : text.find('>', tag);
    const size_t end = start == std::string::npos ? std::string::npos : text.find('<', start);

    return end != std::string::npos && ParseHex(text.substr(start + 1, end - start - 1), out);
}

static bool FindTitleRoot(const fs::path& folder, fs::path* out)
{
    std::error_code error;

    for (fs::path root = folder; !root.empty() && root != root.parent_path(); root = root.parent_path())
    {
        if (!fs::is_regular_file(GetAppFile(root), error))
            continue;

        *out = root;
        return true;
    }

    return false;
}

static bool FindTitle(const fs::path& searchFolder, const fs::path& skipped, u64 titleId, fs::path* out)
{
    std::error_code error;

    for (const fs::directory_entry& entry : fs::directory_iterator(searchFolder, error))
    {
        u64 entryId = 0;

        if (entry.path() == skipped || !entry.is_directory(error) || !ReadTitleId(entry.path(), &entryId) || entryId != titleId)
            continue;

        *out = entry.path();
        return true;
    }

    return false;
}

std::string FindBaseFolder(const std::string& folder)
{
    std::error_code error;
    const fs::path start = fs::absolute(folder, error).lexically_normal();

    fs::path titleRoot;
    fs::path baseRoot;
    u64 titleId = 0;

    if (!FindTitleRoot(start, &titleRoot) || !ReadTitleId(titleRoot, &titleId) || (titleId >> cTitleKindShift) != cUpdateTitleKind)
        return std::string();

    const u64 baseId = (cBaseTitleKind << cTitleKindShift) | (titleId & cTitleLowMask);

    std::string baseFolder;

    if (!FindTitle(titleRoot.parent_path(), titleRoot, baseId, &baseRoot) || !fileio::GetNarrowPath((baseRoot / start.lexically_relative(titleRoot)).make_preferred(), &baseFolder))
        return std::string();

    return baseFolder;
}

}
