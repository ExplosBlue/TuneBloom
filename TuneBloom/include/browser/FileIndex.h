#pragma once

#include <browser/FileProbe.h>
#include <core/BackgroundJob.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

struct BrowserFile
{
    std::string name;
    std::string stem;
    std::string path;
    s32 folder = 0;
    u64 size = 0;
    FileProbe probe;
};

struct BrowserFolder
{
    std::string name;
    s32 parent = -1;
    std::vector<s32> folders;
    std::vector<s32> files;
    u32 fileCount = 0;
};

struct FileIndexSnapshot
{
    std::string root;
    std::vector<BrowserFolder> folders;
    std::vector<BrowserFile> files;
    u64 totalSize = 0;
    u32 kindCounts[static_cast<s32>(BrowserFileKind::Count)] = {};
};

class FileIndex
{
public:
    void scan(const std::string& root, const std::string& cachePath);
    void cancel();
    bool update();

    bool isScanning() const;
    u32 getScannedCount() const;
    const FileIndexSnapshot& getSnapshot() const;

private:
    BackgroundJob mJob;
    std::atomic<bool> mCancel { false };
    std::atomic<u32> mScannedCount { 0 };
    std::atomic<u32> mProbedCount { 0 };
    std::unique_ptr<FileIndexSnapshot> mPending;
    FileIndexSnapshot mSnapshot;
};
