#pragma once

#include <bfsar/DecodedPcm.h>
#include <bfsar/WaveFile.h>
#include <snd/DecodeAdpcm.h>
#include <algorithm>

inline DecodedPcm decodeWaveFileForEditing(const WaveFile& wave)
{
    DecodedPcm out;
    const auto& channels = wave.getChannels();
    if (channels.isEmpty() || wave.getSampleCount() == 0)
        return out;

    const u32 sampleCount = wave.getSampleCount();
    out.sampleRate = wave.getSampleRate();
    out.sampleCount = sampleCount;
    out.channels.assign(channels.size(), std::vector<float>(sampleCount, 0.0f));

    for (u32 c = 0; c < channels.size(); c++)
    {
        const WaveFile::Channel* channel = channels.nth(c);
        const void* data = channel->getFullData_();

        WaveFile::Encoding encoding = data ? channel->getFullDataEncoding_() : wave.getEncoding();
        if (!data)
            data = channel->getData();
        if (!data)
            continue;

        u32 safeSampleCount = sampleCount;
        if (data == channel->getData())
        {
            u32 bytesPerSample = (encoding == WaveFile::Encoding::Pcm8) ? 1  : (encoding == WaveFile::Encoding::Pcm16) ? 2 : 0;
            if (bytesPerSample > 0)
            {
                const u32 availableSamples = channel->getDataSize() / bytesPerSample;
                safeSampleCount = std::min(sampleCount, availableSamples);
            }
            else if (encoding == WaveFile::Encoding::DspAdpcm)
            {
                const u32 availableFrames = channel->getDataSize() / BYTES_PER_FRAME;
                const u32 availableSamples = availableFrames * SAMPLES_PER_FRAME;
                safeSampleCount = std::min(sampleCount, availableSamples);
            }
        }

        if (encoding == WaveFile::Encoding::Pcm16)
        {
            const s16* src = static_cast<const s16*>(data);
            for (u32 i = 0; i < safeSampleCount; i++)
                out.channels[c][i] = static_cast<float>(src[i]) / 32768.0f;
        }
        else if (encoding == WaveFile::Encoding::Pcm8)
        {
            const s8* src = static_cast<const s8*>(data);
            for (u32 i = 0; i < safeSampleCount; i++)
                out.channels[c][i] = static_cast<float>(src[i]) / 128.0f;
        }
        else if (encoding == WaveFile::Encoding::DspAdpcm)
        {
            snd::AdpcmContext context{};

            const snd::DspAdpcmParam& dspParam = channel->getAdpcmParam(false);
            snd::AdpcmParam param{};
            for (int row = 0; row < 8; row++)
                for (int col = 0; col < 2; col++)
                    param.coef[row][col] = dspParam.coef[row][col];

            std::vector<s16> pcm(safeSampleCount);
            snd::internal::DecodeDspAdpcm(
                0,
                context,
                param,
                data,
                safeSampleCount,
                pcm.data());

            for (u32 i = 0; i < safeSampleCount; i++)
                out.channels[c][i] = static_cast<float>(pcm[i]) / 32768.0f;
        }
    }

    return out;
}
