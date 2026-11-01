#include <ui/stream/StreamTracks.h>

#include <messages/Messages.h>
#include <ui/wave/LoopWaveformInternal.h>
#include <ui/wave/WaveMonoJob.h>
#include <ui/widgets/LaneRow.h>

#include <imgui/imgui_internal.h>

#include <algorithm>
#include <vector>

static constexpr u32 cTrackPeakKeepCount   = 64;
static constexpr s32 cTrackPeakForgetFrames = 600;

struct TrackPeakKey
{
    const WaveFile* wave;
    u32 contentSerial;
    u32 sampleCount;
    u32 channelNum;
    WaveFile::Encoding encoding;
};

struct TrackPeaks
{
    TrackPeakKey key;
    s32 lastFrame;
    bool isReady;
    f64 appearTime;
    std::vector<f32> peakMin;
    std::vector<f32> peakMax;
};

static std::vector<TrackPeaks>* sTrackPeaks = nullptr;
static WaveMonoJob* sTrackPeakJob = nullptr;
static TrackPeakKey sTrackPeakJobKey = {};
static s32 sTrackPeakBuildFrame = -1;

static TrackPeakKey MakeTrackPeakKey(const WaveFile& wave)
{
    return { &wave, wave.getContentSerial(), wave.getSampleCount(), static_cast<u32>(wave.getChannels().size()), wave.getEncoding() };
}

static bool IsTrackPeakKeySame(const TrackPeakKey& a, const TrackPeakKey& b)
{
    return a.wave == b.wave && a.contentSerial == b.contentSerial && a.sampleCount == b.sampleCount && a.channelNum == b.channelNum && a.encoding == b.encoding;
}

static bool CanDecodeTrackPeaks(const WaveFile& wave)
{
    switch (wave.getEncoding())
    {
        case WaveFile::Encoding::Pcm8:
        case WaveFile::Encoding::Pcm16:
        case WaveFile::Encoding::DspAdpcm:
            return wave.getSampleCount() <= cWaveViewMaxSamples;

        default:
            return false;
    }
}

static void TakeTrackPeakJob()
{
    WaveMono mono;

    if (!sTrackPeakJob || !sTrackPeakJob->poll(&mono))
        return;

    auto found = std::find_if(sTrackPeaks->begin(), sTrackPeaks->end(), [](const TrackPeaks& peaks) { return IsTrackPeakKeySame(peaks.key, sTrackPeakJobKey); });

    if (found == sTrackPeaks->end())
        return;

    if (found->peakMin.empty() && !mono.peakMin.empty())
        found->appearTime = ImGui::GetTime();

    found->peakMin = std::move(mono.peakMin);
    found->peakMax = std::move(mono.peakMax);
    found->isReady = true;
}

static void StartTrackPeakJob(const TrackPeakKey& key, const WaveFile& wave)
{
    sTrackPeakJobKey = key;
    GetWaveMonoJob(&sTrackPeakJob).requestPeaks(wave);
}

static void ForgetOldTrackPeaks(s32 frame)
{
    std::vector<TrackPeaks>& list = *sTrackPeaks;

    list.erase(std::remove_if(list.begin(), list.end(), [frame](const TrackPeaks& peaks) { return frame - peaks.lastFrame > cTrackPeakForgetFrames; }), list.end());

    if (list.size() < cTrackPeakKeepCount)
        return;

    list.erase(std::min_element(list.begin(), list.end(), [](const TrackPeaks& a, const TrackPeaks& b) { return a.lastFrame < b.lastFrame; }));
}

static const TrackPeaks& FindTrackPeaks(const WaveFile& wave)
{
    if (!sTrackPeaks)
        sTrackPeaks = new std::vector<TrackPeaks>();

    const s32 frame = ImGui::GetFrameCount();
    const TrackPeakKey key = MakeTrackPeakKey(wave);

    TakeTrackPeakJob();

    auto found = std::find_if(sTrackPeaks->begin(), sTrackPeaks->end(), [&wave](const TrackPeaks& peaks) { return peaks.key.wave == &wave; });

    if (found == sTrackPeaks->end())
    {
        ForgetOldTrackPeaks(frame);
        found = sTrackPeaks->insert(sTrackPeaks->end(), TrackPeaks { key, frame, false, -1.0, {}, {} });
    }
    else if (!IsTrackPeakKeySame(found->key, key))
    {
        found->key = key;
        found->isReady = false;
    }

    found->lastFrame = frame;

    if (found->isReady || sTrackPeakBuildFrame == frame || (sTrackPeakJob && sTrackPeakJob->isLoading()))
        return *found;

    sTrackPeakBuildFrame = frame;
    StartTrackPeakJob(key, wave);
    TakeTrackPeakJob();

    return *found;
}

f32 CalcStreamPlayRatio(const StreamSource& source, u32 sampleCount)
{
    if (sSoundPlayer.getPlayingStream() != source.stream || sampleCount == 0)
        return -1.0f;

    return static_cast<f32>(sSoundPlayer.getPlaySamplePosition(true)) / static_cast<f32>(sampleCount);
}

static ImGui::WaveOverviewLoop CalcTrackLoop(const WaveFile& wave)
{
    const f32 sampleCount = static_cast<f32>(wave.getSampleCount());

    return { wave.getOriginalLoopStartFrame() / sampleCount, wave.getOriginalLoopEndFrame() / sampleCount };
}

static bool IsTrackLaneSelected(const TrackContext& ctx, s32 index)
{
    return ctx.tracks[index] == GetHighlightedStreamTrack();
}

static ImGui::WaveOverviewTone GetTrackLaneTone(const TrackContext& ctx, s32 index)
{
    if (!sSoundPlayer.isStreamTrackAudible(*ctx.source.stream, ctx.tracks[index], static_cast<u32>(index)))
        return ImGui::WaveOverviewTone::Dim;

    if (IsTrackLaneSelected(ctx, index))
        return ImGui::WaveOverviewTone::Lit;

    return ImGui::WaveOverviewTone::Normal;
}

static void DrawTrackLaneText(ImDrawList* draw, const ImVec2& min, const ImVec2& max, const char* text)
{
    const ImVec2 size = ImGui::CalcTextSize(text);
    const ImVec2 pos = ImFloor(min + (max - min - size) * 0.5f);

    draw->AddText(pos, ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
}

static void DrawTrackLaneWave(const TrackContext& ctx, s32 index, const WaveFile* wave, ImDrawList* draw, const ImVec2& min, const ImVec2& max)
{
    const TrackPeaks& peaks = FindTrackPeaks(*wave);
    const ImVec2 pad = CalcLanePad();
    const bool hasLoop = wave->hasValidLoop();
    const ImGui::WaveOverviewLoop loop = hasLoop ? CalcTrackLoop(*wave) : ImGui::WaveOverviewLoop{};

    draw->PushClipRect(min, max, true);
    ImGui::DrawWaveOverview(draw, min + pad, max - pad, peaks.peakMin, peaks.peakMax, GetTrackLaneTone(ctx, index), hasLoop ? &loop : nullptr, ImGui::LoopWaveformAppearAlpha(peaks.appearTime));
    draw->PopClipRect();
}

static const WaveFile* FindTrackRulerWave(const TrackContext& ctx)
{
    for (const Sound::StreamSoundInfo::Track* track : ctx.tracks)
    {
        if (track->getWaveFileRef().isAttached())
            return static_cast<const WaveFile*>(track->getWaveFileRef().getItem());
    }

    return nullptr;
}

static void DrawTrackLanePlayhead(const TrackContext& ctx, const WaveFile* wave, const ImVec2& min, const ImVec2& max)
{
    const WaveFile* timeWave = wave ? wave : FindTrackRulerWave(ctx);

    if (!timeWave)
        return;

    const ImVec2 pad = CalcLanePad();

    DrawLaneRowPlayhead(min.x + pad.x, max.x - pad.x, max.y - min.y, CalcStreamPlayRatio(ctx.source, timeWave->getSampleCount()));
}

void DrawTrackLane(const TrackContext& ctx, s32 index, f32 height)
{
    const WaveFile* wave = static_cast<const WaveFile*>(ctx.tracks[index]->getWaveFileRef().getItem());
    const bool selected = IsTrackLaneSelected(ctx, index);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max = min + ImVec2(ImGui::GetContentRegionAvail().x, height);

    DrawLaneFill(draw, min, max, selected);
    ImGui::Dummy(max - min);

    if (!wave)
        DrawTrackLaneText(draw, min, max, messages::reference::cNoWaveFile);
    else if (!CanDecodeTrackPeaks(*wave))
        DrawTrackLaneText(draw, min, max, messages::stream::cTrackLaneNoPreview);
    else
        DrawTrackLaneWave(ctx, index, wave, draw, min, max);

    DrawTrackLanePlayhead(ctx, wave, min, max);
}

void DrawTrackLaneRuler(const TrackContext& ctx)
{
    const WaveFile* wave = FindTrackRulerWave(ctx);

    DrawLaneRuler(wave ? wave->getSampleCount() : 0, wave ? wave->getSampleRate() : 0);
}
