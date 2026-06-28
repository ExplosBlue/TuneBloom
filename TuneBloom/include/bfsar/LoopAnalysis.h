#pragma once

#include <cstdint>
#include <vector>

namespace LoopAnalysis {

using u32 = uint32_t;
using s32 = int32_t;
using s64 = int64_t;

u32 estimatePeriod(const std::vector<float>& mono, u32 sampleRate);

u32 nearestZeroUp(const std::vector<float>& mono, u32 i);

u32 refineEnd(const std::vector<float>& mono, u32 loopStart, u32 loopEnd, u32 period);

struct SuggestResult { bool ok; u32 loopStart; u32 loopEnd; u32 period; };
SuggestResult suggestLoop(const std::vector<float>& mono, u32 sampleRate);

struct FrameSnapResult { u32 loopStart; u32 loopEnd; };
FrameSnapResult snapFramePeriod(u32 loopStart, u32 loopEnd, u32 period, u32 bufferSampleCount);

struct BakeResult { std::vector<std::vector<float>> channels; u32 crossfadeSamples; };
BakeResult buildBaked(const std::vector<std::vector<float>>& channels, u32 loopStart, u32 loopEnd, u32 sampleRate, float crossfadeMs, bool equalPower);

}