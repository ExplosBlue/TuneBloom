#pragma once

#include <basis/seadTypes.h>

#include <vector>

namespace bfstm
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
    u32 blockCount = 0;
    u32 oneBlockBytes = 0;
    u32 oneBlockSamples = 0;
    u32 lastBlockPaddedBytes = 0;
    u32 tailBlockBytes = 0;
    u32 sampleDataOffset = 0;
    bool hasOriginalLoop = false;
    u32 originalLoopStartFrame = 0;
    u32 originalLoopEndFrame = 0;
    u32 prefetchFrameCount = 0;
    u64 infoBlockStart = 0;
    u32 infoBlockSize = 0;
    u64 streamInfoStart = 0;
    u64 prefetchBlockStart = 0;
    u64 prefetchDataStart = 0;
    u32 prefetchDataSize = 0;
    u64 sampleDataStart = 0;
    u64 seekDataStart = 0;
    u32 seekRowCount = 0;
    std::vector<BfstmChannel> channels;

    u64 getChannelBytes() const
    {
        if (blockCount == 0)
            return 0;

        return static_cast<u64>(blockCount - 1) * oneBlockBytes + lastBlockPaddedBytes + tailBlockBytes;
    }

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
bool ReadBfstpInfo(const void* data, u64 size, Bfstm* out);
bool ReadBfstmHead(const void* data, u64 size, Bfstm* out, u64* outNeededSize);
bool ReadBfstmLayout(const void* data, u64 size, Bfstm* out);

void CopyBfstmChannel(const Bfstm& bfstm, const void* file, u32 channelIndex, u8* out);

void DecodeBfstmChannel(const Bfstm& bfstm, u32 channelIndex, std::vector<s16>* out);

}
