#include <bfsar/archive/Bfsar.h>

#include <bfsar/files/BfgrpFile.h>
#include <bfsar/archive/BfsarSaveContext.h>
#include <bfsar/io/FileIo.h>
#include <bfsar/archive/GroupFilePaths.h>
#include <bfsar/archive/SaveReport.h>

#include <ui/settings/Config.h>
#include <ui/Messages.h>
#include <core/TestHooks.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

static constexpr const char* cStagedFileSuffix   = ".grptmp";
static constexpr const char* cBackupFileSuffix   = ".grpbak";
static constexpr const char* cExportArchiveName  = "tunebloom_group_export_";

static bool IsBelowFolder(const std::filesystem::path& relativePath)
{
    if (relativePath.empty() || relativePath.is_absolute())
        return false;

    for (const std::filesystem::path& part : relativePath)
    {
        if (part == "..")
            return false;
    }

    return true;
}

static std::string GetGroupFileName(const Group& group, const char* extension)
{
    return std::string(group.getName().cstr()) + "." + extension;
}

static std::string GetGroupFileSavePath(const Group& group, const std::filesystem::path& archiveDir, const std::filesystem::path& loadedDir, bool isSameArchive, const char* extension)
{
    if (group.getExternalFilePath().empty())
        return (archiveDir / GetGroupFileName(group, extension)).string();

    if (isSameArchive)
        return group.getExternalFilePath();

    const std::filesystem::path loadedPath(group.getExternalFilePath());
    const std::filesystem::path relativePath = loadedPath.lexically_relative(loadedDir);

    if (IsBelowFolder(relativePath))
        return (archiveDir / relativePath).string();

    return (archiveDir / loadedPath.filename()).string();
}

static std::string GetPathKey(const std::string& path)
{
    std::string key = std::filesystem::path(path).lexically_normal().generic_string();

    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    return key;
}

std::vector<Bfsar::GroupFileSave> Bfsar::planGroupFileSaves_(const sead::SafeString& archivePath)
{
    std::vector<GroupFileSave> groupFileSaves;
    std::vector<std::string> usedPaths;

    const char* extension = GetInnerFileExtension(mFormat, InnerFileKind::Group);
    const std::filesystem::path archiveDir = std::filesystem::path(archivePath.cstr()).parent_path();
    const std::filesystem::path loadedDir = mLoadedArchivePath ? std::filesystem::path(mLoadedArchivePath->cstr()).parent_path() : std::filesystem::path();
    const bool isSameArchive = mLoadedArchivePath && archivePath == *mLoadedArchivePath;

    for (Item* item : mGroupList)
    {
        Group* group = static_cast<Group*>(item);

        if (group->getOutputType() != Group::OutputType::External)
            continue;

        GroupFileSave save;
        save.group = group;
        save.path = GetGroupFileSavePath(*group, archiveDir, loadedDir, isSameArchive, extension);

        if (std::find(usedPaths.begin(), usedPaths.end(), GetPathKey(save.path)) != usedPaths.end())
            save.path = (archiveDir / GetGroupFileName(*group, extension)).string();

        save.stagedPath = save.path + cStagedFileSuffix;

        usedPaths.push_back(GetPathKey(save.path));
        groupFileSaves.push_back(std::move(save));
    }

    return groupFileSaves;
}

std::vector<std::string> Bfsar::getGroupFileSavePaths(const sead::SafeString& archivePath)
{
    std::vector<std::string> paths;

    for (const GroupFileSave& save : planGroupFileSaves_(archivePath))
        paths.push_back(save.path);

    if (!mLoadedArchivePath || !(archivePath == *mLoadedArchivePath))
        return paths;

    for (const std::string& loadedPath : mLoadedGroupFilePaths)
    {
        if (std::find(paths.begin(), paths.end(), loadedPath) == paths.end())
            paths.push_back(loadedPath);
    }

    return paths;
}

std::vector<Bfsar::GroupFileSave> Bfsar::planBackupGroupFiles_(const sead::SafeString& backupPath, std::vector<std::string>* outFiles)
{
    std::vector<GroupFileSave> groupFileSaves = planGroupFileSaves_(backupPath);
    const std::filesystem::path backupDir = std::filesystem::path(backupPath.cstr()).parent_path();

    for (GroupFileSave& save : groupFileSaves)
    {
        save.stagedPath = save.path;

        if (outFiles)
            outFiles->push_back(std::filesystem::path(save.path).lexically_relative(backupDir).generic_string());
    }

    return groupFileSaves;
}

void Bfsar::moveGroupFilePaths_(const sead::SafeString& fromArchivePath, const sead::SafeString& toArchivePath)
{
    const std::filesystem::path fromDir = std::filesystem::path(fromArchivePath.cstr()).parent_path();
    const std::filesystem::path toDir = std::filesystem::path(toArchivePath.cstr()).parent_path();

    for (Item* item : mGroupList)
    {
        Group* group = static_cast<Group*>(item);
        const std::filesystem::path relativePath = std::filesystem::path(group->mExternalFilePath).lexically_relative(fromDir);

        if (IsBelowFolder(relativePath))
            group->mExternalFilePath = (toDir / relativePath).string();
    }
}

std::string Bfsar::getGroupFileSavePath(const Group* group) const
{
    const std::filesystem::path archiveDir = std::filesystem::path(getFilePath().cstr()).parent_path();

    return GetGroupFileSavePath(*group, archiveDir, archiveDir, true, GetInnerFileExtension(mFormat, InnerFileKind::Group));
}

u32 Bfsar::getGroupExtraFileCount(const Group* group) const
{
    return static_cast<u32>(getExtraFileOwners_(group).size());
}

bool Bfsar::usesStoredGroupFilePaths_() const
{
    return (!mCliMode || testhooks::IsGroupFileStoreOn()) && !testhooks::IsEnabled();
}

void Bfsar::setGroupFilePath(const sead::SafeString& groupName, const sead::SafeString& path, bool keep) const
{
    groupfilepaths::Select(getFilePath(), groupName, path.cstr(), keep && usesStoredGroupFilePaths_());
}

void Bfsar::rememberGroupFilePath_(const Group* group) const
{
    if (!mFilePath)
        return;

    const std::filesystem::path path(group->getExternalFilePath());
    const std::filesystem::path archiveDir = std::filesystem::path(getFilePath().cstr()).parent_path();
    const bool isFoundByName = path.stem() == std::filesystem::path(group->getName().cstr()) && IsBelowFolder(path.lexically_relative(archiveDir));

    if (!isFoundByName)
        groupfilepaths::Remember(getFilePath(), group->getName(), group->getExternalFilePath(), usesStoredGroupFilePaths_());
}

static std::string MakeExportArchivePath()
{
    std::error_code error;
    const std::filesystem::path tempDir = std::filesystem::temp_directory_path(error);
    const s64 stamp = std::chrono::steady_clock::now().time_since_epoch().count();

    return error ? std::string() : (tempDir / (cExportArchiveName + std::to_string(stamp) + cStagedFileSuffix)).string();
}

static bool IsSameFile(const std::string& pathA, const std::string& pathB)
{
    std::error_code error;

    return !pathA.empty() && !pathB.empty() && std::filesystem::equivalent(pathA, pathB, error) && !error;
}

bool Bfsar::exportGroupFile(Group* group, const sead::SafeString& path)
{
    if (!mOpen || group->getOutputType() != Group::OutputType::External)
        return false;

    if (IsSameFile(path.cstr(), group->getExternalFilePath()))
    {
        ReportArchiveError(mCliMode, {messages::archive::cGroupFileExportOwnFile, group});
        return false;
    }

    recompileInvalidSequences_();

    if (!validate_())
        return false;

    GroupFileSave save;
    save.group = group;
    save.path = path.cstr();
    save.stagedPath = save.path + cStagedFileSuffix;

    const std::string archivePath = MakeExportArchivePath();
    sead::FileHandle handle;

    if (archivePath.empty() || archivePath.size() > static_cast<size_t>(fileio::cPathLengthMax) || !fileio::OpenForWrite(&handle, archivePath.c_str()))
    {
        reportGroupFileWriteFailed_(save);
        return false;
    }

    syncOpusStreamLoops_();

    const bool written = save_(handle, { save }) && handle.tryClose();

    fileio::RemoveFile(archivePath.c_str());

    if (written && fileio::RenameOver(save.stagedPath.c_str(), save.path.c_str()))
        return true;

    fileio::RemoveFile(save.stagedPath.c_str());
    reportGroupFileWriteFailed_(save);

    return false;
}

static const BfgrpFile* FindExternalGroupFile(const BfsarSaveContext& ctx, const Group* group)
{
    for (const auto& [fileGroup, groupFile] : ctx.externalGroupFiles)
    {
        if (fileGroup == group)
            return groupFile;
    }

    return nullptr;
}

static bool WriteGroupFile(const BfsarSaveContext& ctx, const Group* group, const BfgrpFile* groupFile, const std::string& path, sead::Endian::Types endian)
{
    if (path.size() > static_cast<size_t>(fileio::cPathLengthMax))
        return false;

    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), error);

    sead::FileHandle handle;

    if (!fileio::OpenForWrite(&handle, path.c_str()))
        return false;

    CheckedFileStreamSrc streamSrc(&handle);
    sead::WriteStream stream(&streamSrc, sead::Stream::Modes::eBinary);
    stream.setBinaryEndian(endian);

    groupFile->prepare(group, ctx);
    groupFile->write(&handle, &stream, endian, true);

    return !streamSrc.hasFailed() && handle.tryClose();
}

void Bfsar::reportGroupFileWriteFailed_(const GroupFileSave& save) const
{
    const std::string fileName = std::filesystem::path(save.path).filename().string();

    ReportArchiveError(mCliMode, {sead::FormatFixedSafeString<512>(messages::archive::cGroupFileWriteFailedFormat, fileName.c_str()), save.group});
}

bool Bfsar::reportUnwrittenGroupFile_(const std::vector<GroupFileSave>& groupFileSaves) const
{
    for (const GroupFileSave& save : groupFileSaves)
    {
        if (save.group != mUnwrittenGroupFile)
            continue;

        reportGroupFileWriteFailed_(save);

        return true;
    }

    return false;
}

bool Bfsar::writeExternalGroupFiles_(BfsarSaveContext& ctx, const std::vector<GroupFileSave>& groupFileSaves)
{
    mUnwrittenGroupFile = nullptr;

    for (const GroupFileSave& save : groupFileSaves)
    {
        const BfgrpFile* groupFile = FindExternalGroupFile(ctx, save.group);

        if (groupFile && WriteGroupFile(ctx, save.group, groupFile, save.stagedPath, mEndian))
            continue;

        mUnwrittenGroupFile = save.group;

        return false;
    }

    return true;
}

void Bfsar::removeStagedGroupFiles_(const std::vector<GroupFileSave>& groupFileSaves) const
{
    for (const GroupFileSave& save : groupFileSaves)
        fileio::RemoveFile(save.stagedPath.c_str());
}

static bool CopyOverFileInUse(const std::string& sourcePath, const std::string& destPath)
{
    std::vector<u8> bytes;

    return fileio::ReadWholeFile(sourcePath.c_str(), &bytes) && fileio::WriteWholeFile(destPath.c_str(), bytes.data(), bytes.size()) == fileio::WriteResult::Ok;
}

static bool ReplaceGroupFile(const std::string& stagedPath, const std::string& path, std::string* outBackupPath)
{
    std::error_code error;

    if (std::filesystem::exists(path, error))
    {
        *outBackupPath = path + cBackupFileSuffix;

        if (!fileio::CopyFileOver(path.c_str(), outBackupPath->c_str()))
            return false;
    }

    return fileio::CopyFileOver(stagedPath.c_str(), path.c_str()) || (sForceSaveWhenInUse && CopyOverFileInUse(stagedPath, path));
}

bool Bfsar::replaceGroupFiles_(std::vector<GroupFileSave>& groupFileSaves)
{
    for (GroupFileSave& save : groupFileSaves)
    {
        if (fileio::HasSameBytes(save.stagedPath.c_str(), save.path.c_str()))
            continue;

        if (ReplaceGroupFile(save.stagedPath, save.path, &save.backupPath))
        {
            save.isReplaced = true;
            continue;
        }

        fileio::RemoveFile(save.backupPath.c_str());
        save.backupPath.clear();

        rollBackGroupFiles_(groupFileSaves);
        reportGroupFileWriteFailed_(save);

        return false;
    }

    return true;
}

void Bfsar::rollBackGroupFiles_(std::vector<GroupFileSave>& groupFileSaves) const
{
    for (GroupFileSave& save : groupFileSaves)
    {
        if (!save.isReplaced)
            continue;

        if (save.backupPath.empty())
            fileio::RemoveFile(save.path.c_str());
        else if (!fileio::RenameOver(save.backupPath.c_str(), save.path.c_str()))
            CopyOverFileInUse(save.backupPath, save.path);

        fileio::RemoveFile(save.backupPath.c_str());

        save.backupPath.clear();
        save.isReplaced = false;
    }
}

void Bfsar::finishGroupFileSaves_(const std::vector<GroupFileSave>& groupFileSaves)
{
    for (const GroupFileSave& save : groupFileSaves)
    {
        fileio::RemoveFile(save.stagedPath.c_str());
        fileio::RemoveFile(save.backupPath.c_str());

        save.group->mExternalFilePath = save.path;
        save.group->mExternalFileState = Group::ExternalFileState::Loaded;

        rememberGroupFilePath_(save.group);
    }

    updateLoadedGroupFiles_(groupFileSaves);
}

static bool HasPath(const std::vector<std::string>& paths, const std::string& path)
{
    for (const std::string& other : paths)
    {
        if (GetPathKey(other) == GetPathKey(path))
            return true;
    }

    return false;
}

void Bfsar::updateLoadedGroupFiles_(const std::vector<GroupFileSave>& groupFileSaves)
{
    const bool isSameArchive = mLoadedArchivePath && getFilePath() == *mLoadedArchivePath;
    std::vector<std::string> paths;

    for (const GroupFileSave& save : groupFileSaves)
        paths.push_back(save.path);

    for (const std::string& loadedPath : mLoadedGroupFilePaths)
    {
        if (!isSameArchive || HasPath(paths, loadedPath))
            continue;

        if (!sDeleteUnusedGroupFiles)
        {
            paths.push_back(loadedPath);
            continue;
        }

        fileio::RemoveFile(loadedPath.c_str());
        mDeletedGroupFiles.push_back(loadedPath);
    }

    mLoadedGroupFilePaths = paths;
}

std::vector<std::string> Bfsar::takeDeletedGroupFiles()
{
    std::vector<std::string> deleted;

    deleted.swap(mDeletedGroupFiles);

    return deleted;
}
