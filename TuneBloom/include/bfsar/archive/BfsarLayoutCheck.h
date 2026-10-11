#pragma once

#include <snd/snd_MemorySoundArchive.h>
#include <snd/snd_Util.h>
#include <snd/snd_WaveSoundFile.h>

#include <bit>

struct ByteRange
{
    ByteRange() = default;

    ByteRange(const void* data, u64 size)
        : begin(reinterpret_cast<uintptr_t>(data))
        , end(begin + size)
        , bufferEnd(end)
    {
    }

    ByteRange(const void* data, u64 size, const ByteRange& buffer)
        : begin(reinterpret_cast<uintptr_t>(data))
        , end(begin + size)
        , bufferEnd(buffer.bufferEnd)
    {
    }

    const void* data() const
    {
        return reinterpret_cast<const void*>(begin);
    }

    u64 size() const
    {
        return end - begin;
    }

    u64 sizeToBufferEnd() const
    {
        return bufferEnd - begin;
    }

    bool contains(const void* ptr, u64 size) const
    {
        const uintptr_t address = reinterpret_cast<uintptr_t>(ptr);

        return begin != 0 && begin <= address && address <= end && size <= end - address;
    }

    template <typename T>
    bool containsStruct(const T* ptr) const
    {
        return contains(ptr, sizeof(T));
    }

    bool containsTable(const void* table, u64 itemSize) const
    {
        if (!contains(table, sizeof(nw::ut::ResU32)))
            return false;

        const u64 count = *static_cast<const nw::ut::ResU32*>(table);

        return contains(table, sizeof(nw::ut::ResU32) + count * itemSize);
    }

    bool containsOptions(const nw::snd::internal::Util::BitFlag* options) const
    {
        if (!containsStruct(options))
            return false;

        const u64 valueCount = std::popcount(static_cast<u32>(options->bitFlag));

        return contains(options, sizeof(nw::ut::ResU32) * (1 + valueCount));
    }

    uintptr_t begin = 0;
    uintptr_t end = 0;
    uintptr_t bufferEnd = 0;
};

struct ArchiveLayoutVersion
{
    bool streamTrackInfo;
    bool streamSend;
    bool streamFilter;
};

bool CheckSoundArchiveHeader(const void* data, u32 size);
bool CheckSoundArchiveLayout(const nw::snd::MemorySoundArchive& archive, u32 size, const ArchiveLayoutVersion& version);
const char* FindArchiveString(const nw::snd::MemorySoundArchive& archive, const ByteRange& file, u32 stringId);

bool CheckInnerFileBlocks(const ByteRange& file);
bool CheckGroupFileLayout(const ByteRange& file);
bool CheckWaveArchiveLayout(const ByteRange& file);
bool CheckWaveSoundEntry(const ByteRange& file, const nw::snd::internal::WaveSoundFile::InfoBlockBody& info, u32 index);

const nw::snd::internal::Util::WaveIdTable* FindBankWaveIdTable(const ByteRange& file);
const nw::snd::internal::Util::WaveIdTable* FindWaveSoundWaveIdTable(const ByteRange& file);
