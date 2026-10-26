#pragma once

#include <basis/seadTypes.h>

class ByteView
{
public:
    ByteView(const void* data, u64 size, bool isBigEndian)
        : mData(static_cast<const u8*>(data))
        , mSize(size)
        , mIsBigEndian(isBigEndian)
    {
    }

    static bool IsBigEndianMark(const void* data, u64 size, u64 offset)
    {
        const u8* bytes = static_cast<const u8*>(data);

        return bytes && offset + 1 < size && bytes[offset] == 0xFE && bytes[offset + 1] == 0xFF;
    }

    static bool IsByteOrderMark(const void* data, u64 size, u64 offset)
    {
        const u8* bytes = static_cast<const u8*>(data);

        if (!bytes || offset + 1 >= size)
            return false;

        return (bytes[offset] == 0xFE && bytes[offset + 1] == 0xFF) || (bytes[offset] == 0xFF && bytes[offset + 1] == 0xFE);
    }

    bool has(u64 offset, u64 size) const
    {
        return mData && offset <= mSize && size <= mSize - offset;
    }

    u64 size() const
    {
        return mSize;
    }

    const u8* at(u64 offset) const
    {
        return mData + offset;
    }

    bool isBigEndian() const
    {
        return mIsBigEndian;
    }

    u8 readU8(u64 offset) const
    {
        return mData[offset];
    }

    u16 readU16(u64 offset) const
    {
        const u8* bytes = mData + offset;

        return mIsBigEndian ? static_cast<u16>(bytes[0] << 8 | bytes[1]) : static_cast<u16>(bytes[1] << 8 | bytes[0]);
    }

    s16 readS16(u64 offset) const
    {
        return static_cast<s16>(readU16(offset));
    }

    u32 readU32(u64 offset) const
    {
        const u8* bytes = mData + offset;

        if (mIsBigEndian)
            return static_cast<u32>(bytes[0]) << 24 | static_cast<u32>(bytes[1]) << 16 | static_cast<u32>(bytes[2]) << 8 | bytes[3];

        return static_cast<u32>(bytes[3]) << 24 | static_cast<u32>(bytes[2]) << 16 | static_cast<u32>(bytes[1]) << 8 | bytes[0];
    }

private:
    const u8* mData;
    u64 mSize;
    bool mIsBigEndian;
};
