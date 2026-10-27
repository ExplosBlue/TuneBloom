#include <browser/FileIndex.h>

#include <bfsar/io/FileIo.h>
#include <bfsar/io/PackFile.h>
#include <bfsar/model/ArchiveFormat.h>
#include <browser/FileIndexCache.h>
#include <core/ParallelFor.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iterator>
#include <unordered_map>

namespace fs = std::filesystem;

struct ScannedFile
{
    fs::path path;
    std::string key;
    size_t mergedFolderStart = 0;
    size_t mergedFolderEnd = 0;
    u64 size = 0;
    s64 modified = 0;
    bool isAudio = false;
    FileProbe probe;
};

static bool IsHiddenName(const fs::path& path)
{
    const std::string name = path.filename().string();

    return !name.empty() && name[0] == '.';
}

static std::string MakeStem(const fs::path& path)
{
    fs::path stem = path.filename();

    if (IsZstdPath(stem.string().c_str()))
        stem = stem.stem();

    return stem.stem().string();
}

static bool IsNameLess(const std::string& left, const std::string& right)
{
    return std::lexicographical_compare(left.begin(), left.end(), right.begin(), right.end(), [](char a, char b)
    {
        return std::tolower(static_cast<unsigned char>(a)) < std::tolower(static_cast<unsigned char>(b));
    });
}

static std::vector<ScannedFile> CollectFile(const fs::path& path, std::atomic<u32>* foundCount)
{
    std::error_code error;
    ScannedFile file;

    file.path = path;
    file.key = path.filename().generic_string();
    file.size = fs::file_size(path, error);
    file.modified = fs::last_write_time(path, error).time_since_epoch().count();
    (*foundCount)++;

    return { std::move(file) };
}

static std::vector<ScannedFile> CollectFiles(const fs::path& root, const std::atomic<bool>& cancel, std::atomic<u32>* foundCount)
{
    std::vector<ScannedFile> files;
    std::error_code error;

    if (fs::is_regular_file(root, error))
        return CollectFile(root, foundCount);

    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, error);
    const fs::recursive_directory_iterator end;

    while (!error && it != end && !cancel)
    {
        const fs::directory_entry& entry = *it;

        if (IsHiddenName(entry.path()))
        {
            if (entry.is_directory(error))
                it.disable_recursion_pending();
        }
        else if (entry.is_regular_file(error))
        {
            ScannedFile file;
            file.path = entry.path();
            file.key = file.path.lexically_relative(root).generic_string();
            file.size = entry.file_size(error);
            file.modified = entry.last_write_time(error).time_since_epoch().count();
            files.push_back(std::move(file));
            (*foundCount)++;
        }

        error.clear();
        it.increment(error);
    }

    return files;
}

static s32 AddFolder(FileIndexSnapshot* snapshot, std::unordered_map<std::string, s32>* folderIds, const ScannedFile& file, size_t folderEnd)
{
    const std::string& path = file.key;
    s32 folder = 0;

    for (size_t start = 0; start < folderEnd;)
    {
        const bool isMerged = start == file.mergedFolderStart && file.mergedFolderEnd > start;
        const size_t end = isMerged ? file.mergedFolderEnd : std::min(path.find('/', start), folderEnd);
        const std::string key = path.substr(0, end);
        const auto found = folderIds->find(key);
        const size_t partStart = start;

        start = end + 1;

        if (found != folderIds->end())
        {
            folder = found->second;
            continue;
        }

        const s32 created = static_cast<s32>(snapshot->folders.size());

        BrowserFolder child;
        child.name = path.substr(partStart, end - partStart);
        child.parent = folder;
        snapshot->folders.push_back(std::move(child));
        snapshot->folders[folder].folders.push_back(created);

        folderIds->emplace(key, created);
        folder = created;
    }

    return folder;
}

static void SortFolder(FileIndexSnapshot* snapshot, s32 folder)
{
    BrowserFolder& entry = snapshot->folders[folder];

    std::sort(entry.folders.begin(), entry.folders.end(), [snapshot](s32 left, s32 right)
    {
        return IsNameLess(snapshot->folders[left].name, snapshot->folders[right].name);
    });

    std::sort(entry.files.begin(), entry.files.end(), [snapshot](s32 left, s32 right)
    {
        return IsNameLess(snapshot->files[left].name, snapshot->files[right].name);
    });

    for (s32 child : entry.folders)
        SortFolder(snapshot, child);
}

static void BuildSnapshot(const fs::path& root, std::vector<ScannedFile>& scanned, FileIndexSnapshot* snapshot)
{
    std::unordered_map<std::string, s32> folderIds;

    std::sort(scanned.begin(), scanned.end(), [](const ScannedFile& left, const ScannedFile& right) { return IsNameLess(left.key, right.key); });

    BrowserFolder rootFolder;
    rootFolder.name = root.filename().string();
    snapshot->folders.push_back(std::move(rootFolder));

    for (ScannedFile& scannedFile : scanned)
    {
        if (!scannedFile.isAudio)
            continue;

        if (scannedFile.probe.isPack)
        {
            BrowserFolder& pack = snapshot->folders[AddFolder(snapshot, &folderIds, scannedFile, scannedFile.key.size())];
            pack.isPack = true;
            pack.packSize = scannedFile.size;
            pack.packFormat = scannedFile.probe.formatName;
            continue;
        }

        const size_t nameStart = scannedFile.key.rfind('/');
        const s32 folder = AddFolder(snapshot, &folderIds, scannedFile, nameStart == std::string::npos ? 0 : nameStart);
        const s32 fileId = static_cast<s32>(snapshot->files.size());

        BrowserFile file;
        file.name = scannedFile.path.filename().string();
        file.stem = MakeStem(scannedFile.path);
        file.path = scannedFile.path.string();
        file.folder = folder;
        file.size = scannedFile.size;
        file.probe = scannedFile.probe;
        snapshot->files.push_back(std::move(file));
        snapshot->folders[folder].files.push_back(fileId);

        snapshot->totalSize += scannedFile.size;
        snapshot->kindCounts[static_cast<s32>(scannedFile.probe.kind)]++;

        for (s32 ancestor = folder; ancestor >= 0; ancestor = snapshot->folders[ancestor].parent)
            snapshot->folders[ancestor].fileCount++;
    }

    SortFolder(snapshot, 0);
}

static bool ReadCachedProbe(const FileIndexCache& cache, ScannedFile* file)
{
    const auto found = cache.find(file->key);

    if (found == cache.end() || found->second.size != file->size || found->second.modified != file->modified)
        return false;

    file->isAudio = found->second.isAudio;
    file->probe = found->second.probe;

    return true;
}

static void SaveScannedFiles(const fs::path& cachePath, const std::vector<ScannedFile>& scanned)
{
    FileIndexCache cache;

    for (const ScannedFile& file : scanned)
        cache.emplace(file.key, CachedFile { file.size, file.modified, file.isAudio, file.probe });

    SaveFileIndexCache(cachePath, cache);
}

static void ProbePackFiles(const fs::path& packPath, std::vector<ScannedFile>* files, const std::vector<u64>& offsets, const std::vector<u8>& isCached, const std::atomic<bool>& cancel, std::atomic<u32>* probedCount)
{
    std::vector<u8> bytes;

    if (!fileio::ReadWholeFile(packPath.string().c_str(), &bytes))
        return;

    ParallelFor(static_cast<u32>(files->size()), [&](u32 index)
    {
        ScannedFile& file = (*files)[index];

        if (cancel || isCached[index] || offsets[index] + file.size > bytes.size())
            return;

        file.isAudio = ProbeFileBytes(bytes.data() + offsets[index], file.size, &file.probe);
        (*probedCount)++;
    });
}

static size_t GetSharedFolderLength(const std::vector<packfile::Resource>& resources)
{
    if (resources.empty())
        return 0;

    const std::string& first = resources.front().name;
    size_t length = first.rfind('/') == std::string::npos ? 0 : first.rfind('/') + 1;

    for (const packfile::Resource& resource : resources)
    {
        const size_t common = std::mismatch(first.begin(), first.begin() + std::min(length, resource.name.size()), resource.name.begin()).first - first.begin();

        length = common == 0 ? 0 : first.rfind('/', common - 1) + 1;
    }

    return length;
}

static void AddPackFiles(const ScannedFile& pack, const FileIndexCache& cache, const std::atomic<bool>& cancel, std::atomic<u32>* probedCount, std::vector<ScannedFile>* out)
{
    std::vector<packfile::Resource> resources;

    if (!packfile::ListResources(pack.path.string(), &resources))
        return;

    const size_t sharedFolderLength = GetSharedFolderLength(resources);
    std::vector<ScannedFile> files;
    std::vector<u64> offsets;
    std::vector<u8> isCached;

    for (const packfile::Resource& resource : resources)
    {
        ScannedFile file;
        file.path = packfile::MakePath(pack.path.string(), resource.name);
        file.key = pack.key + '/' + resource.name;

        if (sharedFolderLength > 0)
        {
            file.mergedFolderStart = pack.key.size() + 1;
            file.mergedFolderEnd = file.mergedFolderStart + sharedFolderLength - 1;
        }
        file.size = resource.size;
        file.modified = pack.modified;
        isCached.push_back(ReadCachedProbe(cache, &file) ? 1 : 0);

        files.push_back(std::move(file));
        offsets.push_back(resource.offset);
    }

    if (std::find(isCached.begin(), isCached.end(), 0) != isCached.end())
        ProbePackFiles(pack.path, &files, offsets, isCached, cancel, probedCount);

    out->insert(out->end(), std::make_move_iterator(files.begin()), std::make_move_iterator(files.end()));
}

static void AddAllPackFiles(std::vector<ScannedFile>* scanned, const FileIndexCache& cache, const std::atomic<bool>& cancel, std::atomic<u32>* probedCount)
{
    std::vector<ScannedFile> packFiles;

    for (const ScannedFile& file : *scanned)
    {
        if (!cancel && file.isAudio && file.probe.isPack)
            AddPackFiles(file, cache, cancel, probedCount, &packFiles);
    }

    scanned->insert(scanned->end(), std::make_move_iterator(packFiles.begin()), std::make_move_iterator(packFiles.end()));
}

FileIndex::~FileIndex()
{
    cancel();
}

void FileIndex::scan(const std::string& root, const std::string& cachePath, bool isCacheRead)
{
    cancel();

    mListDone = false;
    mFoundCount = 0;
    mScannedCount = 0;
    mProbedCount = 0;
    mPending = std::make_unique<FileIndexSnapshot>();
    mPending->root = root;

    FileIndexSnapshot* pending = mPending.get();

    mJob.start([this, pending, cachePath, isCacheRead]()
    {
        FileIndexCache cache;

        if (isCacheRead && !cachePath.empty())
            LoadFileIndexCache(cachePath, &cache);

        const fs::path rootPath(pending->root);
        std::vector<ScannedFile> scanned = CollectFiles(rootPath, mCancel, &mFoundCount);

        mListDone = true;

        ParallelFor(static_cast<u32>(scanned.size()), [this, &scanned, &cache](u32 index)
        {
            if (mCancel)
                return;

            ScannedFile& file = scanned[index];

            if (!ReadCachedProbe(cache, &file))
            {
                file.isAudio = ProbeFile(file.path, file.size, ProbeDepth::Contents, &file.probe);
                mProbedCount++;
            }

            mScannedCount++;
        });

        AddAllPackFiles(&scanned, cache, mCancel, &mProbedCount);

        if (mCancel)
            return;

        if (!cachePath.empty() && (mProbedCount > 0 || cache.size() != scanned.size()))
            SaveScannedFiles(cachePath, scanned);

        BuildSnapshot(rootPath, scanned, pending);
    });
}

void FileIndex::cancel()
{
    mCancel = true;
    mJob.wait();
    mPending.reset();
    mCancel = false;
}

bool FileIndex::update(FileIndexSnapshot* previous)
{
    if (!mJob.finish() || !mPending)
        return false;

    *previous = std::move(mSnapshot);
    mSnapshot = std::move(*mPending);
    mPending.reset();

    return true;
}

bool FileIndex::isScanning() const
{
    return mJob.isBusy();
}

bool FileIndex::isListDone() const
{
    return mListDone;
}

u32 FileIndex::getFoundCount() const
{
    return mFoundCount;
}

u32 FileIndex::getScannedCount() const
{
    return mScannedCount;
}

const FileIndexSnapshot& FileIndex::getSnapshot() const
{
    return mSnapshot;
}
