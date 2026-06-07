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

    void create(ArchiveFormat format = ArchiveFormat::BFSAR);
    bool open(u8* bfsarFile, const sead::SafeString& filePath, sead::Heap* heap);
    bool save();
    bool saveAs(const sead::SafeString& filePath);
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

    // BFSAR: version = (0 << 24) | (major << 16) | (minor << 8) | sub
    // BCSAR: version = (major << 24) | (minor << 16) | (0 << 8) | sub
    u32 getDecodedMajor() const
    {
        if (mFormat == ArchiveFormat::BCSAR)
            return (mVersion >> 24) & 0xFF;
        return (mVersion >> 16) & 0xFF;
    }

    u32 getDecodedMinor() const
    {
        if (mFormat == ArchiveFormat::BCSAR)
            return (mVersion >> 16) & 0xFF;
        return (mVersion >> 8) & 0xFF;
    }

    u32 getDecodedPatch() const
    {
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
            return isVersionOrLater(2, 3, 1);

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

    u32 getVersionForBfwsd() const
    {
        if (mFormat == ArchiveFormat::BCSAR)
            return makeVersion(1, 0, 1);

        if (isVersionOrLater(2, 1, 0))
            return makeVersion(1, 0, 0);

        return makeVersion(0, 1, 0);
    }

    u32 getVersionForBfbnk() const
    {
        if (mFormat == ArchiveFormat::BCSAR)
            return makeVersion(1, 0, 1);

        return makeVersion(0, 1, 0);
    }

    u32 getVersionForBfwar() const
    {
        if (mFormat == ArchiveFormat::BCSAR)
            return makeVersion(1, 0, 0);

        if (isVersionOrLater(2, 1, 0))
            return makeVersion(1, 0, 0);

        return makeVersion(0, 1, 0);
    }

    u32 getVersionForBfgrp() const
    {
        if (mFormat == ArchiveFormat::BCSAR)
        {
            if (isVersionOrLater(2, 1, 0))
                return makeVersion(1, 1, 0);

            return makeVersion(1, 0, 0);
        }

        if (isVersionOrLater(2, 1, 0))
            return makeVersion(1, 1, 0);

        return makeVersion(0, 1, 0);
    }

    u32 getVersionForBfseq() const
    {
        if (mFormat == ArchiveFormat::BCSAR)
        {
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
    bool validate_();

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

private:
    bool open_(const nw::snd::MemorySoundArchive& soundArchive, sead::Heap* heap);
    void save_(sead::FileHandle& handle);
    void close_();

    bool validateName_(const sead::SafeString& name, const Item::List& list, const Item* ignoreItem = nullptr) const;

private:
    bool mOpen;
    ArchiveFormat mFormat;
    sead::HeapSafeString* mFilePath;

    sead::Endian::Types mEndian;
    u32 mVersion;
    bool mIncludeStringTable;
    SoundArchivePlayerInfo mSoundArchivePlayerInfo;

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
};
