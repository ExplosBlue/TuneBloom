#pragma once

#include <ui/backup/Backup.h>

#include <filesystem>
#include <string>

namespace backup
{

namespace fs = std::filesystem;

struct Recovery
{
    std::string markerPath;
    Snapshot snapshot;
};

bool IsActive();
bool GetFailure(Bfsar::BackupResult* result);
void MarkEditsBackedUp();

fs::path GetRootDir();
void PinSnapshot(const std::string& snapshotDir);
void SetUntitledKey(const std::string& key);
void ForgetArchiveKeys();

bool ReadInfoFile(const fs::path& dir, Snapshot* snapshot);
bool IsSafeRelativePath(const std::string& path);
void LinkUnchangedGroupFiles(const fs::path& dir, const std::string& previousDir, const std::vector<std::string>& groupFiles);
bool WriteSnapshot(Kind kind, Bfsar::BackupResult* result, std::string* outDir = nullptr);

fs::path GetMarkerPath();
void WriteMarker(const std::string& snapshotDir);
void RemoveMarker();

void ScanRecoveries();
bool HasRecovery();
const Recovery& GetNextRecovery();
void DropNextRecovery();
void AddRecovery(Recovery&& recovery);
void RemoveRecoveryMarker(const Recovery& recovery);
void DiscardRecovery(const Recovery& recovery);

}
