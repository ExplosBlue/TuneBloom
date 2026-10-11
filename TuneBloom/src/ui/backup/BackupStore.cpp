#include <ui/backup/BackupInternal.h>

#include <ui/settings/Config.h>
#include <ui/settings/ConfigFile.h>
#include <ui/file/File.h>
#include <ui/Messages.h>
#include <ui/file/UnsavedChanges.h>

#include <bfsar/model/ArchiveFormat.h>
#include <bfsar/io/FileIo.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <thread>

#if defined(SEAD_PLATFORM_WINDOWS)
#include <windows.h>
#else
#include <signal.h>
#include <unistd.h>
#endif

namespace backup
{

static constexpr const char* cRootFolder         = "backups";
static constexpr const char* cInfoFile           = "snapshot.json";
static constexpr const char* cLegacyInfoFile     = "snapshot.cfg";
static constexpr const char* cNamesSuffix        = ".metadata.json";
static constexpr const char* cCmpbinSuffix       = ".cmpbin";
static constexpr const char* cTempSuffix         = ".tmp";
static constexpr const char* cMarkerPrefix       = "session-";
static constexpr const char* cMarkerSuffix       = ".json";
static constexpr const char* cMarkerDirKey       = "dir";
static constexpr const char* cLegacyMarkerSuffix = ".cfg";
static constexpr const char* cLegacyMarkerPrefix = "dir=";
static constexpr const char* cUntitledPrefix     = "Untitled-";
static constexpr const char* cImageName          = "tunebloom";

static constexpr s64 cInfoVersion = 1;

static constexpr size_t cPreviousVersionMax = 5;
static constexpr size_t cRestorePointMax    = 3;
static constexpr size_t cManualMax          = 5;
static constexpr size_t cKeyNameMax         = 24;

static constexpr s32 cTempKeepMinutes    = 10;
static constexpr s32 cUntitledKeepDays   = 14;
static constexpr s32 cRenameTries        = 5;
static constexpr s32 cRenameWaitMs       = 50;

static constexpr u64 cSpaceMargin = 16 * 1024 * 1024;

struct Marker
{
    fs::path path;
    u32 pid;
    std::string dir;
};

static std::string sUntitledKey;
static std::string sPinnedDir;
static std::vector<Recovery> sRecoveries;

fs::path GetRootDir()
{
    return fs::path(GetConfigDir()) / cRootFolder;
}

static u32 GetProcessId()
{
#if defined(SEAD_PLATFORM_WINDOWS)
    return static_cast<u32>(GetCurrentProcessId());
#else
    return static_cast<u32>(getpid());
#endif
}

#if defined(SEAD_PLATFORM_WINDOWS)
static std::string GetProcessImageName(HANDLE process)
{
    char image[MAX_PATH] = {};
    DWORD imageSize = sizeof(image);

    if (!QueryFullProcessImageNameA(process, 0, image, &imageSize))
        return "";

    std::string name = fs::path(image).filename().string();
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    return name;
}
#endif

static bool IsProcessRunning(u32 pid)
{
#if defined(SEAD_PLATFORM_WINDOWS)
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);

    if (!process)
        return GetLastError() == ERROR_ACCESS_DENIED;

    DWORD exitCode = 0;
    const bool running = GetExitCodeProcess(process, &exitCode) && exitCode == STILL_ACTIVE;
    const std::string name = GetProcessImageName(process);

    CloseHandle(process);

    if (!running)
        return false;

    if (name.empty())
        return true;

    return name == GetProcessImageName(GetCurrentProcess()) || name.find(cImageName) != std::string::npos;
#else
    return kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
#endif
}

static std::string GetArchiveExtension()
{
    const ArchiveFormatInfo* info = getFormatInfo(sBfsar.getFormat(), sBfsar.getPlatform());

    return info ? info->extension : "bfsar";
}

std::string GetArchiveName()
{
    if (!sBfsar.getFilePath().isEmpty())
        return fs::path(sBfsar.getFilePath().cstr()).filename().string();

    if (!sPinnedDir.empty() && !sBfsar.getLoadedArchivePath().isEmpty())
        return fs::path(sBfsar.getLoadedArchivePath().cstr()).filename().string();

    return std::string(messages::backup::cUntitled) + "." + GetArchiveExtension();
}

static std::string FormatTimeName(std::time_t time)
{
    char text[32];
    std::strftime(text, sizeof(text), "%Y-%m-%d_%H-%M-%S", std::localtime(&time));

    return text;
}

static std::string NormalizePath(const char* path)
{
    std::error_code ec;
    const fs::path absolute = fs::absolute(path, ec);

    return ec ? std::string(path) : absolute.lexically_normal().string();
}

static std::string GetArchivePath()
{
    return sBfsar.getFilePath().isEmpty() ? std::string() : NormalizePath(sBfsar.getFilePath().cstr());
}

static std::string GetKeyForPath(const std::string& archivePath)
{
    std::string path = archivePath;
    std::replace(path.begin(), path.end(), '\\', '/');

#if defined(SEAD_PLATFORM_WINDOWS)
    std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif

    u32 hash = 2166136261u;

    for (const char c : path)
    {
        hash ^= static_cast<u8>(c);
        hash *= 16777619u;
    }

    std::string name = fs::path(archivePath).filename().string();

    if (name.size() > cKeyNameMax)
        name.resize(cKeyNameMax);

    for (char& c : name)
    {
        const bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';

        if (!keep)
            c = '_';
    }

    char suffix[16];
    std::snprintf(suffix, sizeof(suffix), "-%08x", hash);

    return name + suffix;
}

static std::string GetArchiveKey()
{
    if (!sBfsar.getFilePath().isEmpty())
        return GetKeyForPath(GetArchivePath());

    if (sUntitledKey.empty())
        sUntitledKey = cUntitledPrefix + FormatTimeName(std::time(nullptr)) + "-" + std::to_string(GetProcessId());

    return sUntitledKey;
}

void PinSnapshot(const std::string& snapshotDir)
{
    sPinnedDir = snapshotDir;
}

void SetUntitledKey(const std::string& key)
{
    sUntitledKey = key;
}

void ForgetArchiveKeys()
{
    sUntitledKey.clear();
    sPinnedDir.clear();
}

static bool IsSafeRelativePath(const std::string& path)
{
    const fs::path relative(path);

    if (relative.empty() || relative.is_absolute() || relative.has_root_name() || relative.has_root_directory())
        return false;

    for (const fs::path& part : relative)
    {
        if (part == "..")
            return false;
    }

    return true;
}

static bool IsSnapshotDir(const fs::path& dir)
{
    const fs::path relative = dir.lexically_normal().lexically_relative(GetRootDir().lexically_normal());

    return std::distance(relative.begin(), relative.end()) == 2 && IsSafeRelativePath(relative.string());
}

static bool IsSameDir(const std::string& a, const std::string& b)
{
    return fs::path(a).lexically_normal() == fs::path(b).lexically_normal();
}

static const char* GetKindName(Kind kind)
{
    switch (kind)
    {
    case Kind::Save:
        return "save";

    case Kind::Restore:
        return "restore";

    case Kind::Manual:
        return "manual";

    default:
        return "auto";
    }
}

static Kind GetKindOfName(const char* name)
{
    for (const Kind kind : { Kind::Save, Kind::Restore, Kind::Manual })
    {
        if (std::strcmp(name, GetKindName(kind)) == 0)
            return kind;
    }

    return Kind::Auto;
}

static bool WriteInfoFile(const fs::path& dir, const Snapshot& snapshot)
{
    json::Value info;
    info.setInt("version", cInfoVersion);
    info.setString("path", snapshot.originalPath.c_str());
    info.setString("file", snapshot.archiveName.c_str());
    info.setString("kind", GetKindName(snapshot.kind));
    info.setInt("time", snapshot.time);

    if (snapshot.sourceStamp != 0)
        info.setString("sourceStamp", std::to_string(snapshot.sourceStamp).c_str());

    if (snapshot.saveNames >= 0)
        info.setBool("saveNames", snapshot.saveNames != 0);

    json::Value& streams = info.setArray("streams");

    for (const Bfsar::BackupStream& stream : snapshot.streams)
    {
        char signature[20];
        std::snprintf(signature, sizeof(signature), "%016llx", static_cast<unsigned long long>(stream.signature));

        json::Value& entry = streams.pushObject();
        entry.setString("signature", signature);
        entry.setString("path", stream.path.c_str());
    }

    json::Value& groupFiles = info.setArray("groupFiles");

    for (const std::string& groupFile : snapshot.groupFiles)
        groupFiles.pushString(groupFile.c_str());

    std::string text;
    json::Write(info, &text);

    return fileio::WriteWholeFile((dir / cInfoFile).string().c_str(), text.data(), text.size()) == fileio::WriteResult::Ok;
}

static bool ReadInfoJson(const fs::path& path, Snapshot* snapshot)
{
    std::vector<u8> data;
    json::Value info;

    if (!fileio::ReadWholeFile(path.string().c_str(), &data))
        return false;

    if (!json::Parse(reinterpret_cast<const char*>(data.data()), data.size(), &info) || !info.isObject())
        return false;

    if (const std::string* originalPath = info.findString("path"))
        snapshot->originalPath = *originalPath;

    if (const std::string* archiveName = info.findString("file"))
        snapshot->archiveName = *archiveName;

    if (const std::string* kind = info.findString("kind"))
        snapshot->kind = GetKindOfName(kind->c_str());

    s64 time;

    if (info.findInteger("time", &time))
        snapshot->time = time;

    if (const std::string* sourceStamp = info.findString("sourceStamp"))
        snapshot->sourceStamp = std::strtoll(sourceStamp->c_str(), nullptr, 10);

    if (info.find("saveNames"))
        snapshot->saveNames = info.getBool("saveNames", false) ? 1 : 0;

    if (const json::Value* groupFiles = info.findArray("groupFiles"))
    {
        for (const json::Value& groupFile : groupFiles->items)
        {
            if (groupFile.isString() && IsSafeRelativePath(groupFile.string))
                snapshot->groupFiles.push_back(groupFile.string);
        }
    }

    const json::Value* streams = info.findArray("streams");

    if (!streams)
        return true;

    for (const json::Value& entry : streams->items)
    {
        const std::string* signature = entry.findString("signature");
        const std::string* streamPath = entry.findString("path");

        if (signature && streamPath && IsSafeRelativePath(*streamPath))
            snapshot->streams.push_back({*streamPath, std::strtoull(signature->c_str(), nullptr, 16)});
    }

    return true;
}

static void ReadLegacyInfoLine(const char* line, Snapshot* snapshot)
{
    if (std::strncmp(line, "path=", 5) == 0)
    {
        snapshot->originalPath = line + 5;
    }
    else if (std::strncmp(line, "file=", 5) == 0)
    {
        snapshot->archiveName = line + 5;
    }
    else if (std::strncmp(line, "kind=", 5) == 0)
    {
        snapshot->kind = GetKindOfName(line + 5);
    }
    else if (std::strncmp(line, "time=", 5) == 0)
    {
        snapshot->time = std::strtoll(line + 5, nullptr, 10);
    }
    else if (std::strncmp(line, "source=", 7) == 0)
    {
        snapshot->sourceStamp = std::strtoll(line + 7, nullptr, 10);
    }
    else if (std::strncmp(line, "names=", 6) == 0)
    {
        snapshot->saveNames = std::atoi(line + 6) != 0 ? 1 : 0;
    }
    else if (std::strncmp(line, "stream=", 7) == 0)
    {
        char* end = nullptr;
        const u64 signature = std::strtoull(line + 7, &end, 16);

        if (end && *end == '\t' && IsSafeRelativePath(end + 1))
            snapshot->streams.push_back({end + 1, signature});
    }
}

static bool MeasureSnapshot(const fs::path& dir, Snapshot* snapshot)
{
    if (snapshot->archiveName.empty() || snapshot->archiveName != fs::path(snapshot->archiveName).filename().string())
        return false;

    std::error_code ec;
    const std::uintmax_t archiveSize = fs::file_size(dir / snapshot->archiveName, ec);

    if (ec || archiveSize == 0)
        return false;

    snapshot->size = archiveSize;

    const std::uintmax_t namesSize = fs::file_size((dir / snapshot->archiveName).string() + cNamesSuffix, ec);

    if (!ec)
        snapshot->size += namesSize;

    ec.clear();

    for (const Bfsar::BackupStream& stream : snapshot->streams)
    {
        const std::uintmax_t streamSize = fs::file_size(dir / stream.path, ec);

        if (!ec)
            snapshot->size += streamSize;

        ec.clear();
    }

    for (const std::string& groupFile : snapshot->groupFiles)
    {
        const std::uintmax_t groupFileSize = fs::file_size(dir / groupFile, ec);

        if (!ec)
            snapshot->size += groupFileSize;

        ec.clear();
    }

    return true;
}

static bool ReadLegacyInfo(const fs::path& path, Snapshot* snapshot)
{
    FILE* f = fopen(path.string().c_str(), "rb");

    if (!f)
        return false;

    char line[2048];

    while (fgets(line, sizeof(line), f))
    {
        size_t length = std::strlen(line);

        while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r'))
            line[--length] = '\0';

        ReadLegacyInfoLine(line, snapshot);
    }

    fclose(f);

    return true;
}

bool ReadInfoFile(const fs::path& dir, Snapshot* snapshot)
{
    if (!ReadInfoJson(dir / cInfoFile, snapshot) && !ReadLegacyInfo(dir / cLegacyInfoFile, snapshot))
        return false;

    snapshot->dir = dir.string();

    return MeasureSnapshot(dir, snapshot);
}

static bool HasSuffix(const std::string& text, const char* suffix)
{
    const size_t length = std::strlen(suffix);

    return text.size() >= length && text.compare(text.size() - length, length, suffix) == 0;
}

static std::vector<Snapshot> ListSnapshotsIn(const fs::path& keyDir)
{
    std::vector<Snapshot> snapshots;
    std::error_code ec;

    for (fs::directory_iterator it(keyDir, ec), end; !ec && it != end; it.increment(ec))
    {
        std::error_code entryError;

        if (!it->is_directory(entryError) || entryError)
            continue;

        if (HasSuffix(it->path().filename().string(), cTempSuffix))
            continue;

        Snapshot snapshot;

        if (ReadInfoFile(it->path(), &snapshot))
            snapshots.push_back(std::move(snapshot));
    }

    std::sort(snapshots.begin(), snapshots.end(), [](const Snapshot& a, const Snapshot& b)
    {
        if (a.time != b.time)
            return a.time > b.time;

        return a.dir > b.dir;
    });

    return snapshots;
}

static void RemoveOldTempDirs(const fs::path& keyDir)
{
    const fs::file_time_type limit = fs::file_time_type::clock::now() - std::chrono::minutes(cTempKeepMinutes);
    std::vector<fs::path> old;
    std::error_code ec;

    for (fs::directory_iterator it(keyDir, ec), end; !ec && it != end; it.increment(ec))
    {
        std::error_code entryError;

        if (!HasSuffix(it->path().filename().string(), cTempSuffix))
            continue;

        const fs::file_time_type time = it->last_write_time(entryError);

        if (!entryError && time < limit)
            old.push_back(it->path());
    }

    for (const fs::path& dir : old)
        fs::remove_all(dir, ec);
}

static std::string ReadMarkerDir(const fs::path& markerPath)
{
    std::vector<u8> data;

    if (!fileio::ReadWholeFile(markerPath.string().c_str(), &data))
        return "";

    json::Value marker;

    if (json::Parse(reinterpret_cast<const char*>(data.data()), data.size(), &marker) && marker.isObject())
    {
        const std::string* dir = marker.findString(cMarkerDirKey);

        return dir ? *dir : "";
    }

    const std::string text(data.begin(), data.end());

    if (text.rfind(cLegacyMarkerPrefix, 0) != 0)
        return "";

    const size_t start = std::strlen(cLegacyMarkerPrefix);

    return text.substr(start, text.find_first_of("\r\n", start) - start);
}

static bool IsMarkerName(const std::string& name)
{
    return name.rfind(cMarkerPrefix, 0) == 0 && (HasSuffix(name, cMarkerSuffix) || HasSuffix(name, cLegacyMarkerSuffix));
}

static std::vector<std::string> ListMarkedDirs()
{
    std::vector<std::string> dirs;
    std::error_code ec;

    for (fs::directory_iterator it(GetRootDir(), ec), end; !ec && it != end; it.increment(ec))
    {
        if (!IsMarkerName(it->path().filename().string()))
            continue;

        const std::string dir = ReadMarkerDir(it->path());

        if (!dir.empty())
            dirs.push_back(dir);
    }

    return dirs;
}

static bool IsProtected(const Snapshot& snapshot, const std::vector<std::string>& markedDirs)
{
    if (!sPinnedDir.empty() && IsSameDir(snapshot.dir, sPinnedDir))
        return true;

    for (const std::string& dir : markedDirs)
    {
        if (IsSameDir(snapshot.dir, dir))
            return true;
    }

    return false;
}

static void MakeRoom(const fs::path& keyDir, const std::vector<Snapshot>& snapshots)
{
    if (snapshots.size() < 2)
        return;

    std::error_code ec;
    const std::uintmax_t archiveSize = fs::file_size(fs::path(snapshots.front().dir) / snapshots.front().archiveName, ec);

    if (ec)
        return;

    const u64 needed = static_cast<u64>(archiveSize) + cSpaceMargin;
    const std::vector<std::string> markedDirs = ListMarkedDirs();

    for (size_t i = snapshots.size() - 1; i > 0; i--)
    {
        const fs::space_info space = fs::space(keyDir, ec);

        if (ec || space.available >= needed)
            return;

        if (snapshots[i].kind == Kind::Auto && !IsProtected(snapshots[i], markedDirs))
            fs::remove_all(snapshots[i].dir, ec);
    }
}

static void Rotate(const fs::path& keyDir)
{
    const std::vector<Snapshot> snapshots = ListSnapshotsIn(keyDir);
    const std::vector<std::string> markedDirs = ListMarkedDirs();

    size_t count[4] = {};
    const size_t limit[4] = {
        static_cast<size_t>(sAutoBackupMaxBackups < 1 ? 1 : sAutoBackupMaxBackups),
        cPreviousVersionMax,
        cRestorePointMax,
        cManualMax
    };

    std::error_code ec;

    for (const Snapshot& snapshot : snapshots)
    {
        const size_t kind = static_cast<size_t>(snapshot.kind);
        count[kind]++;

        if (count[kind] <= limit[kind] || IsProtected(snapshot, markedDirs))
            continue;

        fs::remove_all(snapshot.dir, ec);
    }
}

fs::path GetMarkerPath()
{
    return GetRootDir() / (cMarkerPrefix + std::to_string(GetProcessId()) + cMarkerSuffix);
}

static void WriteMarkerFile(const fs::path& path, const std::string& snapshotDir)
{
    json::Value marker;
    marker.setString(cMarkerDirKey, snapshotDir.c_str());

    std::string text;
    json::Write(marker, &text);

    WriteTextFileAtomic(path.string(), text);
}

void WriteMarker(const std::string& snapshotDir)
{
    WriteMarkerFile(GetMarkerPath(), snapshotDir);
}

void RemoveMarker()
{
    std::error_code ec;
    fs::remove(GetMarkerPath(), ec);
}

static fs::path MakeSnapshotDir(const fs::path& keyDir, std::time_t time, Kind kind)
{
    const std::string base = FormatTimeName(time) + "_" + GetKindName(kind);
    std::error_code ec;

    fs::path dir = keyDir / base;

    for (s32 i = 2; fs::exists(dir, ec) || fs::exists(dir.string() + cTempSuffix, ec); i++)
        dir = keyDir / (base + "-" + std::to_string(i));

    return dir;
}

static bool FinishSnapshot(const fs::path& tempDir, const fs::path& finalDir, Snapshot* snapshot)
{
    std::error_code ec;

    if (!WriteInfoFile(tempDir, *snapshot))
    {
        fs::remove_all(tempDir, ec);
        return false;
    }

    for (s32 i = 0; i < cRenameTries; i++)
    {
        fs::rename(tempDir, finalDir, ec);

        if (!ec)
            break;

        std::this_thread::sleep_for(std::chrono::milliseconds(cRenameWaitMs));
    }

    if (ec)
    {
        fs::remove_all(tempDir, ec);
        return false;
    }

    snapshot->dir = finalDir.string();

    return true;
}

static void LinkUnchangedGroupFiles(const fs::path& dir, const std::string& previousDir, const std::vector<std::string>& groupFiles)
{
    if (previousDir.empty())
        return;

    for (const std::string& groupFile : groupFiles)
    {
        const fs::path file = dir / groupFile;
        const fs::path previousFile = fs::path(previousDir) / groupFile;
        const fs::path linkFile = file.string() + cTempSuffix;

        if (!fileio::HasSameBytes(file.string().c_str(), previousFile.string().c_str()))
            continue;

        std::error_code ec;
        fs::create_hard_link(previousFile, linkFile, ec);

        if (ec)
            continue;

        fs::rename(linkFile, file, ec);

        if (ec)
            fs::remove(linkFile, ec);
    }
}

bool WriteSnapshot(Kind kind, Bfsar::BackupResult* result, std::string* outDir)
{
    *result = Bfsar::BackupResult::OpenFailed;

    if (!IsActive() || !sBfsar.isOpen())
        return false;

    const fs::path keyDir = GetRootDir() / GetArchiveKey();
    std::error_code ec;

    fs::create_directories(keyDir, ec);
    RemoveOldTempDirs(keyDir);
    MakeRoom(keyDir, ListSnapshotsIn(keyDir));

    const std::vector<Snapshot> previous = ListSnapshotsIn(keyDir);

    Snapshot snapshot;
    snapshot.archiveName = GetArchiveName();
    snapshot.originalPath = GetArchivePath();
    snapshot.kind = kind;
    snapshot.time = static_cast<s64>(std::time(nullptr));
    snapshot.saveNames = sBfsar.getSaveMetadata() ? 1 : 0;

    const fs::path finalDir = MakeSnapshotDir(keyDir, static_cast<std::time_t>(snapshot.time), kind);
    const fs::path tempDir = finalDir.string() + cTempSuffix;

    fs::create_directories(tempDir, ec);

    if (ec)
        return false;

    const std::string archivePath = (tempDir / snapshot.archiveName).string();

    *result = sBfsar.saveBackup(archivePath.c_str(), &snapshot.groupFiles);

    if (*result != Bfsar::BackupResult::Ok)
    {
        fs::remove_all(tempDir, ec);
        return false;
    }

    std::string previousDir;
    std::vector<Bfsar::BackupStream> previousStreams;

    for (const Snapshot& other : previous)
    {
        if (other.kind == Kind::Save)
            continue;

        previousDir = other.dir;
        previousStreams = other.streams;
        break;
    }

    LinkUnchangedGroupFiles(tempDir, previousDir, snapshot.groupFiles);

    if (!sBfsar.saveBackupStreams(tempDir.string(), previousDir, previousStreams, &snapshot.streams) ||
        !FinishSnapshot(tempDir, finalDir, &snapshot))
    {
        *result = Bfsar::BackupResult::WriteFailed;
        fs::remove_all(tempDir, ec);
        return false;
    }

    if (kind != Kind::Manual || gUnsavedChanges)
        WriteMarker(snapshot.dir);

    Rotate(keyDir);

    if (outDir)
        *outDir = snapshot.dir;

    return true;
}

bool HasCompleteArchive(const u8* file, u64 size)
{
    if (size < sizeof(nw::ut::BinaryFileHeader))
        return false;

    const u8* mark = file + offsetof(nw::ut::BinaryFileHeader, byteOrder);
    const u8* field = file + offsetof(nw::ut::BinaryFileHeader, fileSize);
    const bool isLittle = mark[0] == 0xFF && mark[1] == 0xFE;

    const u32 headerSize = isLittle ? static_cast<u32>(field[3]) << 24 | static_cast<u32>(field[2]) << 16 | static_cast<u32>(field[1]) << 8 | static_cast<u32>(field[0])
                                    : static_cast<u32>(field[0]) << 24 | static_cast<u32>(field[1]) << 16 | static_cast<u32>(field[2]) << 8 | static_cast<u32>(field[3]);

    return headerSize == size;
}

static bool HasCompleteArchiveFile(const Snapshot& snapshot)
{
    const fs::path path = fs::path(snapshot.dir) / snapshot.archiveName;

    if (HasSuffix(snapshot.archiveName, cCmpbinSuffix))
        return true;

    FILE* f = fopen(path.string().c_str(), "rb");

    if (!f)
        return false;

    u8 header[sizeof(nw::ut::BinaryFileHeader)];
    const bool headerRead = fread(header, 1, sizeof(header), f) == sizeof(header);

    fclose(f);

    std::error_code ec;
    const std::uintmax_t size = fs::file_size(path, ec);

    return headerRead && !ec && HasCompleteArchive(header, size);
}

static bool FindCompleteSnapshot(const fs::path& keyDir, Snapshot* out)
{
    for (const Snapshot& snapshot : ListSnapshotsIn(keyDir))
    {
        if (snapshot.kind == Kind::Save || !HasCompleteArchiveFile(snapshot))
            continue;

        *out = snapshot;
        return true;
    }

    return false;
}

static std::vector<Marker> ListDeadMarkers(std::vector<std::string>* usedKeys)
{
    const u32 ownPid = GetProcessId();

    std::vector<Marker> markers;
    std::error_code ec;

    for (fs::directory_iterator it(GetRootDir(), ec), end; !ec && it != end; it.increment(ec))
    {
        const std::string name = it->path().filename().string();

        if (!IsMarkerName(name))
            continue;

        const u32 pid = static_cast<u32>(std::strtoul(name.c_str() + std::strlen(cMarkerPrefix), nullptr, 10));
        const std::string dir = ReadMarkerDir(it->path());

        if (!dir.empty())
            usedKeys->push_back(fs::path(dir).parent_path().filename().string());

        if (pid != ownPid && IsProcessRunning(pid))
            continue;

        markers.push_back({it->path(), pid, dir});
    }

    return markers;
}

static bool FindRecovery(const Marker& marker, Recovery* recovery)
{
    fs::path markerPath = marker.path;
    std::error_code markerError;

    if (marker.pid == GetProcessId())
    {
        const std::string stem = marker.path.stem().string();

        markerPath = GetRootDir() / (stem + "-" + FormatTimeName(std::time(nullptr)) + cMarkerSuffix);
        fs::rename(marker.path, markerPath, markerError);

        if (markerError)
            return false;
    }

    recovery->markerPath = markerPath.string();

    if (marker.dir.empty() || !IsSnapshotDir(marker.dir))
    {
        fs::remove(markerPath, markerError);
        return false;
    }

    if (!ReadInfoFile(marker.dir, &recovery->snapshot) || !HasCompleteArchiveFile(recovery->snapshot))
    {
        recovery->snapshot = Snapshot();

        if (!FindCompleteSnapshot(fs::path(marker.dir).parent_path(), &recovery->snapshot))
        {
            fs::remove(markerPath, markerError);
            return false;
        }

        WriteMarkerFile(markerPath, recovery->snapshot.dir);
    }

    return true;
}

static void RemoveOldUntitledDirs(const std::vector<std::string>& usedKeys)
{
    const fs::file_time_type limit = fs::file_time_type::clock::now() - std::chrono::hours(24 * cUntitledKeepDays);
    std::vector<fs::path> old;
    std::error_code ec;

    for (fs::directory_iterator it(GetRootDir(), ec), end; !ec && it != end; it.increment(ec))
    {
        std::error_code entryError;
        const std::string name = it->path().filename().string();

        if (name.rfind(cUntitledPrefix, 0) != 0 || !it->is_directory(entryError) || entryError)
            continue;

        if (std::find(usedKeys.begin(), usedKeys.end(), name) != usedKeys.end())
            continue;

        const fs::file_time_type time = it->last_write_time(entryError);

        if (!entryError && time < limit)
            old.push_back(it->path());
    }

    for (const fs::path& dir : old)
        fs::remove_all(dir, ec);
}

void ScanRecoveries()
{
    std::vector<std::string> usedKeys;

    for (const Marker& marker : ListDeadMarkers(&usedKeys))
    {
        Recovery recovery;

        if (FindRecovery(marker, &recovery))
            sRecoveries.push_back(std::move(recovery));
    }

    std::sort(sRecoveries.begin(), sRecoveries.end(), [](const Recovery& a, const Recovery& b)
    {
        return a.snapshot.time < b.snapshot.time;
    });

    RemoveOldUntitledDirs(usedKeys);
}

bool HasRecovery()
{
    return !sRecoveries.empty();
}

const Recovery& GetNextRecovery()
{
    return sRecoveries.back();
}

void DropNextRecovery()
{
    sRecoveries.pop_back();
}

void AddRecovery(Recovery&& recovery)
{
    sRecoveries.push_back(std::move(recovery));
}

void RemoveRecoveryMarker(const Recovery& recovery)
{
    std::error_code ec;

    if (fs::path(recovery.markerPath) != GetMarkerPath())
        fs::remove(recovery.markerPath, ec);
}

void DiscardRecovery(const Recovery& recovery)
{
    const Snapshot& snapshot = recovery.snapshot;
    std::error_code ec;

    fs::remove(recovery.markerPath, ec);

    const fs::path keyDir = fs::path(snapshot.dir).parent_path();

    if (snapshot.originalPath.empty() && IsSnapshotDir(snapshot.dir) && keyDir.filename().string().rfind(cUntitledPrefix, 0) == 0)
        fs::remove_all(keyDir, ec);
}

static void CopyGroupFiles(const std::string& archivePath, const fs::path& destDir, Snapshot* snapshot)
{
    const fs::path sourceDir = fs::path(archivePath).parent_path();

    for (const std::string& path : sBfsar.getGroupFileSavePaths(archivePath.c_str()))
    {
        const std::string relativePath = fs::path(path).lexically_relative(sourceDir).generic_string();

        if (!IsSafeRelativePath(relativePath))
            continue;

        const fs::path dest = destDir / relativePath;
        std::error_code ec;

        fs::create_directories(dest.parent_path(), ec);
        fs::copy_file(path, dest, ec);

        if (!ec)
            snapshot->groupFiles.push_back(relativePath);
    }
}

void KeepPreviousVersion(const char* destPath)
{
    if (!sAutoBackupEnabled || !IsActive() || !sBfsar.isOpen())
        return;

    const std::string sourcePath = destPath ? NormalizePath(destPath) : GetArchivePath();

    if (sourcePath.empty())
        return;

    const fs::path source(sourcePath);
    std::error_code ec;

    const std::uintmax_t sourceSize = fs::file_size(source, ec);

    if (ec || sourceSize == 0)
        return;

    const fs::file_time_type sourceTime = fs::last_write_time(source, ec);

    if (ec)
        return;

    const s64 sourceStamp = static_cast<s64>(sourceTime.time_since_epoch().count());
    const fs::path keyDir = GetRootDir() / GetKeyForPath(sourcePath);

    fs::create_directories(keyDir, ec);
    RemoveOldTempDirs(keyDir);

    for (const Snapshot& other : ListSnapshotsIn(keyDir))
    {
        if (other.kind == Kind::Save && other.sourceStamp == sourceStamp)
            return;
    }

    Snapshot snapshot;
    snapshot.archiveName = source.filename().string();
    snapshot.originalPath = sourcePath;
    snapshot.kind = Kind::Save;
    snapshot.time = static_cast<s64>(std::time(nullptr));
    snapshot.sourceStamp = sourceStamp;

    const fs::path finalDir = MakeSnapshotDir(keyDir, static_cast<std::time_t>(snapshot.time), Kind::Save);
    const fs::path tempDir = finalDir.string() + cTempSuffix;

    fs::create_directories(tempDir, ec);

    if (ec)
        return;

    const fs::path dest = tempDir / snapshot.archiveName;

    fs::copy_file(source, dest, ec);

    if (ec || fs::file_size(dest, ec) != sourceSize || ec)
    {
        fs::remove_all(tempDir, ec);
        return;
    }

    fs::copy_file(source.string() + cNamesSuffix, dest.string() + cNamesSuffix, ec);
    ec.clear();

    CopyGroupFiles(sourcePath, tempDir, &snapshot);

    if (FinishSnapshot(tempDir, finalDir, &snapshot))
        Rotate(keyDir);
}

std::vector<Snapshot> ListSnapshots()
{
    if (!sBfsar.isOpen())
        return {};

    if (sBfsar.getFilePath().isEmpty() && sUntitledKey.empty())
        return {};

    return ListSnapshotsIn(GetRootDir() / GetArchiveKey());
}

std::string GetFolder()
{
    std::error_code ec;

    if (sBfsar.isOpen() && (!sBfsar.getFilePath().isEmpty() || !sUntitledKey.empty()))
    {
        const fs::path keyDir = GetRootDir() / GetArchiveKey();

        if (fs::is_directory(keyDir, ec))
            return keyDir.string();
    }

    return GetRootFolder();
}

std::string GetRootFolder()
{
    std::error_code ec;
    const fs::path rootDir = GetRootDir();

    return fs::is_directory(rootDir, ec) ? rootDir.string() : std::string();
}

void GetUsage(size_t* count, u64* size)
{
    *count = 0;
    *size = 0;

    std::error_code ec;

    for (fs::directory_iterator it(GetRootDir(), ec), end; !ec && it != end; it.increment(ec))
    {
        std::error_code entryError;

        if (!it->is_directory(entryError) || entryError)
            continue;

        for (const Snapshot& snapshot : ListSnapshotsIn(it->path()))
        {
            (*count)++;
            *size += snapshot.size;
        }
    }
}

}
