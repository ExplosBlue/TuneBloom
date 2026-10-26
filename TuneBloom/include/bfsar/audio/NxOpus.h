#pragma once

#include <basis/seadTypes.h>

#include <vector>

namespace nxopus
{

inline constexpr u32 cInfoChunkId      = 0x80000001;
inline constexpr u32 cDataChunkId      = 0x80000004;
inline constexpr u32 cChunkHeaderSize  = 0x08;
inline constexpr u32 cInfoChunkSize    = 0x18;
inline constexpr u32 cPacketHeaderSize = 0x08;

enum class Error
{
    None,
    NotOpus,
    ChannelCountUnsupported,
    SampleRateUnsupported,
    DataDamaged,
    NoAudibleSamples,
    DecoderCreateFailed,
    DecodeFailed
};

enum class PreSkip
{
    Keep,
    Drop
};

struct Info
{
    u32 channelCount = 0;
    u32 sampleRate = 0;
    u32 preSkip = 0;
    u64 dataOffset = 0;
    u64 dataSize = 0;
};

bool IsSupportedSampleRate(u32 sampleRate);
bool HasInfoChunk(const void* data, u64 size);
bool ReadInfo(const void* data, u64 size, Info* out);
u64 Measure(const void* data, u64 size);
Error Decode(const void* data, u64 size, PreSkip preSkip, std::vector<std::vector<s16>>* outChannels, Info* outInfo);

}
