#pragma once

#include <bfsar/InnerFile.h>

#include <VectorSet.h>

class WaveFile;

class BfwarFile : public InnerFile
{
    SEAD_RTTI_OVERRIDE(BfwarFile, InnerFile);

public:
    BfwarFile(sead::Endian::Types endian, u32 version, const VectorSet<const WaveFile*>& waveFiles)
        : InnerFile()
        , mWaveFiles(waveFiles)
        , mUpdateWriteInfo(true)
    {
        mEndian = endian;
        mVersion = version;
    }

    void prepare(bool updateWriteInfo) const
    {
        mUpdateWriteInfo = updateWriteInfo;
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
    const VectorSet<const WaveFile*>& mWaveFiles;
    mutable bool mUpdateWriteInfo;
};
