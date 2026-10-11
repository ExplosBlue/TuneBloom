#include <bfsar/audio/AudioProcessing.h>

#include <algorithm>
#include <cmath>

namespace AudioProcessing
{

constexpr u32 cSpeedRateBase = 1000000;

constexpr float cCurvePi           = 3.14159265358979f;
constexpr float cCurveTensionPower = 5.0f;
constexpr float cCurveCountBase    = 4.0f;
constexpr int   cCurveCountMax     = 16;

std::vector<float> resample(const std::vector<float>& data, u32 fromRate, u32 toRate)
{
    if (fromRate == toRate || data.empty())
        return data;

    const double ratio = static_cast<double>(toRate) / static_cast<double>(fromRate);
    const double step = static_cast<double>(fromRate) / static_cast<double>(toRate);
    const u32 n = static_cast<u32>(std::lround(data.size() * ratio));
    const size_t last = data.size() - 1;
    const float* src = data.data();
    std::vector<float> out(n);
    float* dst = out.data();

    for (u32 i = 0; i < n; i++)
    {
        const double t = i * step;
        const size_t i0 = static_cast<size_t>(t);

        if (i0 >= last)
        {
            dst[i] = (i0 == last) ? src[last] : 0.0f;
            continue;
        }

        const double frac = t - static_cast<double>(i0);
        const float a = src[i0];
        const float b = src[i0 + 1];
        dst[i] = static_cast<float>(a + (b - a) * frac);
    }

    return out;
}

void mixToMono(const std::vector<std::vector<float>>& channels, std::vector<float>& out)
{
    if (channels.empty())
    {
        out.clear();
        return;
    }

    out.assign(channels[0].begin(), channels[0].end());

    if (channels.size() == 1)
        return;

    const size_t length = out.size();
    const float inverseChannelCount = 1.0f / static_cast<float>(channels.size());

    for (size_t c = 1; c < channels.size(); c++)
    {
        const size_t count = std::min(length, channels[c].size());
        const float* src = channels[c].data();

        for (size_t i = 0; i < count; i++)
            out[i] += src[i];
    }

    for (size_t i = 0; i < length; i++)
        out[i] *= inverseChannelCount;
}

std::vector<std::vector<float>> selectChannels(const std::vector<std::vector<float>>& channels, ChannelMode mode)
{
    std::vector<std::vector<float>> out;

    if (channels.empty())
        return out;

    switch (mode)
    {
        case ChannelMode::Stereo:
            out = channels;
            break;

        case ChannelMode::Left:
            out.push_back(channels[0]);
            break;

        case ChannelMode::Right:
            out.push_back(channels.size() > 1 ? channels[1] : channels[0]);
            break;

        case ChannelMode::MonoMix:
            out.emplace_back();
            mixToMono(channels, out.back());
            break;
    }

    return out;
}

std::vector<std::vector<float>> applySpeed(const std::vector<std::vector<float>>& channels, float speedMultiplier)
{
    if (channels.empty() || speedMultiplier <= 0.0f || speedMultiplier == 1.0f)
        return channels;

    std::vector<std::vector<float>> out;
    out.reserve(channels.size());

    const u32 fromRate = static_cast<u32>(std::lround(cSpeedRateBase * speedMultiplier));

    for (const auto& ch : channels)
        out.push_back(resample(ch, fromRate, cSpeedRateBase));

    return out;
}

static float curveShapeValue(float position, float tension, CurveShape shape)
{
    const float power = 1.0f + cCurveTensionPower * std::fabs(tension);
    const auto bend = [&](float value) { return tension >= 0.0f ? std::pow(value, power) : 1.0f - std::pow(1.0f - value, power); };
    const float count = static_cast<float>(std::min(cCurveCountMax, std::max(1, static_cast<int>(std::lround(cCurveCountBase * std::pow(cCurveCountBase, tension))))));

    switch (shape)
    {
        case CurveShape::DoubleCurve:
            return position < 0.5f ? 0.5f * bend(2.0f * position) : 1.0f - 0.5f * bend(2.0f * (1.0f - position));

        case CurveShape::Hold:
            return 0.0f;

        case CurveShape::Stairs:
            return std::floor(position * count) / count;

        case CurveShape::SmoothStairs:
        {
            const float step = std::floor(position * count);
            const float part = position * count - step;

            return (step + part * part * (3.0f - 2.0f * part)) / count;
        }

        case CurveShape::Pulse:
            return static_cast<int>(std::floor(position * 2.0f * count)) % 2 ? 1.0f : 0.0f;

        case CurveShape::Wave:
            return 0.5f - 0.5f * std::cos(2.0f * cCurvePi * count * position);

        case CurveShape::HalfSine:
            return 0.5f - 0.5f * std::cos(cCurvePi * position);

        default:
            return tension == 0.0f ? position : bend(position);
    }
}

float curveValue(const Curve& curve, float x)
{
    if (curve.empty())
        return 1.0f;

    if (x <= curve.front().x)
        return curve.front().y;

    for (size_t i = 0; i + 1 < curve.size(); i++)
    {
        const CurvePoint& from = curve[i];
        const CurvePoint& to = curve[i + 1];

        if (x < to.x)
            return from.y + (to.y - from.y) * curveShapeValue((x - from.x) / (to.x - from.x), to.tension, to.shape);
    }

    return curve.back().y;
}

bool isCurveFlat(const Curve& curve)
{
    for (const CurvePoint& point : curve)
        if (point.y != 1.0f)
            return false;

    return true;
}

void applyGainCurve(std::vector<float>& channel, const Curve& curve, u32 curveSamples)
{
    if (curveSamples == 0 || isCurveFlat(curve))
        return;

    const u32 length = static_cast<u32>(channel.size());

    for (u32 i = 0; i < length; i++)
        channel[i] *= curveValue(curve, static_cast<float>(i) / static_cast<float>(curveSamples));
}

}
