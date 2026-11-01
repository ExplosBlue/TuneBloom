#include <ui/stream/StreamTracks.h>

#include <messages/Messages.h>
#include <ui/wave/LoopWaveformInternal.h>
#include <ui/wave/WaveformBox.h>

#include <imgui/imgui_internal.h>

#include <algorithm>
#include <vector>

static constexpr u32 cTrackPeakKeepCount   = 64;
static constexpr s32 cTrackPeakForgetFrames = 600;
static constexpr f32 cTrackLanePadYEm      = 0.15f;

struct TrackPeaks
{
    const WaveFile* wave;
    u32 contentSerial;
    u32 sampleCount;
    u32 channelNum;
    WaveFile::Encoding encoding;
    s32 lastFrame;
    std::vector<f32> peakMin;
    std::vector<f32> peakMax;
};

static std::vector<TrackPeaks>* sTrackPeaks = nullptr;
static s32 sTrackPeakBuildFrame = -1;

static void FillTrackPeakKey(TrackPeaks* peaks, const WaveFile& wave)
{
    peaks->wave = &wave;
    peaks->contentSerial = wave.getContentSerial();
    peaks->sampleCount = wave.getSampleCount();
    peaks->channelNum = static_cast<u32>(wave.getChannels().size());
    peaks->encoding = wave.getEncoding();
}

static bool IsTrackPeakKeySame(const TrackPeaks& a, const TrackPeaks& b)
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

static void BuildTrackPeaks(TrackPeaks* peaks, const WaveFile& wave)
{
    peaks->peakMin.clear();
    peaks->peakMax.clear();

    std::vector<f32> mono;

    if (CanDecodeTrackPeaks(wave) && DecodeWaveViewMono(wave, &mono) && !mono.empty())
        ImGui::LoopWaveformBuildPeaks(mono, &peaks->peakMin, &peaks->peakMax);
}

static void ForgetOldTrackPeaks(s32 frame)
{
    std::vector<TrackPeaks>& list = *sTrackPeaks;

    list.erase(std::remove_if(list.begin(), list.end(), [frame](const TrackPeaks& peaks) { return frame - peaks.lastFrame > cTrackPeakForgetFrames; }), list.end());

    if (list.size() < cTrackPeakKeepCount)
        return;

    list.erase(std::min_element(list.begin(), list.end(), [](const TrackPeaks& a, const TrackPeaks& b) { return a.lastFrame < b.lastFrame; }));
}

static const TrackPeaks* FindTrackPeaks(const WaveFile& wave)
{
    if (!sTrackPeaks)
        sTrackPeaks = new std::vector<TrackPeaks>();

    const s32 frame = ImGui::GetFrameCount();

    TrackPeaks key = {};
    FillTrackPeakKey(&key, wave);

    auto found = std::find_if(sTrackPeaks->begin(), sTrackPeaks->end(), [&wave](const TrackPeaks& peaks) { return peaks.wave == &wave; });

    if (found != sTrackPeaks->end() && IsTrackPeakKeySame(*found, key))
    {
        found->lastFrame = frame;
        return &*found;
    }

    if (sTrackPeakBuildFrame == frame)
        return nullptr;

    sTrackPeakBuildFrame = frame;

    if (found != sTrackPeaks->end())
        FillTrackPeakKey(&*found, wave);
    else
    {
        ForgetOldTrackPeaks(frame);
        found = sTrackPeaks->insert(sTrackPeaks->end(), key);
    }

    found->lastFrame = frame;
    BuildTrackPeaks(&*found, wave);

    return &*found;
}

static f32 CalcTrackPlayRatio(const TrackContext& ctx, const WaveFile& wave)
{
    if (sSoundPlayer.getPlayingStream() != ctx.source.stream || wave.getSampleCount() == 0)
        return -1.0f;

    return static_cast<f32>(sSoundPlayer.getPlaySamplePosition(true)) / static_cast<f32>(wave.getSampleCount());
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

static ImVec2 CalcTrackLanePad()
{
    return ImVec2(ImGui::GetStyle().FrameRounding, ImFloor(ImGui::GetFontSize() * cTrackLanePadYEm));
}

static void DrawTrackLaneFill(ImDrawList* draw, const ImVec2& min, const ImVec2& max, bool selected)
{
    const f32 rounding = ImGui::GetStyle().FrameRounding;

    draw->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_FrameBg), rounding);

    if (selected)
        draw->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_CheckMark, uicolors::Pick(cTrackSelectedAlpha)), rounding);
}

static void DrawTrackLaneText(ImDrawList* draw, const ImVec2& min, const ImVec2& max, const char* text)
{
    const ImVec2 size = ImGui::CalcTextSize(text);
    const ImVec2 pos = ImFloor(min + (max - min - size) * 0.5f);

    draw->AddText(pos, ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
}

static void DrawTrackLaneWave(const TrackContext& ctx, s32 index, const WaveFile* wave, ImDrawList* draw, const ImVec2& min, const ImVec2& max)
{
    static const std::vector<f32> sNoPeaks;

    const TrackPeaks* peaks = FindTrackPeaks(*wave);
    const ImVec2 pad = CalcTrackLanePad();
    const bool hasLoop = wave->hasValidLoop();
    const ImGui::WaveOverviewLoop loop = hasLoop ? CalcTrackLoop(*wave) : ImGui::WaveOverviewLoop{};

    draw->PushClipRect(min, max, true);
    ImGui::DrawWaveOverview(draw, min + pad, max - pad, peaks ? peaks->peakMin : sNoPeaks, peaks ? peaks->peakMax : sNoPeaks, GetTrackLaneTone(ctx, index), hasLoop ? &loop : nullptr, CalcTrackPlayRatio(ctx, *wave));
    draw->PopClipRect();
}

void DrawTrackLane(const TrackContext& ctx, s32 index, f32 height)
{
    const WaveFile* wave = static_cast<const WaveFile*>(ctx.tracks[index]->getWaveFileRef().getItem());
    const bool selected = IsTrackLaneSelected(ctx, index);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max = min + ImVec2(ImGui::GetContentRegionAvail().x, height);

    DrawTrackLaneFill(draw, min, max, selected);
    ImGui::Dummy(max - min);

    if (!wave)
        DrawTrackLaneText(draw, min, max, messages::reference::cNoWaveFile);
    else if (!CanDecodeTrackPeaks(*wave))
        DrawTrackLaneText(draw, min, max, messages::stream::cTrackLaneNoPreview);
    else
        DrawTrackLaneWave(ctx, index, wave, draw, min, max);
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

void DrawTrackLaneRuler(const TrackContext& ctx)
{
    const WaveFile* wave = FindTrackRulerWave(ctx);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight());

    const f32 padX = CalcTrackLanePad().x;

    if (wave)
        ImGui::DrawTimeRuler(ImGui::GetWindowDrawList(), ImVec2(min.x + padX, min.y), ImVec2(min.x + size.x - padX, min.y + size.y), wave->getSampleCount(), wave->getSampleRate());

    ImGui::Dummy(size);
}
