#pragma once

#include <basis/seadTypes.h>

#include <vector>

namespace aal
{

inline constexpr u32 cAmtaAnalysisRate = 48000;
inline constexpr f32 cAmtaQuietTailLevels[] = { -46.0f, -47.5f };

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
    u32 quietTailFrames = 0;
    u32 summedQuietTailFrames = 0;
    std::vector<AmtaEnvelopePoint> envelope;
};

AmtaLoudness MeasureAmta5Loudness(const std::vector<std::vector<s16>>& channels, u32 sampleRate, bool isLoop, u32 loopStartFrame, bool dropLastSample, f32 quietTailLevel);
u32 CalcAmtaQuietTailFrames(const std::vector<AmtaEnvelopePoint>& envelope, u32 length, f32 level);
AmtaLoudness MeasureAmta4Loudness(const std::vector<std::vector<s16>>& channels, u32 sampleRate, bool hasIntegerMeanSquare);

}
