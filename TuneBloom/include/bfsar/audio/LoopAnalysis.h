#pragma once

#include <basis/seadTypes.h>

#include <vector>

namespace LoopAnalysis
{

u32 estimatePeriod(const std::vector<float>& mono, u32 sampleRate);

struct SuggestResult { bool ok; u32 loopStart; u32 loopEnd; };
SuggestResult suggestLoop(const std::vector<float>& mono, u32 sampleRate);

struct FrameSnapResult { u32 loopStart; u32 loopEnd; };
FrameSnapResult snapFramePeriod(u32 loopStart, u32 loopEnd, u32 period, u32 bufferSampleCount);

}
