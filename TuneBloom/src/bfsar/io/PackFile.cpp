#include <bfsar/io/PackFile.h>

#include <aal/BarsSarc.h>
#include <bfsar/archive/Cmpbin.h>
#include <bfsar/io/FileIo.h>
#include <core/Sarc.h>

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace packfile
{

static bool ReadPack(const std::string& path, std::vector<u8>* out, bool* outIsZstd)
{
    if (!fileio::ReadWholeFile(path.c_str(), out))
        return false;

    *outIsZstd = cmpbin::IsZstdFrame(out->data(), out->size());

    if (!*outIsZstd)
        return true;

    std::vector<u8> plain;

    if (!cmpbin::DecompressZstdFrame(out->data(), out->size(), plain))
        return false;

    *out = std::move(plain);
    return true;
}

static bool EncodePack(std::vector<u8>* bytes, bool isZstd)
{
    if (!isZstd)
        return true;

    std::vector<u8> packed;

    if (!cmpbin::CompressZstdFrame(bytes->data(), bytes->size(), packed))
        return false;

    *bytes = std::move(packed);
    return true;
}

enum class EntryChange
{
    Failed,
    Same,
    Changed
};

static EntryChange ReplaceEntry(const std::string& packPath, const std::string& innerName, const void* data, size_t size, std::vector<u8>* outPack, bool* outIsZstd)
{
    sarc::File pack;

    if (!ReadPack(packPath, outPack, outIsZstd) || !sarc::Read(outPack->data(), outPack->size(), &pack))
        return EntryChange::Failed;

    sarc::Entry* entry = sarc::FindEntry(pack, innerName);

    if (!entry)
        return EntryChange::Failed;

    const u8* first = static_cast<const u8*>(data);

    if (entry->data.size() == size && std::equal(first, first + size, entry->data.begin()))
        return EntryChange::Same;

    entry->data.assign(first, first + size);
    *outPack = sarc::Write(pack);

    return EntryChange::Changed;
}

static bool ReadIndex(const std::string& path, sarc::Index* out)
{
    std::vector<u8> header;
    std::vector<u8> packed;

    if (!fileio::ReadFileStart(path.c_str(), sarc::cHeaderSize, &header))
        return false;

    const bool isZstd = cmpbin::IsZstdFrame(header.data(), header.size());

    if (isZstd && (!fileio::ReadWholeFile(path.c_str(), &packed) || !cmpbin::PeekZstdFrame(packed.data(), packed.size(), sarc::cHeaderSize, header)))
        return false;

    const u32 indexSize = sarc::GetIndexSize(header.data(), header.size());

    if (indexSize < sarc::cHeaderSize)
        return false;

    const bool hasIndex = isZstd ? cmpbin::PeekZstdFrame(packed.data(), packed.size(), indexSize, header) : fileio::ReadFileStart(path.c_str(), indexSize, &header);

    if (!hasIndex || header.size() < indexSize)
        return false;

    return sarc::ReadIndex(header.data(), header.size(), out);
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

static fs::path GetGameRoot(const std::string& packPath)
{
    const fs::path packFolder = fs::path(packPath).parent_path();

    return packFolder.filename() == cFolderName ? packFolder.parent_path() : packFolder;
}

std::string GetFolderOnDisk(const std::string& path)
{
    std::string packPath;
    std::string innerName;

    if (!Split(path, &packPath, &innerName))
        return fs::path(path).parent_path().string();

    return (GetGameRoot(packPath) / fs::path(innerName).parent_path()).make_preferred().string();
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
    const sarc::IndexEntry* entry = ReadIndex(packPath, &index) ? sarc::FindIndexEntry(index, innerName) : nullptr;

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

    if (!ReadPackContents(packPath, &pack) || !sarc::ReadIndex(pack.data(), pack.size(), &index))
        return false;

    const sarc::IndexEntry* entry = sarc::FindIndexEntry(index, innerName);

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
    bool isZstd = false;
    const EntryChange change = ReplaceEntry(packPath, innerName, data, size, &bytes, &isZstd);

    if (change != EntryChange::Changed)
        return change == EntryChange::Same;

    return EncodePack(&bytes, isZstd) && fileio::WriteWholeFileAtomic(packPath.c_str(), bytes.data(), bytes.size());
}

bool CopyFileOver(const std::string& srcPath, const std::string& destPath)
{
    if (srcPath == destPath)
        return true;

    if (!IsPackPath(srcPath) && !IsPackPath(destPath))
        return fileio::CopyFileOver(srcPath.c_str(), destPath.c_str());

    std::vector<u8> bytes;

    return ReadFile(srcPath, &bytes) && WriteFile(destPath, bytes.data(), bytes.size());
}

bool MakePackWithFile(const std::string& packPath, const std::string& innerName, const void* data, size_t size, std::vector<u8>* out)
{
    bool isZstd = false;
    const EntryChange change = ReplaceEntry(packPath, innerName, data, size, out, &isZstd);

    if (change == EntryChange::Same)
        return fileio::ReadWholeFile(packPath.c_str(), out);

    return change == EntryChange::Changed && EncodePack(out, isZstd);
}

bool ReadPackContents(const std::string& packPath, std::vector<u8>* out)
{
    bool isZstd = false;

    return ReadPack(packPath, out, &isZstd);
}

}
