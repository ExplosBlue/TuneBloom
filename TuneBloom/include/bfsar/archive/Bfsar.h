#pragma once

#include <bfsar/model/Bank.h>
#include <bfsar/files/InnerFile.h>
#include <bfsar/files/BankFile.h>
#include <bfsar/model/Group.h>
#include <bfsar/model/Player.h>
#include <bfsar/files/SequenceFile.h>
#include <bfsar/model/Sound.h>
#include <bfsar/model/SoundSet.h>
#include <bfsar/model/WaveArchive.h>
#include <bfsar/files/WaveFile.h>

#include <ui/Messages.h>

#include <filedevice/seadFileDevice.h>
#include <prim/seadSafeString.h>

#include <snd/snd_MemorySoundArchive.h>

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

struct SoundArchivePlayerInfo
{
    SoundArchivePlayerInfo()
    {
        sead::MemUtil::fillZero(this, sizeof(SoundArchivePlayerInfo));
    }

    u16 sequenceSoundMax;
    u16 sequenceTrackMax;
    u16 streamSoundMax;
    u16 streamTrackMax;
    u16 streamChannelMax;
    u16 waveSoundMax;
    u16 waveTrackMax;
    u8 streamBufferTimes;
    u32 options;
};

namespace nw { namespace snd { namespace internal {

class GroupFileReader;
class StreamSoundFileReader;
class WaveArchiveFileReader;
class WaveSoundFileReader;

} } }

struct ByteRange;
struct BfsarGroupItemFiles;
struct BfsarOpenContext;
struct BfsarSaveContext;
struct BfsarValidateContext;
struct File;
struct StreamReloadContext;

template <typename Key, typename T>
class VectorMap;

ArchivePlatform getDefaultPlatformForFormat(ArchiveFormat format);

class Bfsar
{
public:
    Bfsar();
    ~Bfsar();

    ArchiveFormat getFormat() const
    {
        return mFormat;
    }

    void setFormat(ArchiveFormat format)
    {
        mFormat = format;
    }

    ArchivePlatform getPlatform() const
    {
        return mPlatform;
    }

    void setPlatform(ArchivePlatform platform)
    {
        mPlatform = platform;
    }

    void setCliMode(bool cliMode) { mCliMode = cliMode; }
    bool getCliMode() const { return mCliMode; }

    void create(ArchiveFormat format = ArchiveFormat::BFSAR);
    void create(ArchiveFormat format, ArchivePlatform platform);
    bool open(u8* bfsarFile, u32 bfsarSize, const sead::SafeString& filePath, sead::Heap* heap, const sead::SafeString* metadataSource = nullptr);

    struct StreamSaveJob
    {
        const Sound *sound = nullptr;
        std::string savePath;
        std::string srcPath;
        u64 signature = 0;
        bool copyOnly = false;
    };

    struct SkippedStream
    {
        const Sound *sound = nullptr;
        messages::Message message;
    };

    bool save();
    bool saveAs(const sead::SafeString &filePath);
    enum class BackupResult
    {
        Ok,
        Invalid,
        OpenFailed,
        WriteFailed
    };

    struct BackupStream
    {
        std::string path;
        u64 signature = 0;
    };

    BackupResult saveBackup(const sead::SafeString &path, std::vector<std::string> *outGroupFiles = nullptr);
    std::vector<std::string> getGroupFileSavePaths(const sead::SafeString& archivePath);
    std::string getGroupFileSavePath(const Group* group) const;
    u32 getGroupExtraFileCount(const Group* group) const;
    bool exportGroupFile(Group* group, const sead::SafeString& path);

    enum class GroupFileCheck
    {
        Ok,
        Unreadable,
        NotGroupFile,
        OtherArchive
    };

    GroupFileCheck checkGroupFile(const sead::SafeString& path) const;
    std::string findGroupFileInFolder(const sead::SafeString& groupName, const sead::SafeString& folder) const;
    void setGroupFilePath(const sead::SafeString& groupName, const sead::SafeString& path, bool keep) const;
    std::vector<std::string> takeDeletedGroupFiles();
    bool saveBackupStreams(const std::string &dir, const std::string &previousDir, const std::vector<BackupStream> &previous, std::vector<BackupStream> *out) const;
    bool restoreStreamFromFile(const sead::SafeString &streamPath, const sead::SafeString &filePath);
    void clearFilePath();

    bool hasPendingSaveFinalize() const
    {
        return mHasPendingSaveFinalize;
    }

    const sead::SafeString &getPendingSaveFinalizePath() const
    {
        return mPendingSaveFinalizeDest;
    }

    bool retryPendingSaveFinalize();
    bool forcePendingSaveFinalize();
    void cancelPendingSaveFinalize();

    void planStreamSaves(std::vector<StreamSaveJob> &out, std::vector<SkippedStream> &outSkipped) const;
    void executeStreamSave(const StreamSaveJob &job) const;
    void close();

    bool isOpen() const
    {
        return mOpen;
    }

    const sead::SafeString& getFilePath() const
    {
        if (mFilePath)
            return *mFilePath;

        return sead::SafeString::cEmptyString;
    }

    const sead::SafeString &getLoadedArchivePath() const
    {
        if (mLoadedArchivePath)
            return *mLoadedArchivePath;

        return getFilePath();
    }

    sead::Endian::Types getEndian() const
    {
        return mEndian;
    }

    void setEndian(sead::Endian::Types endian)
    {
        mEndian = endian;
    }

    u32 getVersion() const
    {
        return mVersion;
    }

    void setVersion(u32 version)
    {
        mVersion = version;
    }

    // BFSAR:        version = (0 << 24) | (major << 16) | (minor << 8) | sub
    // BFSAR 3.0.0+: version = (major << 24) | (minor << 16) | (micro << 8) | sub
    // BCSAR:        version = (major << 24) | (minor << 16) | (0 << 8) | sub
    bool packsMajorInHighByte() const
    {
        return mFormat == ArchiveFormat::BCSAR || nw::snd::internal::Util::IsHighByteMajorVersion(mVersion);
    }

    bool isV3Bfsar() const
    {
        return mFormat == ArchiveFormat::BFSAR && getDecodedMajor() >= 3;
    }

    u32 getDecodedMajor() const
    {
        return packsMajorInHighByte() ? (mVersion >> 24) & 0xFF : (mVersion >> 16) & 0xFF;
    }

    u32 getDecodedMinor() const
    {
        return packsMajorInHighByte() ? (mVersion >> 16) & 0xFF : (mVersion >> 8) & 0xFF;
    }

    u32 getDecodedPatch() const
    {
        if (mFormat != ArchiveFormat::BCSAR && nw::snd::internal::Util::IsHighByteMajorVersion(mVersion))
            return (mVersion >> 8) & 0xFF;

        return mVersion & 0xFF;
    }

    bool isVersionOrLater(u32 major, u32 minor, u32 patch) const
    {
        if (getDecodedMajor() > major) return true;
        if (getDecodedMajor() < major) return false;
        if (getDecodedMinor() > minor) return true;
        if (getDecodedMinor() < minor) return false;
        return getDecodedPatch() >= patch;
    }

    bool isStreamTrackInfoAvailable() const
    {
        return isVersionOrLater(2, 0, 0);
    }

    bool isStreamSendAvailable() const
    {
        if (mFormat == ArchiveFormat::BCSAR)
            return isVersionOrLater(2, 0, 0);

        return isVersionOrLater(2, 1, 0);
    }

    bool isFilterSupportedVersion() const
    {
        if (mFormat == ArchiveFormat::BCSAR)
            return isVersionOrLater(2, 3, 1);

        return isVersionOrLater(2, 1, 0);
    }

    bool isStreamPrefetchAvailable() const
    {
        if (mFormat == ArchiveFormat::BCSAR)
            return isVersionOrLater(2, 3, 2);

        return isVersionOrLater(2, 2, 0);
    }

    bool isStreamPrefetchBuildable() const
    {
        return mFormat == ArchiveFormat::BFSAR && mEndian == sead::Endian::eBig && isStreamPrefetchAvailable();
    }

    bool hasStreamSoundExtensionRef() const
    {
        return isStreamPrefetchAvailable() || mStreamSoundInfoSize >= cStreamSoundInfoSizeWithExtension;
    }

    bool hasStreamPrefetchFileId() const
    {
        return isStreamPrefetchAvailable() || mStreamSoundInfoSize >= cStreamSoundInfoSizeWithPrefetch;
    }

    bool isIncludeStringTable() const
    {
        return mIncludeStringTable;
    }

    void setIncludeStringTable(bool includeStringTable)
    {
        mIncludeStringTable = includeStringTable;
    }

    bool getSaveMetadata() const
    {
        return mSaveMetadata;
    }

    void setSaveMetadata(bool v)
    {
        mSaveMetadata = v;
    }

    bool hasMetadataNames() const;

    bool hasPendingMetadataUpgrade() const
    {
        return mMetadataUpgradePending;
    }

    u32 getMetadataUpgradeLoadedCount() const
    {
        return mMetadataUpgradeLoaded;
    }

    u32 getMetadataUpgradeMissingCount() const
    {
        return mMetadataUpgradeMissing;
    }

    void dismissMetadataUpgrade()
    {
        mMetadataUpgradePending = false;
    }

    bool upgradeMetadata();

    const SoundArchivePlayerInfo& getSoundArchivePlayerInfo() const
    {
        return mSoundArchivePlayerInfo;
    }

    SoundArchivePlayerInfo& getSoundArchivePlayerInfo()
    {
        return mSoundArchivePlayerInfo;
    }

    Sound::List& getSoundList()
    {
        return mSoundList;
    }

    SoundSet::List& getSoundSetList()
    {
        return mSoundSetList;
    }

    Bank::List& getBankList()
    {
        return mBankList;
    }

    WaveArchive::List& getWaveArchiveList()
    {
        return mWaveArchiveList;
    }

    Group::List& getGroupList()
    {
        return mGroupList;
    }

    Player::List& getPlayerList()
    {
        return mPlayerList;
    }

    WaveFile::List& getWaveFileList()
    {
        return mWaveFileList;
    }

    SequenceFile::List& getSequenceFileList()
    {
        return mSequenceFileList;
    }

    BankFile::List& getBankFileList()
    {
        return mBankFileList;
    }

    void clearGenWaveArchiveList()
    {
        mGenWaveArchiveList.clear();
    }

    const Item* getItem(u32 id, const Item::List& list) const
    {
        if (id != Item::cInvalidId)
        {
            id = nw::snd::internal::Util::GetItemIndex(id);

            if (id < list.size())
            {
                return list.nth(id)->val();
            }
        }

        return nullptr;
    }

    Item* getItem(u32 id, const Item::List& list)
    {
        if (id != Item::cInvalidId)
        {
            id = nw::snd::internal::Util::GetItemIndex(id);

            if (id < list.size())
            {
                return list.nth(id)->val();
            }
        }

        return nullptr;
    }

    const Item::List& getItemList(Item::ItemType itemType)
    {
        switch (itemType)
        {
            case Item::ItemType::Sound:
                return getSoundList();

            case Item::ItemType::SoundSet:
                return getSoundSetList();

            case Item::ItemType::Bank:
                return getBankList();

            case Item::ItemType::WaveArchive:
                return getWaveArchiveList();

            default:
                break;
        }

        static const Item::List cNullList;
        return cNullList;
    }

    //? Check if name contains only allowed characters
    bool validName(const sead::SafeString& name) const;
    //? Check if name is duplicated
    bool validateName(const sead::SafeString& name) const;
    //? Check if name is duplicated (Excluding item)
    bool validateName(const Item& item) const;

    void updateList(Item::List& list);

    struct WaveDuplicateGroup
    {
        WaveFile* keep = nullptr;
        std::vector<WaveFile*> remove;
    };

    struct WaveMergeResult
    {
        u32 duplicatesRemoved = 0;
        u64 bytesSaved = 0;
    };

    std::vector<WaveDuplicateGroup> findDuplicateWaves();
    WaveMergeResult mergeDuplicateWaves(const std::vector<WaveDuplicateGroup>& groups);

    std::vector<WaveFile*> findUnusedWaveFiles();
    u32 removeUnusedWaveFiles(const std::vector<WaveFile*>& unused);

    struct StreamReloadResult
    {
        bool succeeded = false;

        u32 trackCount = 0;
        u32 channelCount = 0;
        u32 sampleRate = 0;

        bool layoutChanged = false;
        bool tracksReplaced = false;
        bool streamTypeChanged = false;
        bool loopChanged = false;

        u32 sharedSoundCount = 0;
    };

    bool hasArchiveDirectory() const;
    bool resolveStreamFilePath(const Sound& sound, sead::BufferedSafeString* out) const;

    StreamReloadResult reloadStreamSound(Sound* sound, const sead::SafeString* filePathOverride = nullptr);

    void captureItemBaselines();
    u64 computeInfoSignature() const;

    bool isInfoModified() const
    {
        return computeInfoSignature() != mInfoBaselineSignature;
    }

    u32 getVersionForBfwsd() const
    {
        if (isV3Bfsar())
            return makeVersion(2, 1, 0);

        if (mFormat == ArchiveFormat::BCSAR)
        {
            if (isVersionOrLater(2, 3, 0))
                return makeVersion(1, 0, 1);

            return makeVersion(1, 0, 0);
        }

        if (isVersionOrLater(2, 1, 0))
            return makeVersion(0, 1, 1);

        return makeVersion(0, 1, 0);
    }

    u32 getVersionForBfbnk() const
    {
        if (isV3Bfsar())
            return makeVersion(2, 0, 0);

        if (mFormat == ArchiveFormat::BCSAR)
            return makeVersion(1, 0, 1);

        return makeVersion(0, 1, 0);
    }

    u32 getVersionForBfwar() const
    {
        if (isV3Bfsar())
            return makeVersion(2, 0, 0);

        if (mFormat == ArchiveFormat::BCSAR)
            return makeVersion(1, 0, 0);

        if (isVersionOrLater(2, 1, 0))
            return makeVersion(0, 1, 0);

        return makeVersion(0, 1, 0);
    }

    u32 getVersionForBfgrp() const
    {
        if (isV3Bfsar())
            return makeVersion(2, 0, 0);

        if (mFormat == ArchiveFormat::BCSAR)
            return makeVersion(1, 1, 0);

        if (isVersionOrLater(2, 1, 0))
            return makeVersion(0, 1, 0);

        return makeVersion(0, 1, 0);
    }

    u32 getVersionForBfseq() const
    {
        if (isV3Bfsar())
            return makeVersion(2, 0, 0);

        if (mFormat == ArchiveFormat::BCSAR)
        {
            if (isVersionOrLater(2, 3, 0))
                return makeVersion(1, 1, 0);

            return makeVersion(1, 0, 0);
        }
        else
        {
            if (isVersionOrLater(2, 1, 0))
                return makeVersion(0, 2, 0);

            return makeVersion(0, 1, 0);
        }
    }

    u32 getVersionForBfwav() const
    {
        if (isV3Bfsar())
            return makeVersion(2, 0, 0);

        if (mFormat == ArchiveFormat::BCSAR)
        {
            if (getDecodedMajor() >= 2)
                return makeVersion(2, 1, 0);

            return makeVersion(2, 0, 0);
        }
        else
        {
            if (isVersionOrLater(2, 2, 0))
                return makeVersion(0, 1, 2);
            else if (getDecodedMajor() >= 2)
                return makeVersion(0, 1, 1);

            return makeVersion(0, 1, 0);
        }
    }

    u32 getVersionForBfstm() const
    {
        if (isV3Bfsar())
            return makeVersion(2, 0, 1);

        if (mFormat == ArchiveFormat::BCSAR)
        {
            if (isVersionOrLater(2, 3, 2))
                return makeVersion(2, 3, 1);
            if (isVersionOrLater(2, 3, 1))
                return makeVersion(2, 3, 0);
            if (isVersionOrLater(2, 3, 0))
                return makeVersion(2, 2, 0);
            if (isVersionOrLater(2, 1, 0))
                return makeVersion(2, 1, 0);
            return makeVersion(2, 0, 0);
        }
        else
        {
            if (isVersionOrLater(2, 2, 0))
                return makeVersion(0, 4, 0);
            else if (getDecodedMajor() >= 2)
                return makeVersion(0, 3, 0);

            return makeVersion(0, 1, 0);
        }
    }

    //? Validate every item for saving
    bool validate_(bool showErrors = true);
    void recompileInvalidSequences_();

    const char* getArchiveMagic() const
    {
        return GetInnerFileMagic(mFormat, InnerFileKind::SoundArchive);
    }

    const char* getSeqMagic() const
    {
        return GetInnerFileMagic(mFormat, InnerFileKind::Sequence);
    }

    const char* getBankMagic() const
    {
        return GetInnerFileMagic(mFormat, InnerFileKind::Bank);
    }

    const char* getWsdMagic() const
    {
        return GetInnerFileMagic(mFormat, InnerFileKind::WaveSoundData);
    }

    const char* getWarMagic() const
    {
        return GetInnerFileMagic(mFormat, InnerFileKind::WaveArchive);
    }

    const char* getGrpMagic() const
    {
        return GetInnerFileMagic(mFormat, InnerFileKind::Group);
    }

    const char* getStmMagic() const
    {
        return GetInnerFileMagic(mFormat, InnerFileKind::Stream);
    }

    const char* getWavMagic() const
    {
        return GetInnerFileMagic(mFormat, InnerFileKind::Wave);
    }

public:
    void setCmpbinPreferZstd(bool v) { mCmpbinPreferZstd = v; }
    bool getCmpbinPreferZstd() const { return mCmpbinPreferZstd; }

private:
    enum class StreamFileOrder
    {
        ByVersion,
        First,
        Last
    };

    struct GroupFileSave
    {
        Group* group = nullptr;
        std::string path;
        std::string stagedPath;
        std::string backupPath;
        bool isReplaced = false;
    };

    bool open_(const nw::snd::MemorySoundArchive &soundArchive, u32 bfsarSize, sead::Heap *heap, const sead::SafeString &groupFileSource);
    bool save_(sead::FileHandle &handle, const std::vector<GroupFileSave>& groupFileSaves, std::vector<SkippedStream> *outSkippedStreams = nullptr);

    bool areStreamFilesLast_() const
    {
        if (mStreamFileOrder != StreamFileOrder::ByVersion)
            return mStreamFileOrder == StreamFileOrder::Last;

        return mFormat == ArchiveFormat::BFSAR && isVersionOrLater(2, 2, 0);
    }

    bool areGroupItemsSortedById_() const
    {
        return isVersionOrLater(2, mFormat == ArchiveFormat::BCSAR ? 3 : 1, 0);
    }

    std::vector<GroupFileSave> planGroupFileSaves_(const sead::SafeString& archivePath);
    std::vector<GroupFileSave> planBackupGroupFiles_(const sead::SafeString& backupPath, std::vector<std::string>* outFiles);
    void moveGroupFilePaths_(const sead::SafeString& fromArchivePath, const sead::SafeString& toArchivePath);
    bool writeExternalGroupFiles_(BfsarSaveContext& ctx, const std::vector<GroupFileSave>& groupFileSaves);
    bool replaceGroupFiles_(std::vector<GroupFileSave>& groupFileSaves);
    void rollBackGroupFiles_(std::vector<GroupFileSave>& groupFileSaves) const;
    void finishGroupFileSaves_(const std::vector<GroupFileSave>& groupFileSaves);
    void updateLoadedGroupFiles_(const std::vector<GroupFileSave>& groupFileSaves);
    void removeStagedGroupFiles_(const std::vector<GroupFileSave>& groupFileSaves) const;
    void reportGroupFileWriteFailed_(const GroupFileSave& save) const;
    bool reportUnwrittenGroupFile_(const std::vector<GroupFileSave>& groupFileSaves) const;
    void rememberGroupFilePath_(const Group* group) const;
    bool usesStoredGroupFilePaths_() const;
    void collectExternalGroupFiles_(BfsarSaveContext& ctx);
    std::vector<const Item*> getExtraFileOwners_(const Group* group) const;
    void attachExtraFilesToGroup_(BfsarSaveContext& ctx, const Group* group);
    bool hasGroupFileSlot_(const Group* group) const;
    void restoreGroupFileOrder_(BfsarSaveContext& ctx, const Group* group);

    void readArchiveVersion_(const BfsarOpenContext& ctx);
    void readPlayerSettings_(const BfsarOpenContext& ctx);
    void setLoadStageCounts_(const BfsarOpenContext& ctx);
    void readItemName_(const BfsarOpenContext& ctx, Item* item, u32 stringId, const char* defaultNameFormat);
    WaveArchiveType readWaveArchiveRef_(const nw::snd::internal::Util::Table<nw::ut::ResU32>& warcIdTable, ItemReference* waveArchiveRef);
    void attachExternalGroupFile_(BfsarOpenContext& ctx, Group* group, nw::snd::internal::SoundArchiveFile::GroupInfo* groupInfo, const ByteRange& file, const std::string& path);
    void assignGroupFileSlots_(const BfsarOpenContext& ctx);
    ByteRange takeGroupFileOfArchive_(const BfsarOpenContext& ctx, const Group& group, const ByteRange& file) const;
    ByteRange loadExternalGroupFile_(const BfsarOpenContext& ctx, const Group& group, bool searchFolders, std::string* outPath);
    bool loadExtDataGroupFile_(BfsarOpenContext& ctx, Group* group, nw::snd::internal::SoundArchiveFile::GroupInfo* groupInfo);
    void resolveGroupFile_(BfsarOpenContext& ctx, Group* group, nw::snd::internal::SoundArchiveFile::GroupInfo* groupInfo);
    bool readGroups_(BfsarOpenContext& ctx);
    bool readPlayers_(BfsarOpenContext& ctx);
    void readSequenceFiles_(BfsarOpenContext& ctx);
    void hashWaveArchiveFiles_(BfsarOpenContext& ctx);
    Item* addGenWaveArchive_(BfsarOpenContext& ctx, u32 index, const nw::snd::internal::SoundArchiveFile::WaveArchiveInfo& warcInfo);
    void readWaveArchiveWaves_(BfsarOpenContext& ctx, u32 warcIndex, u32 waveArchiveId, const nw::snd::internal::WaveArchiveFileReader& reader);
    bool readWaveArchiveFiles_(BfsarOpenContext& ctx);
    bool readWaveArchives_(BfsarOpenContext& ctx);
    void readFileGroupTables_(BfsarOpenContext& ctx);
    bool patchBankFileWaveIds_(BfsarOpenContext& ctx);
    bool patchWaveSoundFileWaveIds_(BfsarOpenContext& ctx);
    void readBankFiles_(BfsarOpenContext& ctx);
    void attachBankFile_(BfsarOpenContext& ctx, Bank* bank, u32 fileIdx);
    bool readBanks_(BfsarOpenContext& ctx);
    void readSoundInfo_(BfsarOpenContext& ctx, Sound* sound, const nw::snd::internal::SoundArchiveFile::SoundInfo& soundInfo);
    void readSequenceSoundInfo_(BfsarOpenContext& ctx, Sound* sound, const nw::snd::internal::SoundArchiveFile::SoundInfo& soundInfo);
    void readStreamTrackInfo_(Sound::StreamSoundInfo::Track* track, const nw::snd::internal::SoundArchiveFile::StreamTrackInfo& trackInfo);
    void readStreamTrackInfoTable_(BfsarOpenContext& ctx, Sound* sound, const nw::snd::internal::SoundArchiveFile::StreamSoundInfo& strmSoundInfo);
    void readStreamFilePath_(const BfsarOpenContext& ctx, Sound* sound, u32 fileId);
    bool findStreamFilePath_(const Sound* sound, sead::BufferedSafeString* outPath) const;
    bool readStreamFileTracks_(BfsarOpenContext& ctx, Sound* sound, const ByteRange& strmFile);
    void readStreamSoundParams_(BfsarOpenContext& ctx, Sound* sound, const nw::snd::internal::SoundArchiveFile::StreamSoundInfo& strmSoundInfo);
    void readStreamFileWaves_(BfsarOpenContext& ctx, Sound* sound, const ByteRange& strmFile);
    void readStreamPrefetch_(BfsarOpenContext& ctx, Sound* sound, const nw::snd::internal::SoundArchiveFile::StreamSoundInfo& strmSoundInfo);
    void readStreamSoundInfo_(BfsarOpenContext& ctx, Sound* sound, const nw::snd::internal::SoundArchiveFile::SoundInfo& soundInfo);
    void attachWaveSoundWave_(Sound* sound, const nw::snd::internal::WaveSoundFileReader& reader, u32 index);
    void readWaveSoundParams_(Sound* sound, const nw::snd::internal::WaveSoundFileReader& reader, u32 index);
    void readWaveSoundInfo_(BfsarOpenContext& ctx, Sound* sound, const nw::snd::internal::SoundArchiveFile::SoundInfo& soundInfo);
    bool readSounds_(BfsarOpenContext& ctx);
    void guessBankWaveFileNames_();
    bool readSoundSets_(BfsarOpenContext& ctx);
    void addGroupItem_(BfsarOpenContext& ctx, const nw::snd::internal::GroupFileReader& reader, Group* group, u32 itemIdx);
    void addGroupBankFiles_(BfsarOpenContext& ctx, const BfsarGroupItemFiles& item, u32 bankId);
    void addGroupSoundFiles_(BfsarOpenContext& ctx, const BfsarGroupItemFiles& item, u32 soundId, bool isGroupItem);
    void addGroupWaveSoundSetFiles_(BfsarOpenContext& ctx, const BfsarGroupItemFiles& item, const nw::snd::internal::SoundArchiveFile::SoundGroupInfo& soundSetInfo);
    void addGroupSoundSetFiles_(BfsarOpenContext& ctx, const BfsarGroupItemFiles& item);
    void addGroupItemFiles_(BfsarOpenContext& ctx, const BfsarGroupItemFiles& item);
    void addGroupItemsByFile_(BfsarOpenContext& ctx, const nw::snd::internal::GroupFileReader& reader, Group* group);
    void readGroupFile_(BfsarOpenContext& ctx, Group* group, u32 groupId, const nw::snd::internal::SoundArchiveFile::GroupInfo* groupInfo);
    bool readGroupItems_(BfsarOpenContext& ctx);
    void logLoadedItems_() const;
    void recordOriginalFileIds_(const BfsarOpenContext& ctx);

    void delegateSoundSetWaveFiles_(BfsarSaveContext& ctx, const SoundSet* soundSet, const WaveArchive* warc);
    void delegateWaveFiles_(BfsarSaveContext& ctx, const Item* item, const WaveArchive* warc);
    void assignWaveArchives_(BfsarSaveContext& ctx);
    void delegateExplicitWaveFiles_(BfsarSaveContext& ctx);
    void attachSoundToGroup_(BfsarSaveContext& ctx, const Sound* sound, u32 loadFlag, const Group* group);
    void attachSoundSetToGroup_(BfsarSaveContext& ctx, const SoundSet* soundSet, u32 loadFlag, const Group* group);
    void attachItemsToGroups_(BfsarSaveContext& ctx);
    bool groupHasSharedWaveArchive_(const Group* group);
    WaveArchive* createGeneratedWaveArchive_(BfsarSaveContext& ctx, const Item* owner);
    void generateGroupWaveArchives_(BfsarSaveContext& ctx);
    void generateOwnerWaveArchives_(BfsarSaveContext& ctx, const Item* owner, WaveArchiveType type, VectorMap<const Group*, const WaveArchive*>& warcs);
    void generateSoundSetWaveArchives_(BfsarSaveContext& ctx);
    void generateBankWaveArchives_(BfsarSaveContext& ctx);
    void setupPrefetchFiles_(BfsarSaveContext& ctx);
    void setupStreamFiles_(BfsarSaveContext& ctx);
    void setupSequenceFiles_(BfsarSaveContext& ctx);
    void setupWaveSoundFiles_(BfsarSaveContext& ctx);
    void setupBankFiles_(BfsarSaveContext& ctx);
    void setupWaveArchiveFiles_(BfsarSaveContext& ctx);
    void setupGroupFiles_(BfsarSaveContext& ctx);
    void setupGeneratedWaveArchiveFiles_(BfsarSaveContext& ctx);
    void attachSoundFileToGroup_(BfsarSaveContext& ctx, const Sound* sound, u32 loadFlag, const Group* group);
    void attachSoundSetFileToGroup_(BfsarSaveContext& ctx, const SoundSet* soundSet, u32 loadFlag, const Group* group);
    void collectGroupFiles_(BfsarSaveContext& ctx);
    void collectEmbeddedFiles_(BfsarSaveContext& ctx);
    bool getOrigIncludeInBfsar_(const BfsarSaveContext& ctx, const File& file) const;
    void collectStrings_(BfsarSaveContext& ctx);
    void writeStringBlock_(BfsarSaveContext& ctx);
    void writeSequenceSoundInfo_(BfsarSaveContext& ctx, const Sound* sound);
    void writeStreamTrackInfoTable_(BfsarSaveContext& ctx, const Sound::StreamSoundInfo& strmInfo);
    void writeStreamSoundExtension_(BfsarSaveContext& ctx, const Sound::StreamSoundInfo& strmInfo);
    void writeStreamSoundInfo_(BfsarSaveContext& ctx, const Sound* sound);
    void writeWaveSoundInfo_(BfsarSaveContext& ctx, const Sound* sound);
    void writeSoundInfo_(BfsarSaveContext& ctx, const Sound* sound);
    void writeSoundInfoSection_(BfsarSaveContext& ctx);
    void writeSoundSetInfoSection_(BfsarSaveContext& ctx);
    void writeBankInfoSection_(BfsarSaveContext& ctx);
    void writeWaveArchiveInfoSection_(BfsarSaveContext& ctx);
    void writeGroupInfoSection_(BfsarSaveContext& ctx);
    void writePlayerInfoSection_(BfsarSaveContext& ctx);
    void writeFileInfoSection_(BfsarSaveContext& ctx);
    void writeSoundArchivePlayerInfo_(BfsarSaveContext& ctx);
    void writeInfoBlock_(BfsarSaveContext& ctx);
    void writeFileBlock_(BfsarSaveContext& ctx);
    bool isStreamSoundExtensionWritten_(const Sound::StreamSoundInfo& strmInfo) const;
    void syncOpusStreamLoops_();
    void deleteGeneratedItems_(BfsarSaveContext& ctx);
    void saveStreamFiles_(std::vector<SkippedStream> &&skippedStreams);
    void logSavedItems_() const;

    bool saveArchiveFile_(const sead::SafeString &path);
    bool finalizeSavedFile_(const sead::SafeString &stagedPath, const sead::SafeString &destPath);
    void finishPendingSaveFinalize_();
    void setLoadedArchivePath_(const sead::SafeString &path);
    bool readEmbeddedStreamTracks(nw::snd::internal::StreamSoundFileReader& reader, Sound::StreamSoundInfo& strmSoundInfo, sead::Heap* heap) const;
    void applyStreamTrackLayout(Sound* sound, u32 trackCount, u32 channelCount) const;
    void detachStreamWaves(Sound* sound);
    u32 shareStreamWavesWithSiblings(Sound* source, bool tracksFromFile);
    bool loadStreamFileForReload_(StreamReloadContext& ctx);
    bool checkStreamFileForReload_(StreamReloadContext& ctx, StreamReloadResult* result) const;
    bool stageStreamReload_(StreamReloadContext& ctx, const StreamReloadResult& result);
    void commitStreamReload_(StreamReloadContext& ctx, StreamReloadResult* result);
    bool writeStreamFile_(const Sound *sound, const sead::SafeString &path, bool *outReported) const;
    bool buildStreamPrefetch_(const Sound *sound, const std::vector<StreamSaveJob> &jobs, std::vector<u8> *out) const;
    bool buildStreamPrefetchFromFile_(const Sound *sound, const std::vector<StreamSaveJob> &jobs, std::vector<u8> *out) const;
    void planNonStreamBinarySave(const Sound *sound, const sead::SafeString &archiveDir, const sead::SafeString &sourceDir, bool inPlace, std::unordered_set<std::string> &seen, std::vector<StreamSaveJob> &out, std::vector<SkippedStream> &outSkipped) const;
    void close_();

    bool validateName_(const sead::SafeString &name, const Item *ignoreItem) const;
    bool validateSharedStream_(BfsarValidateContext& ctx, const Sound* sound) const;
    bool validateSounds_(BfsarValidateContext& ctx) const;
    bool validateWaveSoundOwners_(const BfsarValidateContext& ctx) const;

    u32 getItemOrigFileId_(const Item *item) const
    {
        for (const auto &[mapItem, origFileId] : mItemOrigFileIds)
        {
            if (mapItem == item)
                return origFileId;
        }

        return nw::snd::SoundArchive::INVALID_ID;
    }

    const Item* getOrigFileOwner_(u32 origFileId) const
    {
        for (const auto &[item, itemFileId] : mItemOrigFileIds)
        {
            if (itemFileId == origFileId)
                return item;
        }

        return nullptr;
    }

    bool loadMetadata_(const sead::SafeString &archivePath);
    bool saveMetadata_(const sead::SafeString &archivePath, bool report);
    bool buildMetadata_(std::string *out);

private:
    bool mOpen;
    ArchiveFormat mFormat;
    ArchivePlatform mPlatform;
    sead::HeapSafeString *mFilePath;
    sead::HeapSafeString *mLoadedArchivePath{nullptr};
    bool mCmpbinPreferZstd{false};

    bool mHasPendingSaveFinalize{false};
    sead::FixedSafeString<560> mPendingSaveFinalizeStaged;
    sead::FixedSafeString<560> mPendingSaveFinalizeDest;
    bool mPendingSaveChangesPath{false};
    sead::HeapSafeString *mPendingSavePreviousPath{nullptr};

    sead::Endian::Types mEndian;
    u32 mVersion;
    bool mIncludeStringTable;
    bool mSaveMetadata;
    bool mMetadataUpgradePending = false;
    bool mMetadataFileUnread = false;
    u32 mMetadataUpgradeLoaded = 0;
    u32 mMetadataUpgradeMissing = 0;
    SoundArchivePlayerInfo mSoundArchivePlayerInfo;
    u64 mInfoBaselineSignature{0};

    static constexpr u32 cStreamSoundInfoSizeWithExtension = 0x20;
    static constexpr u32 cStreamSoundInfoSizeWithPrefetch  = 0x24;
    u32 mStreamSoundInfoSize{0};

    std::vector<std::vector<u32>> mFileAttachedGroups;
    std::vector<bool> mFileOriginalIncludeInBfsar;
    std::vector<bool> mFileOriginalHasData;
    std::vector<std::pair<const Item *, u32>> mItemOrigFileIds;
    StreamFileOrder mStreamFileOrder{StreamFileOrder::ByVersion};

    Sound::List mSoundList;

    SoundSet::List mSoundSetList;

    Bank::List mBankList;

    WaveArchive::List mWaveArchiveList;

    Group::List mGroupList;

    Player::List mPlayerList;

    WaveFile::List mWaveFileList;

    SequenceFile::List mSequenceFileList;

    BankFile::List mBankFileList;

    Item::List mGenWaveArchiveList; // ..........so we can display warnings on open

    std::vector<u8*> mExternalGroupBuffers; // Owned buffers loaded from extData/ or dialog
    std::vector<GroupFileSave> mPendingGroupFileSaves;
    std::vector<std::string> mLoadedGroupFilePaths;
    std::vector<std::string> mDeletedGroupFiles;
    const Group* mUnwrittenGroupFile = nullptr;
    bool mCliMode = false;
};
