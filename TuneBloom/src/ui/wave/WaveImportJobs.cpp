#include <ui/wave/WaveImportPanelInternal.h>

#include <ui/player/PlayerViews.h>

#include <algorithm>

namespace waveimport
{

constexpr u32 cLoopGuardSamples = 256;

namespace
{

struct PreviewParams
{
    OutputParams output;
    bool hasLoop = false;
    bool loopPlayback = false;
};

}

static u32 BuildPreviewWave(WaveFile* wave, const std::vector<std::vector<float>>& shapedChannels, const PreviewParams& params)
{
    const OutputParams& output = params.output;

    DecodedPcm pcm;
    pcm.channels = BuildOutputChannels(shapedChannels, output);
    pcm.sampleRate = output.sampleRate;

    u32 loopStart = output.loopStart > output.cropStart ? output.loopStart - output.cropStart : 0;
    u32 loopEnd = output.loopEnd > output.cropStart ? output.loopEnd - output.cropStart : 0;

    if (params.loopPlayback && !pcm.channels.empty())
    {
        const u32 len = static_cast<u32>(pcm.channels[0].size());

        if (loopEnd >= len && loopEnd > loopStart)
        {
            const u32 guard = std::min<u32>(loopEnd - loopStart, cLoopGuardSamples);

            for (auto& ch : pcm.channels)
            {
                ch.reserve(len + guard);

                for (u32 i = 0; i < guard; i++)
                    ch.push_back(ch[loopStart + i]);
            }
        }
    }

    pcm.sampleCount = pcm.channels.empty() ? 0u : static_cast<u32>(pcm.channels[0].size());

    if (!params.hasLoop || loopEnd > pcm.sampleCount)
    {
        loopStart = params.hasLoop ? std::min(loopStart, pcm.sampleCount) : 0u;
        loopEnd = pcm.sampleCount;
    }

    wave->setupPreviewPcm16(pcm, params.loopPlayback, loopStart, loopEnd);

    return pcm.sampleCount;
}

OutputParams WaveImportPanel::makeOutputParams(bool isLoop, u32 loopStart, u32 loopEnd, u32 outputEnd, f32 gain) const
{
    OutputParams params;
    params.isLoop = isLoop;
    params.loopStart = loopStart;
    params.loopEnd = loopEnd;
    params.cropStart = mSettings.workingCropStart;
    params.cropEnd = mSettings.workingCropEnd;
    params.outputEnd = outputEnd;
    params.sampleRate = mSettings.workingSampleRate;
    params.equalPower = mWaveformState.equalPowerCurve;
    params.crossfadeMs = mWaveformState.crossfadeMs;
    params.gain = gain;
    params.curveEnabled = mSettings.curveEnabled;

    if (params.curveEnabled)
        params.curve = mWaveformState.curve;

    return params;
}

void WaveImportPanel::applyShapedAudio(ShapedAudio& shaped)
{
    const WaveImportSettings::RebuildResult rebuilt = mSettings.applyShaped(shaped);

    rescaleWaveformView(rebuilt.oldLength, rebuilt.newLength);

    mWaveformState.dataRevision++;
    ImGui::LoopWaveformSetPeaks(mWaveformState, mSettings.mono, shaped.monoPeakMin, shaped.monoPeakMax);
}

void WaveImportPanel::rebuildShapedNow(bool passThrough)
{
    ShapedAudio shaped = BuildShapedAudio(mSettings.pcm, mSettings.shapeParams(passThrough));

    mSettings.shapeDirty = false;
    applyShapedAudio(shaped);
}

void WaveImportPanel::startShapeJob(bool passThrough)
{
    const ShapeParams params = mSettings.shapeParams(passThrough);

    mSettings.shapeDirty = false;
    mJobKind = JobKind::Shape;

    mJob.start([this, params]()
    {
        mShapedResult = BuildShapedAudio(mSettings.pcm, params);
    });
}

void WaveImportPanel::startPreviewJob(const FrameContext& ctx)
{
    if (!mPreviewWave)
    {
        mPreviewWave = new WaveFile;
        mSpareWave = new WaveFile;
    }

    PreviewParams params;
    params.output = makeOutputParams(ctx.manualLoop, ctx.loopStart, ctx.loopEnd, mSettings.workingCropEnd, mSettings.fullScaleGain());
    params.hasLoop = mSettings.hasLoop();
    params.loopPlayback = params.hasLoop && mLoopPlayback;

    mSparePreview.cropStart = mSettings.workingCropStart;
    mSparePreview.totalSamples = static_cast<u32>(mSettings.mono.size());
    mSparePreview.gain = params.output.gain;
    mSparePreview.speed = mSettings.shapedSpeed();

    mPreviewStale = false;
    mJobKind = JobKind::Preview;

    WaveFile* wave = mSpareWave;

    mJob.start([this, wave, params]()
    {
        mSparePreview.sampleCount = BuildPreviewWave(wave, mSettings.shapedChannels, params);
    });
}

void WaveImportPanel::finishPreviewJob()
{
    const bool wasActive = isPreviewActive();
    const bool paused = wasActive && sSoundPlayer.isPause();
    const u32 oldTotal = mPreview.totalSamples;
    const u64 oldPosition = wasActive ? static_cast<u64>(std::max(sSoundPlayer.getPlaySamplePosition(true), 0)) + mPreview.cropStart : 0u;

    std::swap(mPreviewWave, mSpareWave);
    std::swap(mPreview, mSparePreview);
    mPreviewBuilt = true;

    if (mPlayRequested)
    {
        mPlayRequested = false;
        playPreview(0);
        return;
    }

    if (!wasActive)
        return;

    const u64 position = oldTotal > 0 ? oldPosition * mPreview.totalSamples / oldTotal : oldPosition;
    u32 offset = position > mPreview.cropStart ? static_cast<u32>(position - mPreview.cropStart) : 0u;

    if (offset >= mPreview.sampleCount)
        offset = 0;

    if (playPreview(offset, paused) && paused)
        sSoundPlayer.pause(true);
}

bool WaveImportPanel::pollJob()
{
    if (!mJob.finish())
        return false;

    const JobKind kind = mJobKind;
    mJobKind = JobKind::None;

    if (kind == JobKind::Preview)
    {
        finishPreviewJob();
        return false;
    }

    applyShapedAudio(mShapedResult);

    return true;
}

bool WaveImportPanel::updateDerived(bool passThrough)
{
    bool rebuilt = pollJob();

    if (mSettings.shapeDirty)
    {
        if (mSettings.shapedChannels.empty())
        {
            rebuildShapedNow(passThrough);
            return true;
        }

        if (!mJob.isBusy())
            startShapeJob(passThrough);

        return rebuilt;
    }

    if (mSettings.derivedDirty && mJobKind != JobKind::Shape)
    {
        mSettings.rebuildRanges();
        rebuilt = true;
    }

    return rebuilt;
}

void WaveImportPanel::cancelJob()
{
    if (!mJob.isBusy())
        return;

    mJob.wait();

    if (mJobKind == JobKind::Shape)
        mSettings.markShapeDirty();
    else
        mPreviewStale = true;

    mJobKind = JobKind::None;
}

}
