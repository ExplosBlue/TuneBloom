#pragma once

#include <bfsar/archive/Bfsar.h>

#include <string>
#include <vector>

namespace backup
{

enum class Kind
{
    Auto,
    Save,
    Restore,
    Manual
};

struct Snapshot
{
    std::string dir;
    std::string archiveName;
    std::string originalPath;
    Kind kind = Kind::Auto;
    s64 time = 0;
    s64 sourceStamp = 0;
    s32 saveNames = -1;
    u64 size = 0;
    std::vector<Bfsar::BackupStream> streams;
    std::vector<std::string> groupFiles;
};

void Update();
void OnClean();
void KeepPreviousVersion(const char* destPath = nullptr);

std::vector<Snapshot> ListSnapshots();
bool SaveRestorePoint(const Snapshot& target);
bool RestoreSnapshot(const Snapshot& snapshot);
bool CreateBackup();
std::string GetFolder();
std::string GetRootFolder();
void GetUsage(size_t* count, u64* size);
void FormatSize(u64 size, char* buffer, size_t bufferSize);
std::string GetArchiveName();

void OpenRestoreDialog();
void DrawDialogs();

}
