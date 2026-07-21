#pragma once

#include <bfsar/Bank.h>
#include <bfsar/InnerFile.h>
#include <bfsar/BankFile.h>
#include <bfsar/Group.h>
#include <bfsar/Player.h>
#include <bfsar/SequenceFile.h>
#include <bfsar/Sound.h>
#include <bfsar/SoundSet.h>
#include <bfsar/WaveArchive.h>
#include <bfsar/WaveFile.h>

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
    bool open(u8* bfsarFile, u32 bfsarSize, const sead::SafeString& filePath, sead::Heap* heap);

    struct StreamSaveJob
    {
        const Sound *sound = nullptr;
        std::string savePath;
        std::string srcPath;
        u64 signature = 0;
        bool copyOnly = false;
    };

    bool save();
    bool saveAs(const sead::SafeString &filePath);
    bool saveBackup(const sead::SafeString &path);

    void planStreamSaves(std::vector<StreamSaveJob> &out) const;
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
        u32 groups = 0;
        u32 duplicatesRemoved = 0;
        u64 bytesSaved = 0;
    };

    std::vector<WaveDuplicateGroup> findDuplicateWaves();
    WaveMergeResult mergeDuplicateWaves(const std::vector<WaveDuplicateGroup>& groups);

    std::vector<WaveFile*> findUnusedWaveFiles();
    u32 removeUnusedWaveFiles(const std::vector<WaveFile*>& unused);

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

    const char* getArchiveMagic() const
    {
        return mFormat == ArchiveFormat::BCSAR ? "CSAR" : "FSAR";
    }

    const char* getSeqMagic() const
    {
        return mFormat == ArchiveFormat::BCSAR ? "CSEQ" : "FSEQ";
    }

    const char* getBankMagic() const
    {
        return mFormat == ArchiveFormat::BCSAR ? "CBNK" : "FBNK";
    }

    const char* getWsdMagic() const
    {
        return mFormat == ArchiveFormat::BCSAR ? "CWSD" : "FWSD";
    }

    const char* getWarMagic() const
    {
        return mFormat == ArchiveFormat::BCSAR ? "CWAR" : "FWAR";
    }

    const char* getGrpMagic() const
    {
        return mFormat == ArchiveFormat::BCSAR ? "CGRP" : "FGRP";
    }

    const char* getStmMagic() const
    {
        return mFormat == ArchiveFormat::BCSAR ? "CSTM" : "FSTM";
    }

    const char* getWavMagic() const
    {
        return mFormat == ArchiveFormat::BCSAR ? "CWAV" : "FWAV";
    }

public:
    void setCmpbinPreferZstd(bool v) { mCmpbinPreferZstd = v; }
    bool getCmpbinPreferZstd() const { return mCmpbinPreferZstd; }

private:
    bool open_(const nw::snd::MemorySoundArchive &soundArchive, u32 bfsarSize, sead::Heap *heap);
    void save_(sead::FileHandle &handle, const sead::SafeString *metadataPathOverride = nullptr, bool writeStreams = true);
    bool saveArchiveFile_(const sead::SafeString &path);
    void planNonStreamBinarySave_(const Sound *sound, const sead::SafeString &archiveDir, const sead::SafeString &sourceDir, bool inPlace, std::unordered_set<std::string> &seen, std::vector<StreamSaveJob> &out) const;
    void close_();

    bool validateName_(const sead::SafeString &name, const Item::List &list, const Item *ignoreItem = nullptr) const;

    u32 getItemOrigFileId_(const Item *item) const
    {
        for (const auto &[mapItem, origFileId] : mItemOrigFileIds)
        {
            if (mapItem == item)
                return origFileId;
        }

        return nw::snd::SoundArchive::INVALID_ID;
    }

    void readNamesFromMetadata_(const sead::SafeString &filePath);

private:
    bool mOpen;
    ArchiveFormat mFormat;
    ArchivePlatform mPlatform;
    sead::HeapSafeString *mFilePath;
    sead::HeapSafeString *mLoadedArchivePath{nullptr};
    bool mCmpbinPreferZstd{false};

    sead::Endian::Types mEndian;
    u32 mVersion;
    bool mIncludeStringTable;
    bool mSaveMetadata;
    SoundArchivePlayerInfo mSoundArchivePlayerInfo;

    std::vector<std::vector<u32>> mFileAttachedGroups;
    std::vector<bool> mFileOriginalIncludeInBfsar;
    std::vector<std::pair<const Item *, u32>> mItemOrigFileIds;

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
    bool mCliMode = false;
};
