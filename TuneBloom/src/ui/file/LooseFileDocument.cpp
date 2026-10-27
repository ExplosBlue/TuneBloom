#include <ui/file/LooseFileDocument.h>

#include <ui/backup/Backup.h>
#include <ui/file/File.h>
#include <ui/file/FileDialogs.h>
#include <ui/Messages.h>

#include <bfsar/io/PackFile.h>

#include <filesystem>

namespace fs = std::filesystem;

const sead::SafeString& LooseFileDocument::loadedPath() const
{
    return mLoadedPath;
}

const char* LooseFileDocument::extension() const
{
    return GetInnerFileExtension(fileTarget().format, innerFileKind());
}

UIType LooseFileDocument::defaultTab() const
{
    return UIType::ProjectInfo;
}

bool LooseFileDocument::isValidName(const sead::SafeString& name) const
{
    return !name.isEmpty();
}

bool LooseFileDocument::isUniqueName(const sead::SafeString&, const Item*) const
{
    return true;
}

void LooseFileDocument::renumber(Item::List& list)
{
    Bfsar::updateList(list);
}

void LooseFileDocument::captureItemBaselines()
{
}

Document::ParseContext LooseFileDocument::parseContext() const
{
    ParseContext context = CurrentParseContext();
    context.endian = fileTarget().endian;

    return context;
}

u64 LooseFileDocument::estimateMemory() const
{
    return 0;
}

bool LooseFileDocument::isLoaded() const
{
    return true;
}

bool LooseFileDocument::canUnload() const
{
    return false;
}

void LooseFileDocument::unload()
{
}

bool LooseFileDocument::reload()
{
    return true;
}

bool LooseFileDocument::prepareSave()
{
    return true;
}

bool LooseFileDocument::prepareSaveAs()
{
    return true;
}

bool LooseFileDocument::chooseSaveAsPath(sead::BufferedSafeString* outPath) const
{
    const char* fileExtension = extension();
    const char* displayName = GetInnerFileDisplayName(fileTarget().format, innerFileKind());

    const sead::FormatFixedSafeString<32> pattern("*.%s", fileExtension);
    const sead::FormatFixedSafeString<64> filterName("%s (%s)", displayName, pattern.cstr());

    FileFilter filters[] = {
        { filterName.cstr(), pattern.cstr() }
    };

    const fs::path documentPath(filePath().isEmpty() ? this->displayName() : std::string(filePath().cstr()));
    const std::string defaultPath = (fs::path(packfile::GetDiskPath(documentPath.string())).parent_path() / documentPath.filename()).string();

    return SaveFileDialog(outPath, nullptr, IM_ARRAYSIZE(filters), filters, fileExtension, defaultPath.c_str());
}

sead::FixedSafeString<64> LooseFileDocument::MakeUntitledName(const InnerFileTarget& target, InnerFileKind kind)
{
    return sead::FormatFixedSafeString<64>(messages::archive::cUntitledFileNameFormat, GetInnerFileExtension(target.format, kind));
}

SaveResult LooseFileDocument::reportSave(SaveResult result, const char** outError)
{
    *outError = result == SaveResult::Saved ? nullptr : mSaveError.cstr();

    return result;
}

SaveResult LooseFileDocument::saveUntitled(const char** outError)
{
    *outError = nullptr;

    return SaveFileAs() ? SaveResult::Saved : SaveResult::Failed;
}

void LooseFileDocument::finishSave()
{
    mLoadedPath = filePath();
}

s32 LooseFileDocument::getBackupNamesSetting() const
{
    return -1;
}

bool LooseFileDocument::writeBackup(const BackupWrite& write, backup::Snapshot* snapshot, Bfsar::BackupResult* outResult)
{
    const std::string path = (fs::path(write.dir) / snapshot->archiveName).string();
    const bool written = writeTo(sead::FixedSafeString<512>(path.c_str()));

    *outResult = written ? Bfsar::BackupResult::Ok : Bfsar::BackupResult::WriteFailed;

    return written;
}

void LooseFileDocument::copyPreviousSidecars(const std::string&, const std::string&, backup::Snapshot*)
{
}

void LooseFileDocument::restoreSidecars(const backup::Snapshot&)
{
}

void LooseFileDocument::applyRestoredSnapshot(const backup::Snapshot&, bool)
{
}
