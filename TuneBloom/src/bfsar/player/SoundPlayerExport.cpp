#include <bfsar/player/SoundPlayer.h>

#include <bfsar/audio/WavWriter.h>

#include <snd/HardwareMgr.h>
#include <snd/SoundSystem.h>

#include <math/seadMathCalcCommon.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

using snd::internal::driver::HardwareMgr;
using snd::internal::driver::SoundThread;

static constexpr f64 cFiniteMarginSec   = 30.0;
static constexpr f32 cSilenceThreshold  = 0.001f * 32767.0f;
static constexpr u32 cSilenceHoldFrames = 100;
static constexpr u32 cMaxTailFrames     = 1000;

static f64 CalcSeqExportSeconds(const SeqTimeline& timeline, const SoundPlayer::SeqExportSettings& settings, f32 tempoRatio, f64* fadeSec)
{
    const f64 timeScale = tempoRatio > 0.0f ? 1.0 / tempoRatio : 1.0;

    switch (timeline.kind)
    {
        case SeqTimeline::Kind::Finite:
            *fadeSec = 0.0;
            return timeline.calcMsec(timeline.endTick) / 1000.0 * timeScale;

        case SeqTimeline::Kind::Loop:
        {
            const f64 introSec = timeline.calcMsec(timeline.loopStartTick) / 1000.0 * timeScale;
            const f64 loopSec = timeline.calcMsec(timeline.endTick) / 1000.0 * timeScale - introSec;

            *fadeSec = settings.fadeSec;
            return introSec + loopSec * settings.loopCount;
        }

        default:
            *fadeSec = std::min<f64>(settings.fadeSec, settings.noEndSecs);
            return settings.noEndSecs - *fadeSec;
    }
}

static f32 RenderSeqExportFrame()
{
    SoundThread::instance()->frameProcess(snd::UpdateType::AudioFrame);

    f32* dataBuffers[2] = { HardwareMgr::sLeftDataBuffer, HardwareMgr::sRightDataBuffer };

    HardwareMgr::resetFinalMixCallbackData();
    HardwareMgr::processFinalMixCallback(dataBuffers, HardwareMgr::cSamplePerFrame, HardwareMgr::cChannelCount);

    f32 peak = 0.0f;
    for (u32 s = 0; s < HardwareMgr::cSamplePerFrame; s++)
    {
        peak = std::max(peak, std::fabs(dataBuffers[0][s]));
        peak = std::max(peak, std::fabs(dataBuffers[1][s]));
    }

    return peak;
}

static void FadeOutSeqExport(std::vector<f32>& left, std::vector<f32>& right, u64 fadeStartSample, u64 fadeEndSample)
{
    if (fadeEndSample <= fadeStartSample)
        return;

    const u64 sampleNum = std::min<u64>(fadeEndSample, std::min(left.size(), right.size()));
    const f64 fadeLength = static_cast<f64>(fadeEndSample - fadeStartSample);

    for (u64 i = fadeStartSample; i < sampleNum; i++)
    {
        const f32 gain = static_cast<f32>(1.0 - (i - fadeStartSample) / fadeLength);
        left[i] *= gain;
        right[i] *= gain;
    }

    left.resize(sampleNum);
    right.resize(sampleNum);
}

static std::vector<s16> MakeSeqExportChannel(const std::vector<f32>& src, u32 dstLen)
{
    const u32 srcLen = static_cast<u32>(src.size());
    std::vector<s16> dst(dstLen);

    for (u32 i = 0; i < dstLen; i++)
    {
        f32 sample = src[sead::Mathu::min(i, srcLen - 1)];

        if (dstLen != srcLen)
        {
            const f64 pos = (static_cast<f64>(i) * srcLen) / dstLen;
            const u32 idx = static_cast<u32>(pos);
            const f32 frac = static_cast<f32>(pos - idx);
            const u32 i0 = sead::Mathu::min(idx, srcLen - 1);
            const u32 i1 = sead::Mathu::min(idx + 1, srcLen - 1);

            sample = src[i0] + (src[i1] - src[i0]) * frac;
        }

        dst[i] = static_cast<s16>(sead::Mathi::clamp2(-32768, static_cast<s32>(sample), 32767));
    }

    return dst;
}

bool SoundPlayer::beginSeqExport(const Sound* sound, const SeqExportSettings& settings)
{
    SEAD_ASSERT(!mSeqExport.isActive);

    mSeqExport = SeqExportState();
    mSeqExport.settings = settings;

    mSeqExport.muteTracks = mMuteTracks;
    mSeqExport.soloTracks = mSoloTracks;

    mMuteTracks = 0;
    mSoloTracks = settings.soloTrack >= 0 ? 1 << settings.soloTrack : 0;

    snd::SoundSystem::pauseAudio();

    mSeqCapture.prepare();

    if (!mSeqCaptureRegistered)
    {
        HardwareMgr::instance()->appendFinalMixCallback(&mSeqCapture);
        mSeqCaptureRegistered = true;
    }

    if (!playSound(sound))
    {
        mSeqCapture.finish();
        restoreSeqExportTracks_();
        snd::SoundSystem::resumeAudio();
        return false;
    }

    mSequencePlayer.setLoopStop(0, 0);

    const f64 frameRate = static_cast<f64>(HardwareMgr::cSampleRate) / HardwareMgr::cSamplePerFrame;
    const bool isFinite = mSeqTimeline.kind == SeqTimeline::Kind::Finite;

    mSeqExport.playSec = CalcSeqExportSeconds(mSeqTimeline, settings, mSeqTempoRatio, &mSeqExport.fadeSec);

    const f64 capSec = isFinite ? std::max(mSeqExport.playSec * 2.0, mSeqExport.playSec + cFiniteMarginSec) : mSeqExport.playSec + mSeqExport.fadeSec;

    mSeqExport.frameMax = static_cast<u32>(std::ceil(capSec * frameRate));
    mSeqExport.playFrameNum = static_cast<u32>(std::ceil((mSeqExport.playSec + mSeqExport.fadeSec) * frameRate));

    if (mSeqTimeline.kind == SeqTimeline::Kind::Loop)
    {
        const f64 timeScale = mSeqTempoRatio > 0.0f ? 1.0 / mSeqTempoRatio : 1.0;

        mSeqExport.loopStartSec = mSeqTimeline.calcMsec(mSeqTimeline.loopStartTick) / 1000.0 * timeScale;
        mSeqExport.loopEndSec = mSeqTimeline.calcMsec(mSeqTimeline.endTick) / 1000.0 * timeScale;
    }

    mSeqExport.isActive = true;

    return true;
}

bool SoundPlayer::stepSeqExport(f64 budgetSec)
{
    SEAD_ASSERT(mSeqExport.isActive);

    const std::chrono::steady_clock::time_point beginTime = std::chrono::steady_clock::now();

    do
    {
        if (!mSeqExport.isTail)
        {
            if (isActive() && mSeqExport.frameCount < mSeqExport.frameMax)
            {
                RenderSeqExportFrame();
                mSeqExport.frameCount++;
                continue;
            }

            if (isActive())
                mSequencePlayer.finishPlayer(false);

            mSeqExport.isTail = true;
        }

        if (mSeqExport.tailFrameCount >= cMaxTailFrames || mSeqExport.quietFrameCount >= cSilenceHoldFrames)
            return true;

        const f32 peak = RenderSeqExportFrame();

        mSeqExport.quietFrameCount = peak < cSilenceThreshold ? mSeqExport.quietFrameCount + 1 : 0;
        mSeqExport.tailFrameCount++;
    }
    while (std::chrono::duration<f64>(std::chrono::steady_clock::now() - beginTime).count() < budgetSec);

    return false;
}

f32 SoundPlayer::getSeqExportProgress() const
{
    if (!mSeqExport.isActive || mSeqExport.playFrameNum == 0)
        return 0.0f;

    return sead::Mathf::min(static_cast<f32>(mSeqExport.frameCount) / static_cast<f32>(mSeqExport.playFrameNum), 1.0f);
}

void SoundPlayer::restoreSeqExportTracks_()
{
    mMuteTracks = mSeqExport.muteTracks;
    mSoloTracks = mSeqExport.soloTracks;
}

void SoundPlayer::endSeqExport(Pcm16Wav* out)
{
    SEAD_ASSERT(mSeqExport.isActive);

    mSeqCapture.finish();

    if (!out)
    {
        stopAllPlayers();
        stopAllVoices();
    }

    restoreSeqExportTracks_();
    snd::SoundSystem::resumeAudio();

    mSeqExport.isActive = false;

    if (!out)
        return;

    const f64 captureRate = HardwareMgr::cSampleRate;

    std::vector<f32>& left = mSeqCapture.mSamplesLeft;
    std::vector<f32>& right = mSeqCapture.mSamplesRight;

    FadeOutSeqExport(left, right, static_cast<u64>(mSeqExport.playSec * captureRate), static_cast<u64>((mSeqExport.playSec + mSeqExport.fadeSec) * captureRate));

    out->sampleRate = mSeqExport.settings.sampleRate != 0 ? mSeqExport.settings.sampleRate : HardwareMgr::cSampleRate;
    out->channels.clear();

    const u32 srcLen = static_cast<u32>(left.size());
    if (srcLen == 0)
        return;

    const u32 dstLen = std::max<u32>(1, static_cast<u32>((static_cast<u64>(srcLen) * out->sampleRate) / HardwareMgr::cSampleRate));

    out->channels.push_back(MakeSeqExportChannel(left, dstLen));
    out->channels.push_back(MakeSeqExportChannel(right, dstLen));

    const u32 loopStart = static_cast<u32>(mSeqExport.loopStartSec * out->sampleRate);
    const u32 loopEnd = static_cast<u32>(mSeqExport.loopEndSec * out->sampleRate);

    out->loop.enabled = loopEnd > loopStart && loopEnd <= dstLen;
    out->loop.start = loopStart;
    out->loop.end = loopEnd;
}
