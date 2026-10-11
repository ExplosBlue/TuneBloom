#pragma once

#include <bfsar/files/InnerFile.h>

#include <vector>

class BfstpFile : public InnerFile
{
    SEAD_RTTI_OVERRIDE(BfstpFile, InnerFile);

public:
    static constexpr u32 cBlocksPerBufferTime = 5;

public:
    BfstpFile(std::vector<u8>&& bytes, sead::Endian::Types endian, ArchiveFormat format)
        : InnerFile()
        , mBytes(std::move(bytes))
    {
        mEndian = endian;
        mFormat = format;
    }

    static bool CheckLayout(const void* data, u64 size);
    static bool Build(std::vector<u8>* out, const void* strmFile, u64 strmFileSize, u32 streamBufferTimes);
    static bool BuildFromStreamFile(std::vector<u8>* out, const char* strmFilePath, u32 streamBufferTimes);

private:
    bool doRead(const void* fileAddr) override
    {
        return false;
    }

    u32 doWrite(sead::FileHandle* handle, sead::WriteStream* stream, bool isLast) const override;

private:
    std::vector<u8> mBytes;
};
