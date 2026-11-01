#pragma once

#include <bfsar/audio/DecodedPcm.h>
#include <bfsar/files/WaveFile.h>
#include <snd/DecodeAdpcm.h>
#include <algorithm>
#include <functional>
#include <vector>

struct WaveChannelSource
{
    WaveFile::Encoding encoding = WaveFile::Encoding::Pcm16;
    const void* data = nullptr;
    u32 sampleCount = 0;
    snd::AdpcmParam param {};
};

inline bool GetWaveChannelSource(const WaveFile& wave, u32 channelIndex, WaveChannelSource* out)
{
    *out = WaveChannelSource();

    const auto& channels = wave.getChannels();
    const WaveFile::Channel* firstChannel = channels.nth(0);
    const bool useFullData = firstChannel && firstChannel->getFullData_() != nullptr;
    const WaveFile::Channel* channel = channels.nth(channelIndex);
    const void* data = useFullData ? channel->getFullData_() : nullptr;

    out->encoding = data ? channel->getFullDataEncoding_() : wave.getEncoding();
    if (!data)
        data = channel->getData();
    if (!data)
        return false;

    out->data = data;
    out->sampleCount = wave.getSampleCount();

    if (data == channel->getData())
    {
        u32 bytesPerSample = (out->encoding == WaveFile::Encoding::Pcm8) ? 1  : (out->encoding == WaveFile::Encoding::Pcm16) ? 2 : 0;
        if (bytesPerSample > 0)
        {
            const u32 availableSamples = channel->getDataSize() / bytesPerSample;
            out->sampleCount = std::min(out->sampleCount, availableSamples);
        }
        else if (out->encoding == WaveFile::Encoding::DspAdpcm)
        {
            const u32 availableFrames = channel->getDataSize() / BYTES_PER_FRAME;
            const u32 availableSamples = availableFrames * SAMPLES_PER_FRAME;
            out->sampleCount = std::min(out->sampleCount, availableSamples);
        }
    }

    const snd::DspAdpcmParam& dspParam = channel->getAdpcmParam(false);
    for (int row = 0; row < 8; row++)
        for (int col = 0; col < 2; col++)
            out->param.coef[row][col] = dspParam.coef[row][col];

    return true;
}

inline size_t GetWaveChannelSourceSize(const WaveChannelSource& source)
{
    switch (source.encoding)
    {
        case WaveFile::Encoding::Pcm8:
            return source.sampleCount;

        case WaveFile::Encoding::Pcm16:
            return static_cast<size_t>(source.sampleCount) * 2;

        case WaveFile::Encoding::DspAdpcm:
            return static_cast<size_t>((source.sampleCount + SAMPLES_PER_FRAME - 1) / SAMPLES_PER_FRAME) * BYTES_PER_FRAME;

        default:
            return 0;
    }
}

inline constexpr u32 cWaveSourceBlockSamples = SAMPLES_PER_FRAME * 65536;

inline void AddWaveChannelBlock(const WaveChannelSource& source, u32 first, u32 count, snd::AdpcmContext& context, std::vector<s16>& pcm, float* out, float gain)
{
    if (source.encoding == WaveFile::Encoding::Pcm16)
    {
        const s16* src = static_cast<const s16*>(source.data);
        for (u32 i = first; i < first + count; i++)
            out[i] += AudioProcessing::fromPcm16(src[i]) * gain;
    }
    else if (source.encoding == WaveFile::Encoding::Pcm8)
    {
        const s8* src = static_cast<const s8*>(source.data);
        for (u32 i = first; i < first + count; i++)
            out[i] += static_cast<float>(src[i]) / 128.0f * gain;
    }
    else if (source.encoding == WaveFile::Encoding::DspAdpcm)
    {
        snd::internal::DecodeDspAdpcm(first, context, source.param, source.data, count, pcm.data());

        for (u32 i = 0; i < count; i++)
            out[first + i] += AudioProcessing::fromPcm16(pcm[i]) * gain;
    }
}

inline bool AddWaveChannelSource(const WaveChannelSource& source, float* out, float gain, const std::function<bool()>& isStopped = {})
{
    const u32 blockSamples = std::min(source.sampleCount, cWaveSourceBlockSamples);

    snd::AdpcmContext context{};
    std::vector<s16> pcm(source.encoding == WaveFile::Encoding::DspAdpcm ? blockSamples : 0);

    for (u32 first = 0; first < source.sampleCount; first += blockSamples)
    {
        if (isStopped && isStopped())
            return false;

        AddWaveChannelBlock(source, first, std::min(blockSamples, source.sampleCount - first), context, pcm, out, gain);
    }

    return true;
}

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

    out.isLoop = wave.getIsLoop();
    out.loopStartFrame = wave.getOriginalLoopStartFrame();
    out.loopEndFrame = wave.getOriginalLoopEndFrame();

    if (out.loopEndFrame > sampleCount)
        out.loopEndFrame = sampleCount;

    if (out.loopStartFrame > out.loopEndFrame)
        out.loopStartFrame = out.loopEndFrame;

    for (u32 c = 0; c < channels.size(); c++)
    {
        WaveChannelSource source;

        if (GetWaveChannelSource(wave, c, &source))
            AddWaveChannelSource(source, out.channels[c].data(), 1.0f);
    }

    return out;
}
