#include <ui/file/LooseFileDocument.h>

#include <ui/app/PopupMgr.h>
#include <ui/backup/Backup.h>
#include <ui/Messages.h>

#include <filesystem>

namespace fs = std::filesystem;

const sead::SafeString& LooseFileDocument::loadedPath() const
{
    return mLoadedPath;
}

const char* LooseFileDocument::extension() const
{
    return fileFormat().extension;
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
    context.endian = fileFormat().endian;

    return context;
}

bool LooseFileDocument::chooseSaveAsPath(sead::BufferedSafeString* outPath) const
{
    const LooseFileFormat format = fileFormat();

    return showSaveAsDialog(outPath, format.name, format.extension, nullptr);
}

Document* LooseFileDocument::OpenAs(LooseFileDocument* document, File* file, const sead::SafeString& path)
{
    sead::FixedSafeString<512> error;

    const bool opened = document->openFile(*file, path, &error);
    Free(file);

    if (opened)
    {
        document->mLoadedPath = path;
        return document;
    }

    PopupMgr::instance()->addPopup({ error, nullptr });

    delete document;
    return nullptr;
}

sead::FixedSafeString<64> LooseFileDocument::MakeUntitledName(const char* extension)
{
    return sead::FormatFixedSafeString<64>(messages::archive::cUntitledFileNameFormat, extension);
}

SaveResult LooseFileDocument::reportSave(SaveResult result, const char** outError)
{
    *outError = result == SaveResult::Saved ? nullptr : mSaveError.cstr();

    return result;
}

void LooseFileDocument::finishSave()
{
    mLoadedPath = filePath();
}

bool LooseFileDocument::writeBackup(const BackupWrite& write, backup::Snapshot* snapshot, Bfsar::BackupResult* outResult)
{
    const std::string path = (fs::path(write.dir) / snapshot->archiveName).string();
    const bool written = writeTo(sead::FixedSafeString<512>(path.c_str()));

    *outResult = written ? Bfsar::BackupResult::Ok : Bfsar::BackupResult::WriteFailed;

    return written;
}
