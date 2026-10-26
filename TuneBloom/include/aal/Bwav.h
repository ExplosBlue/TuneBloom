#pragma once

#include <basis/seadTypes.h>

#include <vector>

namespace aal
{

inline constexpr u32 cBwavNoLoop              = 0xFFFFFFFF;
inline constexpr u32 cBwavPrefetchSampleCount = 0x3800;
inline constexpr u32 cBwavCoefficientCount    = 16;
inline constexpr u32 cBwavChannelMax          = 16;

enum class BwavCodec : u16
{
    Pcm16 = 0,
    DspAdpcm = 1,
    Opus = 2
};

enum class BwavPan : u16
{
    Left = 0,
    Right = 1,
    Center = 2
};

struct BwavChannel
{
    BwavCodec codec = BwavCodec::DspAdpcm;
    BwavPan pan = BwavPan::Center;
    u32 sampleRate = 0;
    u32 fullSampleCount = 0;
    u32 sampleCount = 0;
    s16 coefficients[cBwavCoefficientCount] = {};
    u32 fullDataOffset = 0;
    u32 reserved = 1;
    u32 loopEndFrame = cBwavNoLoop;
    u32 loopStartFrame = 0;
    u16 loopPredScale = 0;
    s16 loopYn1 = 0;
    s16 loopYn2 = 0;
    u16 padding = 0;
    std::vector<u8> data;
};

struct Bwav
{
    bool isBigEndian = false;
    u16 version = 1;
    u32 sampleCrc = 0;
    u16 prefetchFlag = 0;
    std::vector<BwavChannel> channels;
    std::vector<u8> trailing;

    bool isPrefetch() const
    {
        return prefetchFlag != 0;
    }

    bool isLoop() const
    {
        return !channels.empty() && channels.front().loopEndFrame != cBwavNoLoop;
    }
};

bool IsBwav(const void* data, u64 size);
bool ReadBwav(const void* data, u64 size, Bwav* out, const char** outError);
std::vector<u8> WriteBwav(const Bwav& bwav);
u64 MeasureBwav(const void* data, u64 size);

u32 CalcBwavSampleCrc(const Bwav& bwav);
Bwav MakeBwavPrefetch(const Bwav& full, u32 sampleCount, u16 prefetchFlag);
bool IsBwavComplete(const Bwav& bwav);

bool CanSetBwavLoop(const Bwav& bwav, bool isLoop, u32 loopStartFrame);
void CopyBwavLoop(const Bwav& source, Bwav* target);
void SetBwavLoop(Bwav* bwav, bool isLoop, u32 loopStartFrame);
void DecodeBwavChannel(const Bwav& bwav, u32 channelIndex, std::vector<s16>* out);
Bwav EncodeBwav(const std::vector<std::vector<s16>>& channels, u32 sampleRate, bool isLoop, u32 loopStartFrame);

}
