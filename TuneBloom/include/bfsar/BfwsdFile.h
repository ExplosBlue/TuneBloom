#pragma once

#include <bfsar/InnerFile.h>

class WaveArchive;

class BfwsdFile : public InnerFile
{
    SEAD_RTTI_OVERRIDE(BfwsdFile, InnerFile);

public:
    BfwsdFile()
        : InnerFile()
        , mWaveArchive(nullptr)
    {
    }

    void prepare(const WaveArchive* warc)
    {
        SEAD_ASSERT(!mWaveArchive);
        mWaveArchive = warc;
    }

private:
    void doRead(const void* fileAddr) override
    {
    }

    u32 doWrite(sead::FileHandle* handle, sead::WriteStream* stream, bool isLast) const override;

private:
    mutable const WaveArchive* mWaveArchive;
};
