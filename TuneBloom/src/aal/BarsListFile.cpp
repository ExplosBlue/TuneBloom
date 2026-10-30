#include <aal/BarsListFile.h>

#include <aal/BarsSarc.h>
#include <bfsar/io/FileIo.h>
#include <bfsar/io/PackFile.h>
#include <bfsar/model/ArchiveFormat.h>
#include <core/FileName.h>
#include <messages/Messages.h>

#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

using FoundBarsMap = std::unordered_map<std::string, FoundBars>;

static constexpr u32 cScanEntryMax = 20000;
static constexpr s32 cScanDepthMax = 8;

static constexpr char cPackExtension[] = "pack";

static std::string GetDiskName(const std::string& path)
{
    const std::string name = fs::path(path).filename().string();

    return IsZstdPath(name.c_str()) ? fs::path(name).stem().string() : name;
}

static std::string MakeLocation(const fs::path& path, const fs::path& base)
{
    const fs::path relative = path.lexically_relative(base);

    return relative.empty() || *relative.begin() == ".." ? path.string() : relative.generic_string();
}

static void AddLooseBars(const fs::path& root, const fs::path& base, FoundBarsMap* found)
{
    std::error_code error;
    u32 entryCount = 0;

    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, error), end; !error && it != end; it.increment(error))
    {
        if (++entryCount > cScanEntryMax)
            break;

        if (it.depth() >= cScanDepthMax)
            it.disable_recursion_pending();

        std::error_code entryError;
        std::string path;

        if (!fileio::GetNarrowPath(it->path(), &path))
            continue;

        const std::string name = GetDiskName(path);

        if (!filename::HasExactExtension(name, cBarsExtension) || !it->is_regular_file(entryError))
            continue;

        found->emplace(name, FoundBars { path, MakeLocation(it->path().parent_path(), base), it->file_size(entryError) });
    }
}

static void AddPackBars(const fs::path& folder, const fs::path& base, FoundBarsMap* found)
{
    std::error_code error;

    for (fs::directory_iterator it(folder, fs::directory_options::skip_permission_denied, error), end; !error && it != end; it.increment(error))
    {
        std::error_code entryError;
        std::vector<packfile::Resource> resources;
        std::string packPath;

        if (!fileio::GetNarrowPath(it->path(), &packPath) || !filename::HasExactExtension(GetDiskName(packPath), cPackExtension) || !it->is_regular_file(entryError) || !packfile::ListResources(packPath, &resources))
            continue;

        for (const packfile::Resource& resource : resources)
        {
            const std::string name = fs::path(resource.name).filename().string();

            if (filename::HasExactExtension(name, cBarsExtension))
                found->emplace(name, FoundBars { packfile::MakePath(packPath, resource.name), MakeLocation(it->path(), base), resource.size });
        }
    }
}

static void CollectBars(const std::string& listPath, FoundBarsMap* found)
{
    const bool isInPack = packfile::IsPackPath(listPath);

    const fs::path listFolder = isInPack ? fs::path(packfile::GetFolderOnDisk(listPath)) : fs::path(listPath).parent_path();
    const fs::path soundRoot = listFolder.parent_path();
    const fs::path base = soundRoot.parent_path();

    AddLooseBars(soundRoot, base, found);

    if (isInPack)
        AddPackBars(fs::path(packfile::GetDiskPath(listPath)).parent_path(), base, found);

    AddPackBars(base / packfile::cFolderName, base, found);
}

BarsListEntry::BarsListEntry()
    : Item()
{
    mItemType = ItemType::BarsListEntry;
    setEnableName(true);
}

BarsListFile::BarsListFile()
    : mFilePath()
    , mSaved()
    , mEntries()
    , mFound()
{
}

bool BarsListFile::open(const void* data, u64 size, const sead::SafeString& path)
{
    aal::BarsList list;

    if (!aal::ReadBarsList(data, size, &list))
        return false;

    mFilePath = path;
    mSaved = list;
    mEntries.clear();

    for (const std::string& name : list.entries)
        mEntries.pushBack(createEntry(sead::SafeString(name.c_str())));

    renumber();
    findBarsFiles();

    return true;
}

aal::BarsList BarsListFile::makeList(const sead::SafeString& path) const
{
    aal::BarsList list = mSaved;
    list.name = fs::path(path.cstr()).stem().string();
    list.entries.clear();

    for (const Item* entry : mEntries)
        list.entries.push_back(entry->getName().cstr());

    return list;
}

std::string BarsListFile::getListName() const
{
    return makeList(mFilePath).name;
}

bool BarsListFile::isModified() const
{
    const aal::BarsList list = makeList(mFilePath);

    return list.entries != mSaved.entries || list.name != mSaved.name;
}

bool BarsListFile::IsValidEntryName(const sead::SafeString& name)
{
    const std::string text = name.cstr();

    if (!filename::HasExactExtension(text, cBarsExtension) || text.front() == ' ')
        return false;

    for (const char c : text)
    {
        if (static_cast<u8>(c) < 0x20 || std::strchr(filename::cInvalidCharacters, c))
            return false;
    }

    return true;
}

bool BarsListFile::isUniqueEntryName(const sead::SafeString& name, const Item* ignoreItem) const
{
    for (const Item* entry : mEntries)
    {
        if (entry != ignoreItem && entry->getName() == name)
            return false;
    }

    return true;
}

BarsListEntry* BarsListFile::createEntry(const sead::SafeString& name) const
{
    BarsListEntry* entry = new BarsListEntry();
    entry->getName() = name;

    return entry;
}

void BarsListFile::renumber()
{
    u32 id = 0;

    for (Item* entry : mEntries)
        entry->setId(id++);
}

void BarsListFile::findBarsFiles()
{
    mFound.clear();

    if (!mFilePath.isEmpty())
        CollectBars(mFilePath.cstr(), &mFound);
}

const FoundBars* BarsListFile::findBars(const Item* entry) const
{
    const auto it = mFound.find(entry->getName().cstr());

    return it == mFound.end() ? nullptr : &it->second;
}

u32 BarsListFile::countFound() const
{
    u32 count = 0;

    for (const Item* entry : mEntries)
    {
        if (findBars(entry))
            count++;
    }

    return count;
}

bool BarsListFile::writeTo(const sead::SafeString& path) const
{
    const std::vector<u8> bytes = aal::WriteBarsList(makeList(path));

    return packfile::WriteFile(path.cstr(), bytes.data(), bytes.size());
}

SaveResult BarsListFile::save(const char** outError)
{
    return saveAs(sead::FixedSafeString<512>(mFilePath), outError);
}

SaveResult BarsListFile::saveAs(const sead::SafeString& path, const char** outError)
{
    if (!writeTo(path))
    {
        *outError = messages::barslist::cWriteFailed;
        return SaveResult::Failed;
    }

    const bool isMoved = path != mFilePath;

    mFilePath = path;
    mSaved = makeList(path);
    *outError = nullptr;

    if (isMoved)
        findBarsFiles();

    return SaveResult::Saved;
}
