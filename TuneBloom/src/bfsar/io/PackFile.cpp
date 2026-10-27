#include <bfsar/io/PackFile.h>

#include <aal/BarsSarc.h>
#include <bfsar/io/FileIo.h>
#include <core/Sarc.h>

#include <filesystem>

namespace fs = std::filesystem;

namespace packfile
{

static constexpr u64 cSarcHeaderSize = 0x14;

static bool ReadIndex(const std::string& path, sarc::Index* out)
{
    std::vector<u8> header;

    if (!fileio::ReadFileStart(path.c_str(), cSarcHeaderSize, &header))
        return false;

    const u32 indexSize = sarc::GetIndexSize(header.data(), header.size());

    if (indexSize < cSarcHeaderSize || !fileio::ReadFileStart(path.c_str(), indexSize, &header) || header.size() < indexSize)
        return false;

    return sarc::ReadIndex(header.data(), header.size(), out);
}

static const sarc::IndexEntry* FindIndexEntry(const sarc::Index& index, const std::string& name)
{
    for (const sarc::IndexEntry& entry : index.entries)
    {
        if (entry.hasName && entry.name == name)
            return &entry;
    }

    return nullptr;
}

bool Split(const std::string& path, std::string* outPackPath, std::string* outInnerName)
{
    std::error_code error;
    const fs::path full(path);

    if (path.empty() || fs::exists(full, error))
        return false;

    for (fs::path pack = full.parent_path(); !pack.empty() && pack != pack.parent_path(); pack = pack.parent_path())
    {
        if (fs::is_directory(pack, error))
            return false;

        if (!fs::is_regular_file(pack, error))
            continue;

        *outPackPath = pack.string();
        *outInnerName = full.lexically_relative(pack).generic_string();
        return !outInnerName->empty() && *outInnerName != ".";
    }

    return false;
}

bool IsPackPath(const std::string& path)
{
    std::string packPath;
    std::string innerName;

    return Split(path, &packPath, &innerName);
}

bool IsOnDisk(const std::string& path)
{
    std::error_code error;

    return fs::is_regular_file(path, error) || IsPackPath(path);
}

std::string GetDiskPath(const std::string& path)
{
    std::string packPath;
    std::string innerName;

    return Split(path, &packPath, &innerName) ? packPath : path;
}

std::string GetFolderOnDisk(const std::string& path)
{
    std::string packPath;
    std::string innerName;

    if (!Split(path, &packPath, &innerName))
        return fs::path(path).parent_path().string();

    const fs::path innerFolder = fs::path(innerName).parent_path();
    const fs::path packFolder = fs::path(packPath).parent_path();
    std::error_code error;

    for (fs::path root = packFolder; !root.empty() && root != root.parent_path(); root = root.parent_path())
    {
        if (fs::is_directory(root / innerFolder, error))
            return (root / innerFolder).string();
    }

    return (packFolder / innerFolder).string();
}

std::string MakePath(const std::string& packPath, const std::string& innerName)
{
    return (fs::path(packPath) / fs::path(innerName)).string();
}

bool GetFileSize(const std::string& path, u64* outSize)
{
    std::string packPath;
    std::string innerName;
    std::error_code error;

    if (!Split(path, &packPath, &innerName))
    {
        const std::uintmax_t size = fs::file_size(path, error);
        *outSize = static_cast<u64>(size);
        return !error;
    }

    sarc::Index index;
    const sarc::IndexEntry* entry = ReadIndex(packPath, &index) ? FindIndexEntry(index, innerName) : nullptr;

    if (!entry)
        return false;

    *outSize = entry->size;
    return true;
}

bool IsSoundPackFile(const std::string& path)
{
    sarc::Index index;

    return ReadIndex(path, &index) && aal::IsSoundPackIndex(index);
}

bool ListResources(const std::string& packPath, std::vector<Resource>* out)
{
    sarc::Index index;

    if (!ReadIndex(packPath, &index))
        return false;

    out->clear();

    for (const sarc::IndexEntry& entry : index.entries)
    {
        if (entry.hasName && aal::IsPackResourceName(entry.name))
            out->push_back({ entry.name, entry.offset, entry.size });
    }

    return true;
}

bool ReadFile(const std::string& path, std::vector<u8>* out)
{
    std::string packPath;
    std::string innerName;

    if (!Split(path, &packPath, &innerName))
        return fileio::ReadWholeFile(path.c_str(), out);

    std::vector<u8> pack;
    sarc::Index index;

    if (!fileio::ReadWholeFile(packPath.c_str(), &pack) || !sarc::ReadIndex(pack.data(), pack.size(), &index))
        return false;

    const sarc::IndexEntry* entry = FindIndexEntry(index, innerName);

    if (!entry || entry->offset + entry->size > pack.size())
        return false;

    out->assign(pack.begin() + entry->offset, pack.begin() + entry->offset + entry->size);
    return true;
}

bool WriteFile(const std::string& path, const void* data, size_t size)
{
    std::string packPath;
    std::string innerName;

    if (!Split(path, &packPath, &innerName))
        return fileio::WriteWholeFileAtomic(path.c_str(), data, size);

    std::vector<u8> bytes;
    sarc::File pack;

    if (!fileio::ReadWholeFile(packPath.c_str(), &bytes) || !sarc::Read(bytes.data(), bytes.size(), &pack))
        return false;

    sarc::Entry* entry = sarc::FindEntry(pack, innerName);

    if (!entry)
        return false;

    const u8* first = static_cast<const u8*>(data);
    entry->data.assign(first, first + size);

    bytes = sarc::Write(pack);

    return fileio::WriteWholeFileAtomic(packPath.c_str(), bytes.data(), bytes.size());
}

}
