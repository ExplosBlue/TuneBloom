#pragma once

#include <bfsar/audio/AudioProcessing.h>
#include <bfsar/audio/DecodedPcm.h>
#include <bfsar/files/WaveFile.h>

#include <cmath>
#include <cstring>
#include <vector>

namespace waveimport
{

enum class LoopMode : s32
{
    DetectFromWav = 0,
    Disabled = 1,
    Manual = 2,
};

struct SampleRange
{
    u32 start = 0;
    u32 end = 0;
};

// Inputs of the shaped audio
struct ShapeParams
{
    AudioProcessing::ChannelMode channelMode = AudioProcessing::ChannelMode::Stereo;
    float speed = 1.0f;
    u32 targetSampleRate = 0;
    bool passThrough = false;
};

struct ShapedAudio
{
    std::vector<std::vector<float>> channels;
    std::vector<float> blockPeaks;
    std::vector<float> mono;
    std::vector<float> monoPeakMin;
    std::vector<float> monoPeakMax;
    float peak = 0.0f;
    u32 sampleRate = 0;
    u32 period = 0;
    ShapeParams params;
};

struct OutputParams
{
    bool isLoop = false;
    u32 loopStart = 0;
    u32 loopEnd = 0;
    u32 cropStart = 0;
    u32 cropEnd = 0;
    u32 outputEnd = 0;
    u32 sampleRate = 0;
    bool equalPower = true;
    float crossfadeMs = 0.0f;
    float gain = 1.0f;
    bool curveEnabled = false;
    AudioProcessing::Curve curve;
};

u32 CalcCrossfadeSamples(u32 sampleRate, u32 cropStart, u32 loopStart, u32 loopEnd, float crossfadeMs);
ShapedAudio BuildShapedAudio(const DecodedPcm& pcm, const ShapeParams& params);
std::vector<std::vector<float>> BuildOutputChannels(const std::vector<std::vector<float>>& shapedChannels, const OutputParams& params);

struct WaveImportSettings
{
    struct RebuildResult
    {
        u32 oldLength = 0;
        u32 newLength = 0;
    };

    DecodedPcm pcm;
    sead::FixedSafeString<512> cachedPath;
    bool hasDecoded = false;
    bool sourceHadLoop = false;

    LoopMode loopMode = LoopMode::DetectFromWav;
    u32 workingLoopStart = 0;
    u32 workingLoopEnd = 0;
    bool workingLoopInit = false;
    double origLoopFracStart = 0.0;
    double origLoopFracEnd = 1.0;
    double manualLoopFracStart = 0.0;
    double manualLoopFracEnd = 1.0;

    double cropFracStart = 0.0;
    double cropFracEnd = 1.0;
    u32 workingCropStart = 0;
    u32 workingCropEnd = 0;

    u32 targetSampleRate = 0;
    float speedMultiplier = 1.0f;
    bool normalizeEnabled = false;
    float normalizeTargetDb = 0.0f;
    AudioProcessing::ChannelMode channelMode = AudioProcessing::ChannelMode::Stereo;

    bool curveEnabled = false;

    bool derivedDirty = true;
    bool shapeDirty = true;
    std::vector<std::vector<float>> shapedChannels;
    std::vector<float> shapedBlockPeaks;
    float shapedPeak = 0.0f;
    ShapeParams shapedParams;
    float normalizePeak = 0.0f;
    std::vector<float> mono;
    u32 workingSampleRate = 0;
    u32 period = 0;

    float normalizeGain() const
    {
        return normalizeEnabled && normalizePeak > 0.0f ? std::pow(10.0f, normalizeTargetDb / 20.0f) / normalizePeak : 1.0f;
    }

    float fullScaleGain() const
    {
        return normalizePeak > 0.0f ? 1.0f / normalizePeak : 1.0f;
    }

    float shapedSpeed() const
    {
        return shapedParams.passThrough ? 1.0f : shapedParams.speed;
    }

    void markShapeDirty()
    {
        shapeDirty = true;
        derivedDirty = true;
    }

    ShapeParams shapeParams(bool passThrough) const
    {
        return { channelMode, speedMultiplier, targetSampleRate, passThrough };
    }

    bool matchesLoadedPath(const WaveFile::RiffWaveInfo& info) const
    {
        return hasDecoded && std::strcmp(cachedPath.cstr(), info.path.cstr()) == 0;
    }

    bool isUnmodified() const
    {
        return targetSampleRate == pcm.sampleRate && speedMultiplier == 1.0f && !normalizeEnabled && channelMode == AudioProcessing::ChannelMode::Stereo;
    }

    u32 shapedLength() const
    {
        return shapedChannels.empty() ? 0u : static_cast<u32>(shapedChannels[0].size());
    }

    bool isCropped() const
    {
        return workingCropStart > 0 || workingCropEnd < shapedLength();
    }

    bool hasLoop() const
    {
        return loopMode == LoopMode::Manual || (loopMode == LoopMode::DetectFromWav && sourceHadLoop);
    }

    void decode(const WaveFile::RiffWaveInfo& info);

    float keptPeak() const;
    SampleRange audibleRange() const;
    SampleRange detectFromWavLoop(u32 totalSamples) const;
    u32 crossfadeSamples(u32 loopStart, u32 loopEnd, float crossfadeMs) const
    {
        return CalcCrossfadeSamples(workingSampleRate, workingCropStart, loopStart, loopEnd, crossfadeMs);
    }

    void rebuildCropRange();
    void rebuildLoopRange(u32 totalSamples);
    void rebuildRanges();
    RebuildResult applyShaped(ShapedAudio& shaped);
};

}
