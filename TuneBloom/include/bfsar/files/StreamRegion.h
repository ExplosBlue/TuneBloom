#pragma once

#include <bfsar/model/StateHash.h>

#include <prim/seadSafeString.h>
#include <stream/seadStream.h>

#include <vector>

namespace nw { namespace snd { namespace internal
{
    class StreamSoundFileReader;
} } }

struct StreamRegion
{
    u32 start = 0;
    u32 end = 0;
    u32 unknown68 = 0;
    sead::FixedSafeString<64> name;
};

struct StreamFileLayout
{
    static constexpr u32 cRegionCountMax = 127;
    static constexpr s32 cRegionNameLengthMax = 63;

    std::vector<StreamRegion> regions;
    u32 regionRecordBytes = 0;

    u32 version = 0;
    bool hasUnknown4C = false;
    u32 unknown4C = 0;

    void mixInto(StateHash& hash) const;

    bool canAddRegion() const;
    bool canStoreRegionNames() const;

    s32 addRegion(s32 afterNo, u32 sampleCount);
    void removeRegion(s32 regionNo);
    void moveRegion(s32 fromNo, s32 toNo);
    bool setRegionRange(s32 regionNo, u32 start, u32 end, u32 sampleCount);
    bool setRegionName(s32 regionNo, const sead::SafeString& name);
    void setRegions(std::vector<StreamRegion>&& newRegions);
    u32 removeRegionsAfter(u32 sampleCount);

    static bool IsRegionNameValid(const sead::SafeString& name);
    static u32 GetRegionLengthMin(bool isDspAdpcm, bool isPcm8);
};

struct StreamRegionSampleData
{
    const u8* const* channels = nullptr;
    u32 channelCount = 0;
    u32 channelBytes = 0;
    bool isDspAdpcm = false;
};

enum class StreamRegionReadResult
{
    None,
    Read,
    Invalid
};

struct WavRegions
{
    std::vector<StreamRegion> regions;
    u32 sampleRate = 0;
    u32 sampleCount = 0;
};

bool ReadWavRegions(WavRegions* out, const sead::SafeString& path);

u32 CalcWavRegionsSize(const std::vector<StreamRegion>& regions);
void WriteWavRegions(sead::WriteStream* stream, const std::vector<StreamRegion>& regions);

StreamRegionReadResult ReadStreamFileLayout(StreamFileLayout* layout, const nw::snd::internal::StreamSoundFileReader& reader, u32 strmFileSize);
void WriteStreamRegions(sead::WriteStream* stream, const StreamFileLayout& layout, const StreamRegionSampleData& sampleData);
