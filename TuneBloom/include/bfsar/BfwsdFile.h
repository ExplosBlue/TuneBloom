#pragma once

#include <bfsar/InnerFile.h>

#include <unordered_map>

class SoundSet;
class WaveArchive;
class WaveFile;

class BfwsdFile : public InnerFile
{
    SEAD_RTTI_OVERRIDE(BfwsdFile, InnerFile);

public:
    BfwsdFile(sead::Endian::Types endian, u32 version, ArchiveFormat format = ArchiveFormat::BFSAR)
        : InnerFile()
        , mSoundSet(nullptr)
        , mWaveArchive(nullptr)
        , mWaveArchiveWaveFilesIndexes(nullptr)
        , mUpdateWriteInfo(true)
    {
        mEndian = endian;
        mVersion = version;
        mFormat = format;
    }

    void prepare(const SoundSet* soundSet, const WaveArchive* warc, const std::unordered_map<const WaveArchive*, std::unordered_map<const WaveFile*, u32>>& waveFilesIndexes, bool updateWriteInfo) const
    {
        SEAD_ASSERT(!mSoundSet);
        mSoundSet = soundSet;

        SEAD_ASSERT(!mWaveArchive);
        mWaveArchive = warc;

        SEAD_ASSERT(!mWaveArchiveWaveFilesIndexes);

        const auto& it = waveFilesIndexes.find(warc);
        if (it != waveFilesIndexes.end())
        {
            mWaveArchiveWaveFilesIndexes = &it->second;
        }

        mUpdateWriteInfo = updateWriteInfo;
    }

    static bool isFilterSupportedVersion(u32 version)
    {
        return version >= makeVersion(1, 0, 1);
    }

    static u32 getAuxBusCount(ArchiveFormat format)
    {
        return format == ArchiveFormat::BCSAR ? 2 : 3;
    }

private:
    bool doRead(const void* fileAddr) override
    {
        return false;
    }

    u32 doWrite(sead::FileHandle* handle, sead::WriteStream* stream, bool isLast) const override;

    bool updateWriteInfo_() const override
    {
        return mUpdateWriteInfo;
    }

private:
    mutable const SoundSet* mSoundSet;
    mutable const WaveArchive* mWaveArchive;
    mutable const std::unordered_map<const WaveFile*, u32>* mWaveArchiveWaveFilesIndexes;
    mutable bool mUpdateWriteInfo;
};
