#include <ui/file/SoundArchiveDocument.h>

#include <ui/backup/BackupInternal.h>
#include <ui/file/File.h>
#include <ui/file/FileDialogs.h>
#include <ui/project/FileLists.h>
#include <ui/project/ItemViews.h>
#include <ui/Messages.h>
#include <ui/player/PlayerViews.h>
#include <ui/app/PopupMgr.h>
#include <ui/project/ProjectViews.h>
#include <ui/sound/SoundViews.h>

#include <core/Debug.h>
#include <browser/FileProbe.h>
#include <bfsar/model/Sound.h>
#include <bfsar/player/SoundPlayer.h>

#include <filesystem>

namespace fs = std::filesystem;

static constexpr u64 cArchiveMemoryFactor = 3;

Document* SoundArchiveDocument::Open(File* file, const sead::SafeString& path, const sead::SafeString* metadataSource)
{
    SoundArchiveDocument* document = new SoundArchiveDocument();

    if (document->openArchive(file, path, metadataSource))
        return document;

    delete document;
    return nullptr;
}

Document* SoundArchiveDocument::Create(ArchiveFormat format, ArchivePlatform platform)
{
    SoundArchiveDocument* document = new SoundArchiveDocument();
    document->archive().create(format, platform);

    return document;
}

SoundArchiveDocument::SoundArchiveDocument()
    : mArchive(new Bfsar())
{
}

SoundArchiveDocument::~SoundArchiveDocument()
{
    closeWindows();

    delete mArchive;
}

bool SoundArchiveDocument::openArchive(File* file, const sead::SafeString& path, const sead::SafeString* metadataSource)
{
    u8* data = file->data;
    file->data = nullptr;

    if (!mArchive->open(data, file->size, path, nullptr, metadataSource))
    {
        const sead::FormatFixedSafeString<1024> message(messages::archive::cCorruptFormat, GetInnerFileDisplayName(mArchive->getFormat(), InnerFileKind::SoundArchive), PopupMgr::instance()->getCorruptInfo().cstr());
        PopupMgr::instance()->addPopup({ message, nullptr });

        return false;
    }

    if (file->wasCmpbin)
        mArchive->setCmpbinPreferZstd(file->prefersZstd);

    return true;
}

Bfsar& SoundArchiveDocument::archive()
{
    SEAD_ASSERT(mArchive);

    return *mArchive;
}

Document::Kind SoundArchiveDocument::kind() const
{
    return Kind::SoundArchive;
}

const sead::SafeString& SoundArchiveDocument::filePath() const
{
    return mArchive ? mArchive->getFilePath() : mUnloadedPath;
}

const sead::SafeString& SoundArchiveDocument::loadedPath() const
{
    return mArchive ? mArchive->getLoadedArchivePath() : mUnloadedPath;
}

std::string SoundArchiveDocument::displayName() const
{
    return archiveName();
}

const char* SoundArchiveDocument::extension() const
{
    const ArchiveFormatInfo* info = getFormatInfo(mArchive->getFormat(), mArchive->getPlatform());

    return info ? info->extension : GetInnerFileExtension(ArchiveFormat::BFSAR, InnerFileKind::SoundArchive);
}

BrowserFileKind SoundArchiveDocument::fileKind() const
{
    return BrowserFileKind::SoundArchive;
}

UIType SoundArchiveDocument::defaultTab() const
{
    return UIType::ProjectInfo;
}

void SoundArchiveDocument::drawInfo(UIType tab)
{
    switch (tab)
    {
        case UIType::ProjectInfo:
            DrawProjectInfoUI();
            break;

        case UIType::AllSounds:
            DrawAllSoundsUI();
            break;

        case UIType::StreamSounds:
            DrawStreamSoundsUI();
            break;

        case UIType::WaveSounds:
            DrawWaveSoundsUI();
            break;

        case UIType::SequenceSounds:
            DrawSequenceSoundsUI();
            break;

        case UIType::AllSoundSets:
            DrawAllSoundSetsUI();
            break;

        case UIType::WaveSoundSets:
            DrawWaveSoundSetsUI();
            break;

        case UIType::SequenceSoundSets:
            DrawSequenceSoundSetsUI();
            break;

        case UIType::Banks:
            DrawBanksUI();
            break;

        case UIType::WaveArchives:
            DrawWaveArchivesUI();
            break;

        case UIType::Groups:
            DrawGroupsUI();
            break;

        case UIType::Players:
            DrawPlayersUI();
            break;

        case UIType::WaveFiles:
            DrawWaveFilesUI();
            break;

        case UIType::SequenceFiles:
            DrawSequenceFilesUI();
            break;

        case UIType::BankFiles:
            DrawBankFilesUI();
            break;

        case UIType::FileStatistics:
            DrawFileStatisticsUI();
            break;

        default:
            break;
    }
}

bool SoundArchiveDocument::isValidName(const sead::SafeString& name) const
{
    return Bfsar::validName(name);
}

bool SoundArchiveDocument::isUniqueName(const sead::SafeString& name, const Item* ignoreItem) const
{
    return mArchive->validateName(name, ignoreItem);
}

void SoundArchiveDocument::renumber(Item::List& list)
{
    Bfsar::updateList(list);
}

void SoundArchiveDocument::forEachItemList(const std::function<void(Item::List&)>& visit)
{
    visit(mArchive->getSoundList());
    visit(mArchive->getSoundSetList());
    visit(mArchive->getBankList());
    visit(mArchive->getWaveArchiveList());
    visit(mArchive->getGroupList());
    visit(mArchive->getPlayerList());
    visit(mArchive->getWaveFileList());
    visit(mArchive->getSequenceFileList());
    visit(mArchive->getBankFileList());
}

void SoundArchiveDocument::captureItemBaselines()
{
    mArchive->captureItemBaselines();
}

bool SoundArchiveDocument::isInfoModified() const
{
    return mArchive && mArchive->isOpen() && mArchive->isInfoModified();
}

bool SoundArchiveDocument::ownsItem(const Item& item) const
{
    return mArchive && item.getArchive() == mArchive;
}

Document::ParseContext SoundArchiveDocument::parseContext() const
{
    ParseContext context;
    context.endian = mArchive->getEndian();
    context.usesV3SoundInfo = mArchive->isV3Bfsar();

    return context;
}

u64 SoundArchiveDocument::estimateMemory() const
{
    std::error_code error;
    const u64 archiveSize = fs::file_size(loadedPath().cstr(), error);

    u64 total = error ? 0 : archiveSize * cArchiveMemoryFactor;
    sead::FixedSafeString<1024> streamPath;

    for (const Item* item : mArchive->getSoundList())
    {
        const Sound& sound = *static_cast<const Sound*>(item);

        if (sound.getSoundType() != Sound::SoundType::Strm || SoundPlayer::IsStreamFileNotLoaded(sound) || !mArchive->resolveStreamFilePath(sound, &streamPath))
            continue;

        const u64 streamSize = fs::file_size(streamPath.cstr(), error);

        if (!error)
            total += streamSize;
    }

    return total;
}

bool SoundArchiveDocument::isLoaded() const
{
    return mArchive != nullptr;
}

bool SoundArchiveDocument::canUnload() const
{
    return mArchive && mArchive->isOpen() && !mArchive->getFilePath().isEmpty();
}

void SoundArchiveDocument::unload()
{
    if (!mArchive)
        return;

    mUnloadedPath = mArchive->getFilePath();

    closeWindows();

    delete mArchive;
    mArchive = nullptr;
}

bool SoundArchiveDocument::reload()
{
    if (mArchive)
        return true;

    File file;

    if (const char* error = Load(mUnloadedPath, &file))
    {
        Free(&file);
        PopupMgr::instance()->addPopup({ error, nullptr });
        return false;
    }

    if (file.kind != Kind::SoundArchive)
    {
        Free(&file);
        PopupMgr::instance()->addPopup({ messages::archive::cNotArchive, nullptr });
        return false;
    }

    const char* error = nullptr;

    if (!Check(file, &error))
    {
        Free(&file);

        if (error)
            PopupMgr::instance()->addPopup({ error, nullptr });

        return false;
    }

    mArchive = new Bfsar();

    if (openArchive(&file, mUnloadedPath, nullptr))
        return true;

    Free(&file);

    delete mArchive;
    mArchive = nullptr;

    return false;
}

bool SoundArchiveDocument::prepareSave()
{
    return true;
}

bool SoundArchiveDocument::prepareSaveAs()
{
    return mArchive->isOpen() && mArchive->validate();
}

static void GetDefaultSavePath(const Bfsar& archive, const ArchiveFormatInfo& info, sead::BufferedSafeString* outPath)
{
    if (!archive.getFilePath().isEmpty())
    {
        outPath->copy(archive.getFilePath());
        return;
    }

    const sead::FormatFixedSafeString<64> untitledName(messages::archive::cUntitledFileNameFormat, info.extension);

    std::error_code error;
    const fs::path currentDir = fs::current_path(error);

    if (error)
        outPath->copy(untitledName);
    else
        outPath->copy((currentDir / untitledName.cstr()).string().c_str());
}

bool SoundArchiveDocument::chooseSaveAsPath(sead::BufferedSafeString* outPath) const
{
    const ArchiveFormatInfo* info = getFormatInfo(mArchive->getFormat(), mArchive->getPlatform());

    if (!info)
    {
        LOG_STR("SaveFileAs: invalid archive format info");
        return false;
    }

    const sead::FormatFixedSafeString<64> filterPattern("*.%s", info->extension);
    const sead::FormatFixedSafeString<128> filterName("%s (%s)", info->label, filterPattern.cstr());
    const sead::FormatFixedSafeString<32> cmpbinPattern("*.%s", cCmpbinExtension);
    const sead::FormatFixedSafeString<128> cmpbinName(messages::common::cCompressedFilterFormat, info->label, info->extension, cCmpbinExtension);

    FileFilter filters[] = {
        { filterName.cstr(), filterPattern.cstr() },
        { cmpbinName.cstr(), cmpbinPattern.cstr() }
    };

    const u32 filterCount = mArchive->isV3Bfsar() ? IM_ARRAYSIZE(filters) : 1;

    sead::FixedSafeString<512> defaultPath;
    GetDefaultSavePath(*mArchive, *info, &defaultPath);

    if (!SaveFileDialog(outPath, nullptr, filterCount, filters, info->extension, defaultPath.cstr()))
        return false;

    AddInnerExtension(outPath, info->extension, cCmpbinExtension);

    LOG_STR(outPath->cstr());

    return true;
}

SaveResult SoundArchiveDocument::save(const char** outError)
{
    *outError = nullptr;

    if (mArchive->isOpen() && !mArchive->hasFilePath())
        return SaveFileAs() ? SaveResult::Saved : SaveResult::Failed;

    return mArchive->save();
}

SaveResult SoundArchiveDocument::saveAs(const sead::SafeString& path, const char** outError)
{
    *outError = nullptr;

    const SaveResult result = mArchive->saveAs(path);

    if (result == SaveResult::Failed)
        LOG_STR("SaveFileAs failed to write archive");

    return result;
}

void SoundArchiveDocument::finishSave()
{
    NotifyDeletedGroupFiles();
}

s32 SoundArchiveDocument::getBackupNamesSetting() const
{
    return mArchive->getSaveMetadata() ? 1 : 0;
}

bool SoundArchiveDocument::writeBackup(const BackupWrite& write, backup::Snapshot* snapshot, Bfsar::BackupResult* outResult)
{
    const std::string archivePath = (fs::path(write.dir) / snapshot->archiveName).string();

    *outResult = mArchive->saveBackup(archivePath.c_str(), &snapshot->groupFiles);

    if (*outResult != Bfsar::BackupResult::Ok)
        return false;

    backup::LinkUnchangedGroupFiles(write.dir, write.previousDir, snapshot->groupFiles);

    return mArchive->saveBackupStreams(write.dir, write.previousDir, write.previousStreams, &snapshot->streams);
}

void SoundArchiveDocument::copyPreviousSidecars(const std::string& sourcePath, const std::string& destDir, backup::Snapshot* snapshot)
{
    const fs::path sourceDir = fs::path(sourcePath).parent_path();

    for (const std::string& path : mArchive->getGroupFileSavePaths(sourcePath.c_str()))
    {
        const std::string relativePath = fs::path(path).lexically_relative(sourceDir).generic_string();

        if (!backup::IsSafeRelativePath(relativePath))
            continue;

        const fs::path dest = fs::path(destDir) / relativePath;
        std::error_code error;

        fs::create_directories(dest.parent_path(), error);
        fs::copy_file(path, dest, error);

        if (!error)
            snapshot->groupFiles.push_back(relativePath);
    }
}

void SoundArchiveDocument::restoreSidecars(const backup::Snapshot& snapshot)
{
    const fs::path dir(snapshot.dir);

    for (const Bfsar::BackupStream& stream : snapshot.streams)
    {
        const std::string streamFile = (dir / stream.path).string();

        if (mArchive->restoreStreamFromFile(stream.path.c_str(), streamFile.c_str()))
            continue;

        const sead::FormatFixedSafeString<1024> message(messages::backup::cRestoreStreamFormat, stream.path.c_str());
        PopupMgr::instance()->addPopup({ message, nullptr });
    }
}

void SoundArchiveDocument::applyRestoredSnapshot(const backup::Snapshot& snapshot, bool untitled)
{
    if (snapshot.saveNames >= 0)
        mArchive->setSaveMetadata(snapshot.saveNames != 0);

    if (untitled)
        mArchive->clearFilePath();
}
