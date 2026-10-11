#pragma once

#include <bfsar/audio/AudioProcessing.h>
#include <bfsar/files/WaveFile.h>

#include <vector>

constexpr u16 cRiffFormatPcm   = 1;
constexpr u16 cRiffFormatFloat = 3;

struct DecodedPcm
{
    std::vector<std::vector<float>> channels;
    u32 sampleRate = 0;
    u32 sampleCount = 0;

    bool isLoop = false;
    u32 loopStartFrame = 0;
    u32 loopEndFrame = 0;

    bool isValid() const
    {
        return sampleCount > 0 && !channels.empty();
    }

    std::vector<float> monoMix() const
    {
        std::vector<float> out;

        AudioProcessing::mixToMono(channels, out);
        out.resize(sampleCount, 0.0f);

        return out;
    }
};

DecodedPcm decodePcmForPreview(const WaveFile::RiffWaveInfo& info);
bool decodeRiffChannels(const WaveFile::RiffWaveInfo& info, std::vector<std::vector<float>>* out);
