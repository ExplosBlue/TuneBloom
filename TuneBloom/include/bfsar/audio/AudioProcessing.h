#pragma once

#include <basis/seadTypes.h>

#include <vector>

namespace AudioProcessing
{

inline float fromPcm16(s16 value)
{
    return static_cast<float>(value) / 32768.0f;
}

inline s16 toPcm16(float value)
{
    const double scaled = static_cast<double>(value) * 32768.0;
    const double rounded = scaled < 0.0 ? scaled - 0.5 : scaled + 0.5;

    return static_cast<s16>(rounded < -32768.0 ? -32768.0 : (rounded > 32767.0 ? 32767.0 : rounded));
}

std::vector<float> resample(const std::vector<float>& data, u32 fromRate, u32 toRate);

void mixToMono(const std::vector<std::vector<float>>& channels, std::vector<float>& out);
std::vector<std::vector<float>> mixToStereo(const std::vector<std::vector<float>>& channels);

void decodeMidSide(std::vector<s16>& mid, std::vector<s16>& side);
void encodeMidSide(std::vector<s16>& left, std::vector<s16>& right);

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
