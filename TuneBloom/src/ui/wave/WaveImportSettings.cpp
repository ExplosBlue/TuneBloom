#include <ui/wave/WaveImportSettings.h>

#include <ui/wave/LoopWaveformEditor.h>

#include <bfsar/audio/LoopAnalysis.h>

#include <algorithm>
#include <cmath>

namespace waveimport
{

constexpr double cPi = 3.14159265358979323846;

constexpr float cSilenceThreshold = 0.001f;
constexpr u32   cPeakBlockSize    = 256;

void WaveImportSettings::decode(const WaveFile::RiffWaveInfo& info)
{
    pcm = decodePcmForPreview(info);
    cachedPath.copy(info.path.cstr());
    hasDecoded = true;
    sourceHadLoop = info.isLoop;
    loopMode = LoopMode::DetectFromWav;
    targetSampleRate = pcm.isValid() ? pcm.sampleRate : 0;
    speedMultiplier = 1.0f;
    normalizeEnabled = false;
    normalizeTargetDb = 0.0f;
    channelMode = AudioProcessing::ChannelMode::Stereo;
    curveEnabled = false;
    workingLoopInit = false;
    shapedChannels.clear();
    mono.clear();
    markShapeDirty();
    cropFracStart = 0.0;
    cropFracEnd = 1.0;
    manualLoopFracStart = 0.0;
    manualLoopFracEnd = 1.0;

    const double sourceSampleCount = pcm.isValid() ? static_cast<double>(pcm.sampleCount) : 0.0;
    origLoopFracStart = sourceSampleCount > 0.0 ? static_cast<double>(info.loopStartFrame) / sourceSampleCount : 0.0;
    origLoopFracEnd = sourceSampleCount > 0.0 ? static_cast<double>(info.loopEndFrame) / sourceSampleCount : 1.0;
}

float WaveImportSettings::keptPeak() const
{
    if (!isCropped())
        return shapedPeak;

    const u32 end = std::min(workingCropEnd, shapedLength());
    const u32 start = std::min(workingCropStart, end);
    const u32 firstBlock = (start + cPeakBlockSize - 1) / cPeakBlockSize;
    const u32 lastBlock = std::max(firstBlock, end / cPeakBlockSize);
    const u32 headEnd = std::min(end, firstBlock * cPeakBlockSize);
    const u32 tailStart = std::max(headEnd, lastBlock * cPeakBlockSize);

    float peak = 0.0f;

    for (u32 block = firstBlock; block < lastBlock; block++)
        peak = std::max(peak, shapedBlockPeaks[block]);

    for (const auto& ch : shapedChannels)
    {
        for (u32 i = start; i < headEnd; i++)
            peak = std::max(peak, std::fabs(ch[i]));

        for (u32 i = tailStart; i < end; i++)
            peak = std::max(peak, std::fabs(ch[i]));
    }

    return peak;
}

SampleRange WaveImportSettings::audibleRange() const
{
    u32 start = shapedLength();
    u32 end = 0;

    for (const auto& ch : shapedChannels)
    {
        const u32 length = static_cast<u32>(ch.size());
        u32 first = 0;
        u32 last = length;

        while (first < length && std::fabs(ch[first]) <= cSilenceThreshold)
            first++;

        while (last > first && std::fabs(ch[last - 1]) <= cSilenceThreshold)
            last--;

        if (first == last)
            continue;

        start = std::min(start, first);
        end = std::max(end, last);
    }

    return { start, end };
}

SampleRange WaveImportSettings::detectFromWavLoop(u32 totalSamples) const
{
    const double fractionStart = std::min(1.0, std::max(0.0, origLoopFracStart));
    const double fractionEnd = std::min(1.0, std::max(0.0, origLoopFracEnd));

    u32 loopStart = static_cast<u32>(std::lround(fractionStart * totalSamples));
    u32 loopEnd = static_cast<u32>(std::lround(fractionEnd * totalSamples));

    if (loopEnd > totalSamples)
        loopEnd = totalSamples;

    if (totalSamples > 0 && loopStart >= loopEnd)
        loopStart = loopEnd > 0 ? loopEnd - 1 : 0;

    return { loopStart, loopEnd };
}

u32 CalcCrossfadeSamples(u32 sampleRate, u32 cropStart, u32 loopStart, u32 loopEnd, float crossfadeMs)
{
    s32 crossfade = static_cast<s32>(crossfadeMs / 1000.0f * sampleRate);
    const s32 maxByLength = (loopEnd > loopStart) ? static_cast<s32>(loopEnd - loopStart - 1) : -1;
    const s32 maxByLead = (loopStart > cropStart) ? static_cast<s32>(loopStart - cropStart) : 0;
    crossfade = std::min({ crossfade, maxByLead, maxByLength });
    return crossfade > 0 ? static_cast<u32>(crossfade) : 0u;
}

static void BakeCrossfade(std::vector<float>& channel, u32 start, const OutputParams& params, u32 crossfade)
{
    if (params.loopEnd > start + channel.size())
        return;

    for (u32 i = 0; i < crossfade; i++)
    {
        const u32 tailIndex = params.loopEnd - crossfade + i - start;
        const u32 leadIndex = params.loopStart - crossfade + i - start;
        const double fade = static_cast<double>(i + 1) / static_cast<double>(crossfade + 1);
        const double gainOut = params.equalPower ? std::cos(fade * cPi / 2.0) : (1.0 - fade);
        const double gainIn  = params.equalPower ? std::sin(fade * cPi / 2.0) : fade;
        channel[tailIndex] = static_cast<float>(channel[tailIndex] * gainOut + channel[leadIndex] * gainIn);
    }
}

std::vector<std::vector<float>> BuildOutputChannels(const std::vector<std::vector<float>>& shapedChannels, const OutputParams& params)
{
    const u32 curveSamples = params.cropEnd > params.cropStart ? params.cropEnd - params.cropStart : 0u;
    const u32 crossfade = params.isLoop ? CalcCrossfadeSamples(params.sampleRate, params.cropStart, params.loopStart, params.loopEnd, params.crossfadeMs) : 0u;

    std::vector<std::vector<float>> out;
    out.reserve(shapedChannels.size());

    for (const auto& shaped : shapedChannels)
    {
        const u32 end = std::min<u32>(params.outputEnd, static_cast<u32>(shaped.size()));
        const u32 start = std::min(params.cropStart, end);

        out.emplace_back(shaped.begin() + start, shaped.begin() + end);
        std::vector<float>& channel = out.back();

        if (crossfade > 0)
            BakeCrossfade(channel, start, params, crossfade);

        if (params.gain != 1.0f)
        {
            for (float& sample : channel)
                sample *= params.gain;
        }

        if (params.curveEnabled)
            AudioProcessing::applyGainCurve(channel, params.curve, curveSamples);
    }

    return out;
}

static void MeasureBlockPeaks(ShapedAudio* shaped)
{
    const u32 length = shaped->channels.empty() ? 0u : static_cast<u32>(shaped->channels[0].size());

    shaped->blockPeaks.assign(length / cPeakBlockSize, 0.0f);
    shaped->peak = 0.0f;

    for (const auto& ch : shaped->channels)
    {
        const u32 count = std::min(length, static_cast<u32>(ch.size()));
        const float* samples = ch.data();

        for (u32 i = 0; i < count; i++)
        {
            const float value = std::fabs(samples[i]);
            const u32 block = i / cPeakBlockSize;

            if (value > shaped->peak)
                shaped->peak = value;

            if (block < shaped->blockPeaks.size() && value > shaped->blockPeaks[block])
                shaped->blockPeaks[block] = value;
        }
    }
}

ShapedAudio BuildShapedAudio(const DecodedPcm& pcm, const ShapeParams& params)
{
    ShapedAudio shaped;
    shaped.params = params;
    shaped.sampleRate = pcm.sampleRate;

    if (params.passThrough)
    {
        shaped.channels = AudioProcessing::selectChannels(pcm.channels, AudioProcessing::ChannelMode::Stereo);
    }
    else
    {
        shaped.channels = AudioProcessing::selectChannels(pcm.channels, params.channelMode);

        if (params.speed != 1.0f)
            shaped.channels = AudioProcessing::applySpeed(shaped.channels, params.speed);

        if (params.targetSampleRate != 0 && params.targetSampleRate != pcm.sampleRate)
        {
            for (auto& ch : shaped.channels)
                ch = AudioProcessing::resample(ch, pcm.sampleRate, params.targetSampleRate);

            shaped.sampleRate = params.targetSampleRate;
        }
    }

    MeasureBlockPeaks(&shaped);

    AudioProcessing::mixToMono(shaped.channels, shaped.mono);
    ImGui::LoopWaveformBuildPeaks(shaped.mono, &shaped.monoPeakMin, &shaped.monoPeakMax);

    shaped.period = shaped.mono.empty() ? 0u : LoopAnalysis::estimatePeriod(shaped.mono, shaped.sampleRate);

    return shaped;
}

void WaveImportSettings::rebuildCropRange()
{
    const u32 shapedLen = shapedLength();

    workingCropStart = static_cast<u32>(std::lround(std::min(1.0, std::max(0.0, cropFracStart)) * shapedLen));
    workingCropEnd = static_cast<u32>(std::lround(std::min(1.0, std::max(0.0, cropFracEnd)) * shapedLen));

    if (workingCropEnd > shapedLen)
        workingCropEnd = shapedLen;

    if (shapedLen > 0 && workingCropEnd == 0)
        workingCropEnd = 1;

    if (workingCropStart >= workingCropEnd)
        workingCropStart = workingCropEnd > 0 ? workingCropEnd - 1 : 0;
}

void WaveImportSettings::rebuildLoopRange(u32 totalSamples)
{
    if (loopMode == LoopMode::Manual && workingLoopInit)
    {
        const double fracStart = std::min(1.0, std::max(0.0, manualLoopFracStart));
        const double fracEnd = std::min(1.0, std::max(0.0, manualLoopFracEnd));
        workingLoopStart = static_cast<u32>(std::lround(fracStart * totalSamples));
        workingLoopEnd = static_cast<u32>(std::lround(fracEnd * totalSamples));

        if (workingLoopEnd > totalSamples)
            workingLoopEnd = totalSamples;

        if (totalSamples > 0 && workingLoopStart >= workingLoopEnd)
            workingLoopStart = workingLoopEnd > 0 ? workingLoopEnd - 1 : 0;
    }
    else
    {
        const SampleRange detected = detectFromWavLoop(totalSamples);
        workingLoopStart = detected.start;
        workingLoopEnd = detected.end;
    }

    workingLoopInit = true;
}

void WaveImportSettings::rebuildRanges()
{
    rebuildCropRange();

    normalizePeak = shapedParams.passThrough ? 0.0f : keptPeak();

    rebuildLoopRange(static_cast<u32>(mono.size()));
    derivedDirty = false;
}

WaveImportSettings::RebuildResult WaveImportSettings::applyShaped(ShapedAudio& shaped)
{
    const u32 oldLength = static_cast<u32>(mono.size());

    shapedChannels.swap(shaped.channels);
    shapedBlockPeaks.swap(shaped.blockPeaks);
    mono.swap(shaped.mono);
    shapedPeak = shaped.peak;
    shapedParams = shaped.params;
    workingSampleRate = shaped.sampleRate;
    period = shaped.period;

    rebuildRanges();

    return { oldLength, static_cast<u32>(mono.size()) };
}

}
