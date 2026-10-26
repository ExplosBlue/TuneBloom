#pragma once

#include <basis/seadTypes.h>

#include <vector>

namespace aal
{

inline constexpr u32 cAmtaAnalysisRate = 48000;

struct AmtaEnvelopePoint
{
    u32 position = 0;
    f32 power = 0.0f;
};

struct AmtaLoudness
{
    f32 peak = 0.0f;
    f32 rms300 = 0.0f;
    f32 momentaryMax = 0.0f;
    f32 integrated = 0.0f;
    f32 loudnessRange = 0.0f;
    std::vector<AmtaEnvelopePoint> envelope;
};

AmtaLoudness MeasureAmta5Loudness(const std::vector<std::vector<s16>>& channels, u32 sampleRate, bool isLoop, u32 loopStartFrame, bool dropLastSample);
AmtaLoudness MeasureAmta4Loudness(const std::vector<std::vector<s16>>& channels, u32 sampleRate, bool hasIntegerMeanSquare);

}
