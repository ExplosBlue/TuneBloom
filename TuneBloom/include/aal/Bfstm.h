#pragma once

#include <basis/seadTypes.h>

#include <vector>

namespace aal
{

inline constexpr u32 cBfstmCoefficientCount = 16;

enum class BfstmEncoding : u8
{
    Pcm8 = 0,
    Pcm16 = 1,
    DspAdpcm = 2
};

struct BfstmChannel
{
    s16 coefficients[cBfstmCoefficientCount] = {};
    u16 predScale = 0;
    s16 yn1 = 0;
    s16 yn2 = 0;
    u16 loopPredScale = 0;
    s16 loopYn1 = 0;
    s16 loopYn2 = 0;
    std::vector<u8> data;
};

struct Bfstm
{
    bool isBigEndian = false;
    u32 version = 0;
    BfstmEncoding encoding = BfstmEncoding::DspAdpcm;
    bool isLoop = false;
    u32 sampleRate = 0;
    u32 loopStartFrame = 0;
    u32 frameCount = 0;
    u32 oneBlockBytes = 0;
    u32 oneBlockSamples = 0;
    u32 sampleDataOffset = 0;
    bool hasOriginalLoop = false;
    u32 originalLoopStartFrame = 0;
    u32 originalLoopEndFrame = 0;
    bool hasRegions = false;
    u32 prefetchFrameCount = 0;
    std::vector<BfstmChannel> channels;
    std::vector<s16> seekHistory;
    std::vector<u8> info;
    u32 streamInfoOffset = 0;
    std::vector<u32> adpcmInfoOffsets;

    u32 getSourceSampleCount() const
    {
        return hasOriginalLoop ? originalLoopEndFrame : frameCount;
    }

    u32 getSourceLoopStartFrame() const
    {
        return hasOriginalLoop ? originalLoopStartFrame : loopStartFrame;
    }
};

bool IsBfstm(const void* data, u64 size);
bool ReadBfstm(const void* data, u64 size, Bfstm* out, const char** outError);
bool ReadBfstmInfo(const void* data, u64 size, Bfstm* out);
bool WriteBfstm(const Bfstm& bfstm, std::vector<u8>* out);
bool CanWriteBfstm(const Bfstm& bfstm, const char** outReason);

void DecodeBfstmChannel(const Bfstm& bfstm, u32 channelIndex, std::vector<s16>* out);
bool EncodeBfstm(const Bfstm& shape, const std::vector<std::vector<s16>>& channels, u32 sampleRate, bool isLoop, u32 loopStartFrame, Bfstm* out, const char** outError);

}
