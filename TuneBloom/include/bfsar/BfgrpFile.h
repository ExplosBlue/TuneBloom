#pragma once

#include <bfsar/InnerFile.h>

#include <VectorMap.h>
#include <VectorSet.h>

class Bank;
class File;
class Group;
class Item;
class SoundSet;
class WaveArchive;
class WaveFile;

class BfgrpFile : public InnerFile
{
    SEAD_RTTI_OVERRIDE(BfgrpFile, InnerFile);

public:
    // static const u32 cIncludeDisabledItemsVersion = 0x00020000;
    static const u32 cSortItemsAlgo2Version = 0x00020100;

public:
    BfgrpFile(sead::Endian::Types endian, u32 version, ArchiveFormat format = ArchiveFormat::BFSAR)
        : InnerFile()
        , mGroup(nullptr)
        , mItemFiles(nullptr)
        , mFiles(nullptr)
        , mFilesItems(nullptr)
        , mWaveSoundSetsWarcs(nullptr)
        , mBanksWarcs(nullptr)
        , mWarcWaveFilesIndexes(nullptr)
        , mGroupTargetWarcs(nullptr)
    {
        mEndian = endian;
        mVersion = version;
        mFormat = format;
    }

    struct EmbeddedFileInfo
    {
        u32 offset;
        u32 size;
    };

    const std::unordered_map<u32, EmbeddedFileInfo>& getEmbeddedFileInfos() const
    {
        return mEmbeddedFileInfos;
    }

    void prepare(
        const Group* group,
        const VectorSet<u32>& itemFiles,
        const std::vector<File>& files,
        const std::unordered_map<u32, VectorSet<const Item *>>& filesItems,
        const std::unordered_map<const SoundSet*, VectorMap<const Group*, const WaveArchive*>>& waveSoundSetsWarcs,
        const std::unordered_map<const Bank*, VectorMap<const Group*, const WaveArchive*>>& banksWarcs,
        const std::unordered_map<const WaveArchive*, std::unordered_map<const WaveFile*, u32>>& warcWaveFilesIndexes,
        const std::unordered_map<const Group*, const WaveArchive*>& groupTargetWarcs) const
    {
        SEAD_ASSERT(!mGroup);
        mGroup = group;

        SEAD_ASSERT(!mItemFiles);
        mItemFiles = &itemFiles;

        SEAD_ASSERT(!mFiles);
        mFiles = &files;

        SEAD_ASSERT(!mFilesItems);
        mFilesItems = &filesItems;

        SEAD_ASSERT(!mWaveSoundSetsWarcs);
        mWaveSoundSetsWarcs = &waveSoundSetsWarcs;

        SEAD_ASSERT(!mBanksWarcs);
        mBanksWarcs = &banksWarcs;

        SEAD_ASSERT(!mWarcWaveFilesIndexes);
        mWarcWaveFilesIndexes = &warcWaveFilesIndexes;

        SEAD_ASSERT(!mGroupTargetWarcs);
        mGroupTargetWarcs = &groupTargetWarcs;
    }

private:
    bool doRead(const void* fileAddr) override
    {
        return false;
    }

    u32 doWrite(sead::FileHandle* handle, sead::WriteStream* stream, bool isLast) const override;

private:
    mutable std::unordered_map<u32, EmbeddedFileInfo> mEmbeddedFileInfos;
    mutable const Group* mGroup;
    mutable const VectorSet<u32>* mItemFiles;
    mutable const std::vector<File>* mFiles;
    mutable const std::unordered_map<u32, VectorSet<const Item *>>* mFilesItems;
    mutable const std::unordered_map<const SoundSet*, VectorMap<const Group*, const WaveArchive*>>* mWaveSoundSetsWarcs;
    mutable const std::unordered_map<const Bank*, VectorMap<const Group*, const WaveArchive*>>* mBanksWarcs;
    mutable const std::unordered_map<const WaveArchive*, std::unordered_map<const WaveFile*, u32>>* mWarcWaveFilesIndexes;
    mutable const std::unordered_map<const Group*, const WaveArchive*>* mGroupTargetWarcs;
};
