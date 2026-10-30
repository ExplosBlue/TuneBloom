#pragma once

#include <basis/seadTypes.h>

#include <vector>

namespace nxopus
{

inline constexpr u32 cInfoChunkId        = 0x80000001;
inline constexpr u32 cDataChunkId        = 0x80000004;
inline constexpr u32 cMultiStreamChunkId = 0x80000005;
inline constexpr u32 cChunkHeaderSize    = 0x08;
inline constexpr u32 cInfoChunkSize      = 0x18;
inline constexpr u32 cPacketHeaderSize   = 0x08;
inline constexpr u32 cMaxChannelCount    = 255;

inline constexpr u32 cChannelBitrates[] = { 24000, 32000, 48000, 64000, 96000, 128000, 192000, 256000 };

enum class Error
{
    None,
    NotOpus,
    ChannelCountUnsupported,
    SampleRateUnsupported,
    DataDamaged,
    NoAudibleSamples,
    DecoderCreateFailed,
    DecodeFailed,
    EncoderCreateFailed,
    EncodeFailed
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
    u32 streamCount = 0;
    u32 coupledStreamCount = 0;
    std::vector<u8> channelMapping;
    u64 dataOffset = 0;
    u64 dataSize = 0;
};

bool IsSupportedSampleRate(u32 sampleRate);
bool HasInfoChunk(const void* data, u64 size);
bool ReadInfo(const void* data, u64 size, Info* out);
u64 Measure(const void* data, u64 size);
Error Decode(const void* data, u64 size, PreSkip preSkip, std::vector<std::vector<s16>>* outChannels, Info* outInfo);
u32 MeasureChannelBitrate(const void* data, u64 size);
u32 FindNearestChannelBitrate(u32 bitrate);
u32 SnapChannelBitrate(u32 measured);
bool IsChannelBitratePreset(u32 bitrate);
Error Encode(const std::vector<const std::vector<f32>*>& channels, u32 sampleCount, u32 sampleRate, s32 bitrate, PreSkip preSkip, std::vector<u8>* out);

}
