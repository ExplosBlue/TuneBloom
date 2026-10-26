#pragma once

#include <basis/seadTypes.h>

#include <cstring>
#include <utility>
#include <vector>

class ByteBuilder
{
public:
    explicit ByteBuilder(bool isBigEndian)
        : mBytes()
        , mIsBigEndian(isBigEndian)
    {
    }

    ByteBuilder(std::vector<u8> bytes, bool isBigEndian)
        : mBytes(std::move(bytes))
        , mIsBigEndian(isBigEndian)
    {
    }

    u64 size() const
    {
        return mBytes.size();
    }

    bool isBigEndian() const
    {
        return mIsBigEndian;
    }

    const std::vector<u8>& bytes() const
    {
        return mBytes;
    }

    std::vector<u8> take()
    {
        return std::move(mBytes);
    }

    void resize(u64 size)
    {
        mBytes.resize(size, 0);
    }

    void reserve(u64 size)
    {
        mBytes.reserve(size);
    }

    void appendU8(u8 value)
    {
        mBytes.push_back(value);
    }

    void appendU16(u16 value)
    {
        const u64 offset = mBytes.size();

        mBytes.resize(offset + sizeof(u16));
        putU16(offset, value);
    }

    void appendU32(u32 value)
    {
        const u64 offset = mBytes.size();

        mBytes.resize(offset + sizeof(u32));
        putU32(offset, value);
    }

    void appendBytes(const void* data, u64 size)
    {
        const u8* bytes = static_cast<const u8*>(data);

        mBytes.insert(mBytes.end(), bytes, bytes + size);
    }

    void appendZeros(u64 count)
    {
        mBytes.resize(mBytes.size() + count, 0);
    }

    void alignTo(u64 alignment)
    {
        const u64 remainder = mBytes.size() % alignment;

        if (remainder != 0)
            appendZeros(alignment - remainder);
    }

    void putU8(u64 offset, u8 value)
    {
        mBytes[offset] = value;
    }

    void putU16(u64 offset, u16 value)
    {
        mBytes[offset + (mIsBigEndian ? 0 : 1)] = static_cast<u8>(value >> 8);
        mBytes[offset + (mIsBigEndian ? 1 : 0)] = static_cast<u8>(value);
    }

    void putU32(u64 offset, u32 value)
    {
        for (u32 i = 0; i < sizeof(u32); i++)
            mBytes[offset + (mIsBigEndian ? sizeof(u32) - 1 - i : i)] = static_cast<u8>(value >> (8 * i));
    }

    void putBytes(u64 offset, const void* data, u64 size)
    {
        std::memcpy(mBytes.data() + offset, data, size);
    }

private:
    std::vector<u8> mBytes;
    bool mIsBigEndian;
};
