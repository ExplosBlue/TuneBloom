#include <bfsar/AudioProcessing.h>
#include <cmath>
#include <algorithm>

namespace AudioProcessing {

std::vector<float> resample(const std::vector<float>& data, u32 fromRate, u32 toRate)
{
    if (fromRate == toRate || data.empty())
        return data;

    const double ratio = static_cast<double>(toRate) / static_cast<double>(fromRate);
    const u32 n = static_cast<u32>(std::lround(data.size() * ratio));
    std::vector<float> out(n);

    for (u32 i = 0; i < n; i++)
    {
        const double t = i / ratio;
        const u32 i0 = static_cast<u32>(std::floor(t));
        const double frac = t - i0;
        const float a = (i0 < data.size()) ? data[i0] : 0.0f;
        const float b = (i0 + 1 < data.size()) ? data[i0 + 1] : a;
        out[i] = static_cast<float>(a + (b - a) * frac);
    }
    
    return out;
}

NormalizeResult normalizeChannels(const std::vector<std::vector<float>>& channels, bool enabled, float targetDb)
{
    float peak = 0.0f;
    for (const auto& ch : channels)
        for (float v : ch)
            peak = std::max(peak, std::fabs(v));

    const float peakDb = peak > 0.0f ? 20.0f * std::log10(peak) : -INFINITY;

    if (!enabled || peak == 0.0f)
        return { channels, 1.0f, peakDb };

    const float target = std::pow(10.0f, targetDb / 20.0f);
    const float gain = target / peak;

    std::vector<std::vector<float>> out;
    out.reserve(channels.size());

    for (const auto& ch : channels)
    {
        std::vector<float> d(ch.size());
        for (size_t i = 0; i < ch.size(); i++) d[i] = ch[i] * gain;
        out.push_back(std::move(d));
    }

    return { out, gain, peakDb };
}

std::vector<std::vector<float>> selectChannels(const std::vector<std::vector<float>>& channels, ChannelMode mode)
{
    if (channels.empty())
        return channels;

    switch (mode)
    {
        case ChannelMode::Stereo:
            return channels;

        case ChannelMode::Left:
            return { channels[0] };

        case ChannelMode::Right:
            return { channels.size() > 1 ? channels[1] : channels[0] };

        case ChannelMode::MonoMix:
        {
            const size_t n = channels[0].size();
            std::vector<float> mix(n, 0.0f);

            for (const auto& ch : channels)
                for (size_t i = 0; i < n && i < ch.size(); i++)
                    mix[i] += ch[i];
            
            const float invCount = 1.0f / static_cast<float>(channels.size());
            for (float& v : mix) v *= invCount;
            return { mix };
        }
    }
    return channels;
}

std::vector<std::vector<float>> selectChannelByIndex(const std::vector<std::vector<float>>& channels, u32 index)
{
    if (channels.empty())
        return channels;
    
    if (index >= channels.size())
        index = static_cast<u32>(channels.size()) - 1;
    
    return { channels[index] };
}

SpeedResult applySpeed(const std::vector<std::vector<float>>& channels, float speedMultiplier)
{
    if (channels.empty() || speedMultiplier <= 0.0f)
        return { channels, channels.empty() ? 0u : static_cast<u32>(channels[0].size()) };

    if (speedMultiplier == 1.0f)
        return { channels, static_cast<u32>(channels[0].size()) };

    std::vector<std::vector<float>> out;
    out.reserve(channels.size());
    u32 newSampleCount = 0;

    for (const auto& ch : channels)
    {
        constexpr u32 kBase = 1000000;
        const u32 fromRate = static_cast<u32>(std::lround(kBase * speedMultiplier));
        std::vector<float> d = resample(ch, fromRate, kBase);
        newSampleCount = static_cast<u32>(d.size());
        out.push_back(std::move(d));
    }

    return { out, newSampleCount };
}

}
