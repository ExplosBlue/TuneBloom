#pragma once

#include <bfsar/writer/FileWriter.h>

enum class ArchiveFormat
{
    BFSAR,
    BCSAR
};

#include <snd/ut/ut_BinaryFileFormat.h>

#include <prim/seadPtrUtil.h>
#include <prim/seadRuntimeTypeInfo.h>

class InnerFile
{
    SEAD_RTTI_BASE(InnerFile);

public:
    InnerFile()
        : mEndian(sead::Endian::eBig)
        , mVersion(0x00010000)

        , mWritePos(0xFFFFFFFF)
        , mWriteSize(0xFFFFFFFF)
    {
    }

    virtual ~InnerFile()
    {
    }

    virtual bool read(const void* fileAddr)
    {
        const nw::ut::BinaryFileHeader& header = *static_cast<const nw::ut::BinaryFileHeader*>(fileAddr);

        mEndian = nw::ut::GetFileEndian(header);
        
        sead::Endian::Types prevEndian = sFileEndian;
        sFileEndian = mEndian;

        mVersion = header.version;
        bool success = doRead(fileAddr);

        sFileEndian = prevEndian;

        return success;
    }

    virtual u32 write(sead::FileHandle* handle, sead::WriteStream* stream, sead::Endian::Types prevEndian, bool isLast) const
    {
        stream->setBinaryEndian(mEndian);

        if (updateWriteInfo_())
        {
            if (mWritePos == 0xFFFFFFFF)
            {
                mWritePos = handle->getCurrentSeekPos();
            }
        }

        u32 fileSize = doWrite(handle, stream, isLast);

        if (updateWriteInfo_())
        {
            if (mWriteSize == 0xFFFFFFFF)
            {
                mWriteSize = handle->getCurrentSeekPos() - mWritePos;
            }
        }

        stream->setBinaryEndian(prevEndian);

        return fileSize;
    }

    ArchiveFormat getFormat() const
    {
        return mFormat;
    }

    sead::Endian::Types getEndian() const
    {
        return mEndian;
    }

    // Returns the endianness of multi-byte parameter values within sequence data.
    // For 'C' format archives (BCSAR), parameter endianness is the OPPOSITE of
    // the file header BOM; for 'F' format (BFSAR), they MATCH.
    sead::Endian::Types getSeqParamEndian() const
    {
        if (mFormat == ArchiveFormat::BCSAR)
            return mEndian == sead::Endian::eLittle ? sead::Endian::eBig : sead::Endian::eLittle;
        return mEndian;
    }

    void setFormat(ArchiveFormat format) const
    {
        mFormat = format;
    }

    virtual void drawUI();

    u32 getWritePos() const
    {
        return mWritePos;
    }

    u32 getWriteSize() const
    {
        return mWriteSize;
    }

    void clearWriteInfo() const
    {
        mWritePos = 0xFFFFFFFF;
        mWriteSize = 0xFFFFFFFF;
    }

protected:
    virtual bool doRead(const void* fileAddr) = 0;
    virtual u32 doWrite(sead::FileHandle* handle, sead::WriteStream* stream, bool isLast) const = 0;

    virtual bool updateWriteInfo_() const
    {
        return true;
    }

protected:
    mutable sead::Endian::Types mEndian;
    mutable u32 mVersion;
    mutable ArchiveFormat mFormat{ArchiveFormat::BFSAR};

    mutable u32 mWritePos;
    mutable u32 mWriteSize;

    friend class File;
};
