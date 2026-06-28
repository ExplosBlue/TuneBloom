#pragma once

#include <vector>
#include <bfsar/WaveFile.h>

struct DecodedPcm
{
    std::vector<std::vector<float>> channels;
    u32 sampleRate = 0;
    u32 sampleCount = 0;

    bool isValid() const
    {
        return sampleCount > 0 && !channels.empty();
    }

    std::vector<float> monoMix() const
    {
        std::vector<float> out(sampleCount, 0.0f);
        if (channels.empty())
            return out;

        const float invN = 1.0f / static_cast<float>(channels.size());
        for (u32 i = 0; i < sampleCount; i++)
        {
            float s = 0.0f;
            for (const auto& ch : channels)
                s += ch[i];
            out[i] = s * invN;
        }
        return out;
    }
};

DecodedPcm decodePcmForPreview(const WaveFile::RiffWaveInfo& info);
