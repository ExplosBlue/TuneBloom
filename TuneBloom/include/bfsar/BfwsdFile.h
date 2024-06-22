#pragma once

#include <bfsar/InnerFile.h>

class SoundSet;
class WaveArchive;

class BfwsdFile : public InnerFile
{
    SEAD_RTTI_OVERRIDE(BfwsdFile, InnerFile);

public:
    BfwsdFile()
        : InnerFile()
        , mSoundSet(nullptr)
        , mWaveArchive(nullptr)
        , mUpdateWriteInfo(true)
    {
    }

    void prepare(const SoundSet* soundSet, const WaveArchive* warc, bool updateWriteInfo) const
    {
        SEAD_ASSERT(!mSoundSet);
        mSoundSet = soundSet;

        SEAD_ASSERT(!mWaveArchive);
        mWaveArchive = warc;

        mUpdateWriteInfo = updateWriteInfo;
    }

private:
    void doRead(const void* fileAddr) override
    {
    }

    u32 doWrite(sead::FileHandle* handle, sead::WriteStream* stream, bool isLast) const override;

    bool updateWriteInfo_() const override
    {
        return mUpdateWriteInfo;
    }

private:
    mutable const SoundSet* mSoundSet;
    mutable const WaveArchive* mWaveArchive;
    mutable bool mUpdateWriteInfo;
};
