#include <bfsar/archive/Bfsar.h>

#include <core/Debug.h>

#include <bfsar/archive/Bea.h>
#include <bfsar/archive/BfsarLayoutCheck.h>
#include <bfsar/archive/Cmpbin.h>
#include <bfsar/io/FileIo.h>
#include <bfsar/io/PackFile.h>
#include <bfsar/archive/SaveReport.h>
#include <bfsar/archive/SaveSettings.h>

#include <bfsar/model/ModelEvents.h>
#include <messages/Messages.h>
#include <bfsar/model/MessageLog.h>
#include <core/TestHooks.h>

#include <snd/SoundThread.h>

#include <libs/md5.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

bool sSaveMetadataDefault = cSaveMetadataDefault;
bool sForceSaveWhenInUse = cForceSaveInUseDefault;
bool sDeleteUnusedGroupFiles = cDeleteUnusedGroupFilesDefault;

static void FreeLoadedFiles(std::vector<u8*>& buffers)
{
    for (u8* buffer : buffers)
        delete[] buffer;

    buffers.clear();
}

Bfsar::Bfsar()
    : mOpen(false)
    , mFormat(ArchiveFormat::BFSAR)
    , mPlatform(ArchivePlatform::CAFE)
    , mFilePath(nullptr)

    , mEndian(sead::Endian::eBig)
    , mVersion(0x00010000)
    , mIncludeStringTable(true)
    , mSaveMetadata(true)
    , mSoundArchivePlayerInfo()

    , mSoundList()

    , mSoundSetList()

    , mBankList()

    , mWaveArchiveList()

    , mGroupList()

    , mPlayerList()

    , mWaveFileList()

    , mSequenceFileList()

    , mBankFileList()

    , mGenWaveArchiveList()
{
    mSoundList.setArchive(this);
    mSoundSetList.setArchive(this);
    mBankList.setArchive(this);
    mWaveArchiveList.setArchive(this);
    mGroupList.setArchive(this);
    mPlayerList.setArchive(this);
    mWaveFileList.setArchive(this);
    mSequenceFileList.setArchive(this);
    mBankFileList.setArchive(this);
    mGenWaveArchiveList.setArchive(this);
}

Bfsar::~Bfsar()
{
    close();
}

ArchivePlatform getDefaultPlatformForFormat(ArchiveFormat format)
{
    for (const auto &info : cFormatTable)
        if (info.format == format)
            return info.platform;

    return ArchivePlatform::CAFE;
}

void Bfsar::create(ArchiveFormat format)
{
    create(format, getDefaultPlatformForFormat(format));
}

InnerFileTarget Bfsar::GetNewFileTarget(ArchiveFormat format, ArchivePlatform platform, InnerFileKind kind)
{
    Bfsar archive;
    archive.create(format, platform);

    return kind == InnerFileKind::Stream ? archive.getStreamFileTarget() : archive.getWaveFileTarget();
}

void Bfsar::create(ArchiveFormat format, ArchivePlatform platform)
{
    LOG_FUNC();
    LOG_U32("format", (u32)format);
    LOG_U32("platform", (u32)platform);
    close();

    mFormat = format;
    mPlatform = platform;

    const ArchiveFormatInfo *info = getFormatInfo(mFormat, mPlatform);

    if (!info)
    {
        LOG("Bfsar::create: unsupported format/platform pair");
        return;
    }

    mEndian = info->endian;
    mVersion = info->defaultVersion;

    mIncludeStringTable = true;
    mSaveMetadata = sSaveMetadataDefault;
    mSoundArchivePlayerInfo.sequenceSoundMax = 64;
    mSoundArchivePlayerInfo.sequenceTrackMax = 64;
    mSoundArchivePlayerInfo.streamSoundMax = 4;
    mSoundArchivePlayerInfo.streamTrackMax = 4;
    mSoundArchivePlayerInfo.streamChannelMax = 8;
    mSoundArchivePlayerInfo.waveSoundMax = 64;
    mSoundArchivePlayerInfo.waveTrackMax = 64;
    mSoundArchivePlayerInfo.streamBufferTimes = 1;
    mSoundArchivePlayerInfo.options = 0;

    mOpen = true;
}

bool Bfsar::open(u8* bfsarFile, u32 bfsarSize, const sead::SafeString& filePath, sead::Heap* heap, const sead::SafeString* metadataSource)
{
    LOG_FUNC();
    LOG_HEX("magic", bfsarFile, 4);
    LOG_STR(filePath.cstr());

    close();
    MessageLog::instance()->setCorruptInfo("");

    // Detect format from magic
    if (sead::MemUtil::compare(bfsarFile, "CSAR", 4) == 0)
        mFormat = ArchiveFormat::BCSAR;
    else if (sead::MemUtil::compare(bfsarFile, "FSAR", 4) == 0)
        mFormat = ArchiveFormat::BFSAR;
    else
    {
        delete[] bfsarFile;
        return false;
    }

    mPlatform = getDefaultPlatformForFormat(mFormat);

    if (!CheckSoundArchiveHeader(bfsarFile, bfsarSize))
    {
        delete[] bfsarFile;
        return false;
    }

    bool success;
    {
        nw::snd::MemorySoundArchive soundArchive;
        if (!soundArchive.Initialize(bfsarFile))
        {
            delete[] bfsarFile;
            return false;
        }

        mFilePath = new(heap) sead::HeapSafeString(heap, filePath);
        mLoadedArchivePath = new (heap) sead::HeapSafeString(heap, filePath);

        soundArchive.mDataSize = bfsarSize;
        success = open_(soundArchive, bfsarSize, heap, metadataSource ? *metadataSource : filePath);

        if (metadataSource)
            moveGroupFilePaths_(*metadataSource, filePath);

        soundArchive.mExternalGroups.clear();

        FreeLoadedFiles(mExternalGroupBuffers);
    }

    delete[] bfsarFile;

    if (success)
    {
        modelevents::BeginLoadStage(modelevents::LoadStage::Names);

        const sead::SafeString& namesSource = metadataSource ? *metadataSource : filePath;

        mSaveMetadata = loadMetadata_(namesSource) || sSaveMetadataDefault;
    }

    MessageLog::instance()->setCurrentProcessItem(nullptr);

    mOpen = true;

    return success;
}

static bool ForceOverwriteFile_(const sead::SafeString &stagedPath, const sead::SafeString &destPath)
{
    std::vector<u8> raw;

    if (!fileio::ReadWholeFile(stagedPath.cstr(), &raw))
        return false;

    if (fileio::WriteWholeFile(packfile::GetDiskPath(destPath.cstr()).c_str(), raw.data(), raw.size()) != fileio::WriteResult::Ok)
        return false;

    fileio::RemoveFile(stagedPath.cstr());

    return true;
}

void ReportArchiveError(bool toStderr, const MessageLog::PopupInfo& info)
{
    MessageLog::instance()->addPopup(info);

    if (!toStderr)
        return;

    const Item* item = info.super ? info.super : info.item;

    if (item)
        fprintf(stderr, "error: %s: %s\n", item->getFormattedName().cstr(), info.text.cstr());
    else
        fprintf(stderr, "error: %s\n", info.text.cstr());
}

void Bfsar::recompileInvalidSequences_()
{
    for (Item* item : mSequenceFileList)
    {
        static_cast<SequenceFile*>(item)->recompileIfInvalid_();
    }
}

SaveResult Bfsar::save()
{
    LOG_FUNC();

    if (!mOpen || !mFilePath || testhooks::IsEnabled())
        return SaveResult::Failed;

    recompileInvalidSequences_();

    if (!validate())
        return SaveResult::Failed;

    return saveArchiveFile_(*mFilePath) ? SaveResult::Saved : SaveResult::Failed;
}

static bool HasPathExtension(const sead::SafeString &path, const char *extension)
{
    return HasFileExtension(path.cstr(), static_cast<size_t>(path.calcLength()), extension);
}

static bool IsWrappedArchivePath(const sead::SafeString &path)
{
    return HasPathExtension(path, cCmpbinExtension) || HasPathExtension(path, cBeaExtension) || packfile::IsPackPath(path.cstr());
}

static bool VerifyArchiveFile_(const sead::SafeString &path, const char *magic)
{
    FILE *f = fopen(path.cstr(), "rb");

    if (!f)
        return false;

    u8 header[sizeof(nw::ut::BinaryFileHeader)];
    const bool headerRead = fread(header, 1, sizeof(header), f) == sizeof(header);

    fseek(f, 0, SEEK_END);
    const long fileSize = ftell(f);
    fclose(f);

    if (!headerRead || fileSize <= 0 || std::memcmp(header, magic, 4) != 0)
        return false;

    return ReadDeclaredFileSize(header, sizeof(header)) == static_cast<u32>(fileSize);
}

enum class WrapResult
{
    Ok,
    ReadFailed,
    CompressFailed,
    PackFailed,
    SarcFailed,
    OpenFailed,
    WriteFailed
};

static WrapResult PackArchive(const std::vector<u8> &raw, const sead::SafeString &destPath, bool preferZstd, const bea::Source &beaSource, std::vector<u8> *out)
{
    std::string packPath;
    std::string innerName;

    if (packfile::Split(destPath.cstr(), &packPath, &innerName))
        return packfile::MakePackWithFile(packPath, innerName, raw.data(), raw.size(), out) ? WrapResult::Ok : WrapResult::SarcFailed;

    if (HasPathExtension(destPath, cBeaExtension))
        return beaSource.isSet() && bea::ReplaceAsset(beaSource, raw.data(), raw.size(), out) ? WrapResult::Ok : WrapResult::PackFailed;

    const cmpbin::Codec codec = preferZstd ? cmpbin::Codec::Zstd : cmpbin::Codec::Lz4Frame;

    return cmpbin::Compress(raw.data(), raw.size(), codec, *out) && !out->empty() ? WrapResult::Ok : WrapResult::CompressFailed;
}

static WrapResult WrapToTempFile(const sead::SafeString &sourcePath, const sead::SafeString &destPath, bool preferZstd, const bea::Source &beaSource, const sead::SafeString &tempPath)
{
    std::vector<u8> raw;

    if (!fileio::ReadWholeFile(sourcePath.cstr(), &raw))
        return WrapResult::ReadFailed;

    std::vector<u8> wrapped;
    const WrapResult packed = PackArchive(raw, destPath, preferZstd, beaSource, &wrapped);

    if (packed != WrapResult::Ok)
        return packed;

    const fileio::WriteResult written = fileio::WriteWholeFile(tempPath.cstr(), wrapped.data(), wrapped.size());

    if (written == fileio::WriteResult::OpenFailed)
        return WrapResult::OpenFailed;

    if (written != fileio::WriteResult::Ok)
    {
        fileio::RemoveFile(tempPath.cstr());
        return WrapResult::WriteFailed;
    }

    return WrapResult::Ok;
}

static const char* GetWrapErrorText(WrapResult result)
{
    switch (result)
    {
        case WrapResult::ReadFailed:
            return messages::archive::cCmpbinTempReadFailed;

        case WrapResult::CompressFailed:
            return messages::archive::cCmpbinCompressFailed;

        case WrapResult::PackFailed:
            return messages::archive::cBeaPackFailed;

        case WrapResult::SarcFailed:
            return messages::resource::cPackWriteFailed;

        case WrapResult::OpenFailed:
            return messages::archive::cCmpbinOpenFailed;

        default:
            return messages::archive::cSaveWriteFailed;
    }
}

bool Bfsar::saveArchiveFile_(const sead::SafeString &path)
{
    if (path.calcLength() > fileio::cPathLengthMax)
    {
        ReportArchiveError(mCliMode, {messages::archive::cSaveOpenFailed});
        return false;
    }

    if (HasPathExtension(path, cBeaExtension) && !mBeaSource.isSet())
    {
        ReportArchiveError(mCliMode, {messages::archive::cBeaNoSource});
        return false;
    }

    const std::string diskPath = packfile::GetDiskPath(path.cstr());

    sead::FixedSafeString<560> tmpPath;
    tmpPath.format("%s.savetmp", diskPath.c_str());

    std::vector<GroupFileSave> groupFileSaves = planGroupFileSaves_(path);
    std::vector<SkippedStream> skippedStreams;
    bool written = false;

    {
        sead::FileHandle handle;

        if (!fileio::OpenForWrite(&handle, tmpPath))
        {
            ReportArchiveError(mCliMode, {messages::archive::cSaveOpenFailed});
            return false;
        }

        syncOpusStreamLoops_();

        written = save_(handle, groupFileSaves, &skippedStreams) && handle.tryClose();
    }

    if (!written || !VerifyArchiveFile_(tmpPath, getArchiveMagic()))
    {
        fileio::RemoveFile(tmpPath.cstr());
        removeStagedGroupFiles_(groupFileSaves);

        if (!reportUnwrittenGroupFile_(groupFileSaves))
            ReportArchiveError(mCliMode, {messages::archive::cSaveWriteFailed});

        return false;
    }

    saveStreamFiles_(std::move(skippedStreams));

    sead::FixedSafeString<560> stagedPath = tmpPath;

    if (IsWrappedArchivePath(path))
    {
        stagedPath.format("%s.cmptmp", diskPath.c_str());

        const WrapResult wrapped = WrapToTempFile(tmpPath, path, mCmpbinPreferZstd, mBeaSource, stagedPath);

        fileio::RemoveFile(tmpPath.cstr());

        if (wrapped != WrapResult::Ok)
        {
            removeStagedGroupFiles_(groupFileSaves);
            ReportArchiveError(mCliMode, {GetWrapErrorText(wrapped)});
            return false;
        }
    }

    if (!replaceGroupFiles_(groupFileSaves))
    {
        fileio::RemoveFile(stagedPath.cstr());
        removeStagedGroupFiles_(groupFileSaves);
        return false;
    }

    if (!finalizeSavedFile_(stagedPath, path))
    {
        rollBackGroupFiles_(groupFileSaves);
        mPendingGroupFileSaves = groupFileSaves;
        return false;
    }

    finishGroupFileSaves_(groupFileSaves);
    saveMetadata_(path, true);

    return true;
}

bool Bfsar::finalizeSavedFile_(const sead::SafeString &stagedPath, const sead::SafeString &destPath)
{
    if (fileio::RenameOver(stagedPath.cstr(), packfile::GetDiskPath(destPath.cstr()).c_str()))
        return true;

    if (sForceSaveWhenInUse)
    {
        if (ForceOverwriteFile_(stagedPath, destPath))
            return true;
    }

    mPendingSaveFinalizeStaged = stagedPath;
    mPendingSaveFinalizeDest = destPath;
    mHasPendingSaveFinalize = true;

    return false;
}

void Bfsar::setLoadedArchivePath_(const sead::SafeString &path)
{
    delete mLoadedArchivePath;
    mLoadedArchivePath = new sead::HeapSafeString(nullptr, path);
}

void Bfsar::finishPendingSaveFinalize_()
{
    mHasPendingSaveFinalize = false;

    finishGroupFileSaves_(mPendingGroupFileSaves);
    mPendingGroupFileSaves.clear();

    saveMetadata_(mPendingSaveFinalizeDest, true);

    if (!mPendingSaveChangesPath)
        return;

    delete mPendingSavePreviousPath;
    mPendingSavePreviousPath = nullptr;
    mPendingSaveChangesPath = false;

    setLoadedArchivePath_(mPendingSaveFinalizeDest);
}

bool Bfsar::retryPendingSaveFinalize()
{
    if (!mHasPendingSaveFinalize)
        return false;

    if (!replaceGroupFiles_(mPendingGroupFileSaves))
        return false;

    if (!fileio::RenameOver(mPendingSaveFinalizeStaged.cstr(), packfile::GetDiskPath(mPendingSaveFinalizeDest.cstr()).c_str()))
    {
        rollBackGroupFiles_(mPendingGroupFileSaves);
        return false;
    }

    finishPendingSaveFinalize_();

    return true;
}

bool Bfsar::forcePendingSaveFinalize()
{
    if (!mHasPendingSaveFinalize)
        return false;

    if (!replaceGroupFiles_(mPendingGroupFileSaves))
        return false;

    if (!ForceOverwriteFile_(mPendingSaveFinalizeStaged, mPendingSaveFinalizeDest))
    {
        rollBackGroupFiles_(mPendingGroupFileSaves);
        return false;
    }

    finishPendingSaveFinalize_();

    return true;
}

void Bfsar::cancelPendingSaveFinalize()
{
    if (!mHasPendingSaveFinalize)
        return;

    fileio::RemoveFile(mPendingSaveFinalizeStaged.cstr());
    removeStagedGroupFiles_(mPendingGroupFileSaves);
    mPendingGroupFileSaves.clear();

    mHasPendingSaveFinalize = false;

    if (!mPendingSaveChangesPath)
        return;

    delete mFilePath;
    mFilePath = mPendingSavePreviousPath;
    mPendingSavePreviousPath = nullptr;
    mPendingSaveChangesPath = false;
}

SaveResult Bfsar::saveAs(const sead::SafeString &filePath)
{
    LOG_FUNC();
    LOG_STR(filePath.cstr());

    if (!mOpen)
        return SaveResult::Failed;

    recompileInvalidSequences_();

    if (!validate())
        return SaveResult::Failed;

    sead::HeapSafeString* previousPath = mFilePath;
    mFilePath = new sead::HeapSafeString(nullptr, filePath);

    if (!saveArchiveFile_(filePath))
    {
        if (mHasPendingSaveFinalize)
        {
            mPendingSavePreviousPath = previousPath;
            mPendingSaveChangesPath = true;
        }
        else
        {
            delete mFilePath;
            mFilePath = previousPath;
        }

        return SaveResult::Failed;
    }

    delete previousPath;
    setLoadedArchivePath_(filePath);

    return SaveResult::Saved;
}

Bfsar::BackupResult Bfsar::saveBackup(const sead::SafeString &path, std::vector<std::string> *outGroupFiles)
{
    if (!mOpen)
        return BackupResult::Invalid;

    recompileInvalidSequences_();

    if (!validate(false))
        return BackupResult::Invalid;

    if (path.calcLength() > fileio::cPathLengthMax)
        return BackupResult::OpenFailed;

    {
        sead::FileHandle handle;

        if (!fileio::OpenForWrite(&handle, path))
            return BackupResult::OpenFailed;

        syncOpusStreamLoops_();

        if (!save_(handle, planBackupGroupFiles_(path, outGroupFiles)) || !handle.tryClose())
            return BackupResult::WriteFailed;
    }

    if (!VerifyArchiveFile_(path, getArchiveMagic()))
        return BackupResult::WriteFailed;

    if (IsWrappedArchivePath(path))
    {
        sead::FixedSafeString<1100> tempPath;
        tempPath.format("%s.cmptmp", path.cstr());

        if (WrapToTempFile(path, path, mCmpbinPreferZstd, mBeaSource, tempPath) != WrapResult::Ok)
            return BackupResult::WriteFailed;

        if (!fileio::RenameOver(tempPath.cstr(), path.cstr()))
        {
            fileio::RemoveFile(tempPath.cstr());
            return BackupResult::WriteFailed;
        }
    }

    const bool saveMetadata = mSaveMetadata;

    mSaveMetadata = true;
    const bool metadataSaved = saveMetadata_(path, false);
    mSaveMetadata = saveMetadata;

    return metadataSaved ? BackupResult::Ok : BackupResult::WriteFailed;
}

void Bfsar::clearFilePath()
{
    delete mFilePath;
    mFilePath = nullptr;
}

void Bfsar::close()
{
    LOG_FUNC();
    if (!mOpen)
        return;

    close_();

    mMetadataUpgradePending = false;
    mMetadataFileUnread = false;

    cancelPendingSaveFinalize();

    if (mFilePath)
    {
        delete mFilePath;
        mFilePath = nullptr;
    }

    if (mLoadedArchivePath)
    {
        delete mLoadedArchivePath;
        mLoadedArchivePath = nullptr;
    }

    mOpen = false;
}

void Bfsar::updateList(Item::List& list)
{
    u32 i = 0;
    for (Item* item : list)
    {
        item->setId(i);

        i++;
    }
}

static std::string WaveContentKey_(const WaveFile *wave)
{
    std::string blob;
    auto put = [&](const void *p, u32 n)
    { blob.append(static_cast<const char *>(p), n); };

    u32 enc = static_cast<u32>(wave->getEncoding());
    u32 rate = wave->getSampleRate();
    u32 count = wave->getSampleCount();
    u32 isLoop = wave->getIsLoop() ? 1u : 0u;
    u32 loopS = wave->getOriginalLoopStartFrame();
    u32 loopE = wave->getOriginalLoopEndFrame();
    u32 chN = wave->getChannels().size();

    put(&enc, 4);
    put(&rate, 4);
    put(&count, 4);
    put(&isLoop, 4);
    put(&loopS, 4);
    put(&loopE, 4);
    put(&chN, 4);

    for (u32 i = 0; i < chN; i++)
    {
        const WaveFile::Channel *ch = wave->getChannels().nth(i);
        u32 sz = ch->getDataSizeMin();
        put(&sz, 4);
        if (ch->getData() && sz)
            put(ch->getData(), sz);
        if (wave->getEncoding() == WaveFile::Encoding::DspAdpcm)
        {
            const auto &ap = ch->getAdpcmParam(false);
            put(&ap, sizeof(ap));
        }
    }

    return md5(blob.data(), static_cast<u32>(blob.size()));
}

static bool WaveContentEqual_(const WaveFile *a, const WaveFile *b)
{
    if (a->getEncoding() != b->getEncoding())
        return false;

    if (a->getSampleRate() != b->getSampleRate())
        return false;

    if (a->getSampleCount() != b->getSampleCount())
        return false;

    if (a->getIsLoop() != b->getIsLoop())
        return false;

    if (a->getOriginalLoopStartFrame() != b->getOriginalLoopStartFrame())
        return false;

    if (a->getOriginalLoopEndFrame() != b->getOriginalLoopEndFrame())
        return false;

    u32 n = a->getChannels().size();

    if (n != b->getChannels().size())
        return false;

    for (u32 i = 0; i < n; i++)
    {
        const WaveFile::Channel *ca = a->getChannels().nth(i);
        const WaveFile::Channel *cb = b->getChannels().nth(i);

        u32 sz = ca->getDataSizeMin();

        if (sz != cb->getDataSizeMin())
            return false;
        if (sz)
        {
            if (!ca->getData() || !cb->getData())
                return ca->getData() == cb->getData();

            if (sead::MemUtil::compare(ca->getData(), cb->getData(), sz) != 0)
                return false;
        }
        if (a->getEncoding() == WaveFile::Encoding::DspAdpcm)
        {
            if (sead::MemUtil::compare(&ca->getAdpcmParam(false), &cb->getAdpcmParam(false), sizeof(ca->getAdpcmParam(false))) != 0)
                return false;
        }
    }

    return true;
}

static int WaveNameScore_(const WaveFile *wave)
{
    if (!wave->isNameValid())
        return 0;

    std::string name = wave->getName().cstr();

    if (name.rfind("GUESS", 0) == 0)
        return 1;

    return 2;
}

std::vector<Bfsar::WaveDuplicateGroup> Bfsar::findDuplicateWaves()
{
    std::vector<WaveDuplicateGroup> groups;
    std::unordered_map<std::string, std::vector<WaveFile *>> buckets;

    for (Item *it : mWaveFileList)
    {
        WaveFile *wave = static_cast<WaveFile *>(it);

        if (wave->getIsStreamExtended())
            continue;

        std::string key = std::to_string(wave->mWaveArchiveId) + ":" + WaveContentKey_(wave);
        buckets[key].push_back(wave);
    }

    for (auto &entry : buckets)
    {
        std::vector<WaveFile *> &waves = entry.second;

        if (waves.size() < 2)
            continue;

        std::vector<WaveFile *> members;
        members.push_back(waves[0]);

        for (u32 i = 1; i < waves.size(); i++)
        {
            if (WaveContentEqual_(waves[0], waves[i]))
                members.push_back(waves[i]);
        }

        if (members.size() < 2)
            continue;

        WaveDuplicateGroup group;
        group.keep = members[0];

        for (WaveFile *w : members)
        {
            if (WaveNameScore_(w) > WaveNameScore_(group.keep))
                group.keep = w;
        }

        for (WaveFile *w : members)
        {
            if (w != group.keep)
                group.remove.push_back(w);
        }

        groups.push_back(std::move(group));
    }

    std::sort(groups.begin(), groups.end(), [](const WaveDuplicateGroup &a, const WaveDuplicateGroup &b)
              { return a.keep->getId() < b.keep->getId(); });

    return groups;
}

Bfsar::WaveMergeResult Bfsar::mergeDuplicateWaves(const std::vector<WaveDuplicateGroup> &groups)
{
    WaveMergeResult result;

    if (groups.empty())
        return result;

    snd::internal::driver::SoundThreadLock lock;

    for (const WaveDuplicateGroup &group : groups)
    {
        if (!group.keep || group.remove.empty())
            continue;

        for (WaveFile *dup : group.remove)
        {
            std::vector<ItemReference *> refs;

            for (auto rit = dup->mReferences.robustBegin(); rit != dup->mReferences.robustEnd(); ++rit)
                refs.push_back(rit->val());

            for (ItemReference *ref : refs)
                ref->attach(group.keep);

            result.bytesSaved += dup->getFileSize();
            result.duplicatesRemoved++;

            mWaveFileList.erase(dup);
            delete dup;
        }
    }

    if (result.duplicatesRemoved > 0)
        updateList(mWaveFileList);

    return result;
}

std::vector<WaveFile*> Bfsar::findUnusedWaveFiles()
{
    std::vector<WaveFile*> unused;

    for (Item* it : mWaveFileList)
    {
        WaveFile* wave = static_cast<WaveFile*>(it);

        if (wave->getReferences().isEmpty())
            unused.push_back(wave);
    }

    return unused;
}

u32 Bfsar::removeUnusedWaveFiles(const std::vector<WaveFile*>& unused)
{
    if (unused.empty())
        return 0;

    snd::internal::driver::SoundThreadLock lock;

    for (WaveFile* wave : unused)
    {
        mWaveFileList.erase(wave);
        delete wave;
    }

    updateList(mWaveFileList);
    return (u32)unused.size();
}

void Bfsar::close_()
{
    LOG_FUNC();
    mSoundList.clear();
    mSoundSetList.clear();
    mBankList.clear();
    mWaveArchiveList.clear();
    mGroupList.clear();
    mPlayerList.clear();
    mWaveFileList.clear();
    mSequenceFileList.clear();
    mBankFileList.clear();
    clearGenWaveArchiveList();
    mItemOrigFileIds.clear();
    mFileAttachedGroups.clear();
    mFileOriginalIncludeInBfsar.clear();
    mFileOriginalHasData.clear();
    mFileOriginalHomeGroup.clear();
    mFileOriginalOwnWarc.clear();
    mStreamFileOrder = StreamFileOrder::ByVersion;
    mLoadedGroupFilePaths.clear();
    mDeletedGroupFiles.clear();
    mStreamSoundInfoSize = 0;
    mArchiveNameHash = 0;
    forgetReadConverterTraits();

    FreeLoadedFiles(mExternalGroupBuffers);
}
