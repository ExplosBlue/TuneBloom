#include <browser/FileIndex.h>

#include <bfsar/model/ArchiveFormat.h>
#include <browser/FileIndexCache.h>
#include <core/ParallelFor.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <unordered_map>

namespace fs = std::filesystem;

struct ScannedFile
{
    fs::path path;
    std::string key;
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

static std::vector<ScannedFile> CollectFiles(const fs::path& root, const std::atomic<bool>& cancel)
{
    std::vector<ScannedFile> files;
    std::error_code error;
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
        }

        error.clear();
        it.increment(error);
    }

    return files;
}

static s32 AddFolder(FileIndexSnapshot* snapshot, std::unordered_map<std::string, s32>* folderIds, const fs::path& relativeFolder)
{
    s32 folder = 0;
    fs::path walked;

    for (const fs::path& part : relativeFolder)
    {
        walked /= part;

        const std::string key = walked.generic_string();
        const auto found = folderIds->find(key);

        if (found != folderIds->end())
        {
            folder = found->second;
            continue;
        }

        const s32 created = static_cast<s32>(snapshot->folders.size());

        BrowserFolder child;
        child.name = part.string();
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

    BrowserFolder rootFolder;
    rootFolder.name = root.filename().string();
    snapshot->folders.push_back(std::move(rootFolder));

    for (ScannedFile& scannedFile : scanned)
    {
        if (!scannedFile.isAudio)
            continue;

        std::error_code error;
        const fs::path relative = fs::relative(scannedFile.path, root, error);

        if (error)
            continue;

        const s32 folder = AddFolder(snapshot, &folderIds, relative.parent_path());
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

void FileIndex::scan(const std::string& root, const std::string& cachePath)
{
    cancel();

    mScannedCount = 0;
    mProbedCount = 0;
    mPending = std::make_unique<FileIndexSnapshot>();
    mPending->root = root;

    FileIndexSnapshot* pending = mPending.get();

    mJob.start([this, pending, cachePath]()
    {
        FileIndexCache cache;

        if (!cachePath.empty())
            LoadFileIndexCache(cachePath, &cache);

        const fs::path rootPath(pending->root);
        std::vector<ScannedFile> scanned = CollectFiles(rootPath, mCancel);

        ParallelFor(static_cast<u32>(scanned.size()), [this, &scanned, &cache](u32 index)
        {
            if (mCancel)
                return;

            ScannedFile& file = scanned[index];

            if (!ReadCachedProbe(cache, &file))
            {
                file.isAudio = ProbeFile(file.path, file.size, &file.probe);
                mProbedCount++;
            }

            mScannedCount++;
        });

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

bool FileIndex::update()
{
    if (!mJob.finish() || !mPending)
        return false;

    mSnapshot = std::move(*mPending);
    mPending.reset();

    return true;
}

bool FileIndex::isScanning() const
{
    return mJob.isBusy();
}

u32 FileIndex::getScannedCount() const
{
    return mScannedCount;
}

const FileIndexSnapshot& FileIndex::getSnapshot() const
{
    return mSnapshot;
}
