#pragma once

#include <basis/seadTypes.h>

#include <vector>

namespace AudioProcessing
{

inline s16 toPcm16(float value)
{
    value = value < -1.0f ? -1.0f : (value > 1.0f ? 1.0f : value);

    const float scaled = value * (value < 0.0f ? 32768.0f : 32767.0f);
    return static_cast<s16>(static_cast<double>(scaled) + (scaled < 0.0f ? -0.5 : 0.5));
}

std::vector<float> resample(const std::vector<float>& data, u32 fromRate, u32 toRate);

void mixToMono(const std::vector<std::vector<float>>& channels, std::vector<float>& out);

enum class ChannelMode { Stereo, Left, Right, MonoMix };
std::vector<std::vector<float>> selectChannels(const std::vector<std::vector<float>>& channels, ChannelMode mode);

std::vector<std::vector<float>> applySpeed(const std::vector<std::vector<float>>& channels, float speedMultiplier);

enum class CurveShape { SingleCurve, DoubleCurve, Hold, Stairs, SmoothStairs, Pulse, Wave, HalfSine, Num };

struct CurvePoint
{
    float x = 0.0f;
    float y = 1.0f;
    float tension = 0.0f;
    CurveShape shape = CurveShape::SingleCurve;
};

using Curve = std::vector<CurvePoint>;

float curveValue(const Curve& curve, float x);
bool isCurveFlat(const Curve& curve);
void applyGainCurve(std::vector<float>& channel, const Curve& curve, u32 curveSamples);

}
