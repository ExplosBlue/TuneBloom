#include <ui/WaveImportPanel.h>

#include <ui/UI.h>

#include <bfsar/LoopWaveformEditor.h>
#include <bfsar/LoopAnalysis.h>
#include <bfsar/DecodedPcm.h>
#include <bfsar/AudioProcessing.h>
#include <bfsar/TempWavWriter.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <strings.h>
#include <string>
#include <vector>

namespace
{

constexpr double cPi = 3.14159265358979323846;

constexpr float cFieldLabelWidth    = 120.0f;
constexpr float cOutputControlWidth = 210.0f;
constexpr float cLoopControlWidth   = 160.0f;
constexpr float cZoomGroupGap       = 24.0f;

constexpr u32 cRatePresets[] = { 8000, 11025, 16000, 22050, 32000, 44100, 48000 };

bool IsNativeWaveFile(const sead::SafeString& path)
{
    const char* pathStr = path.cstr();
    const char* dot = strrchr(pathStr, '.');
    return dot && (strcasecmp(dot, ".bcwav") == 0 || strcasecmp(dot, ".bfwav") == 0);
}

void FormatRate(char* buffer, size_t size, u32 rate, bool isSource)
{
    if (isSource)
        snprintf(buffer, size, "%u Hz (source)", rate);
    else
        snprintf(buffer, size, "%u Hz", rate);
}

bool WheelAdjustIndex(int* index, int count)
{
    if (!ImGui::IsItemHovered() || ImGui::GetIO().MouseWheel == 0.0f)
        return false;

    int next = *index - static_cast<int>(ImGui::GetIO().MouseWheel);
    next = std::max(0, std::min(next, count - 1));

    ImGui::GetIO().MouseWheel = 0.0f;

    if (next == *index)
        return false;

    *index = next;
    return true;
}

enum class LoopMode : s32
{
    DetectFromWav = 0,
    Disabled = 1,
    Manual = 2,
};

struct LoopRange
{
    u32 start = 0;
    u32 end = 0;
};

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

    u32 startOffsetSamples = 0;
    u32 targetSampleRate = 0;
    float speedMultiplier = 1.0f;
    bool normalizeEnabled = false;
    float normalizeTargetDb = 0.0f;
    AudioProcessing::ChannelMode channelMode = AudioProcessing::ChannelMode::Stereo;
    s32 channelIndexFor3Plus = 0;

    bool derivedDirty = true;
    std::vector<std::vector<float>> workingChannels;
    std::vector<float> mono;
    u32 workingSampleRate = 0;
    u32 period = 0;

    bool matchesLoadedPath(const WaveFile::RiffWaveInfo& info) const
    {
        return hasDecoded && std::strcmp(cachedPath.cstr(), info.path.cstr()) == 0;
    }

    void decode(const WaveFile::RiffWaveInfo& info)
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
        channelIndexFor3Plus = 0;
        workingLoopInit = false;
        derivedDirty = true;
        startOffsetSamples = 0;
        manualLoopFracStart = 0.0;
        manualLoopFracEnd = 1.0;

        const double sourceSampleCount = pcm.isValid() ? static_cast<double>(pcm.sampleCount) : 0.0;
        origLoopFracStart = sourceSampleCount > 0.0 ? static_cast<double>(info.loopStartFrame) / sourceSampleCount : 0.0;
        origLoopFracEnd = sourceSampleCount > 0.0 ? static_cast<double>(info.loopEndFrame) / sourceSampleCount : 1.0;
    }

    bool isUnmodified() const
    {
        return targetSampleRate == pcm.sampleRate && speedMultiplier == 1.0f && !normalizeEnabled && channelMode == AudioProcessing::ChannelMode::Stereo && channelIndexFor3Plus == 0 && startOffsetSamples == 0;
    }

    void setStartOffset(u32 newOffset)
    {
        newOffset = std::min<u32>(newOffset, pcm.sampleCount > 0 ? pcm.sampleCount - 1 : 0);

        if (newOffset == startOffsetSamples)
            return;

        if (loopMode == LoopMode::Manual && workingLoopInit)
        {
            const u32 oldTrimmedLen = (pcm.sampleCount > startOffsetSamples) ? (pcm.sampleCount - startOffsetSamples) : 0;
            const u32 newTrimmedLen = (pcm.sampleCount > newOffset) ? (pcm.sampleCount - newOffset) : 0;

            if (oldTrimmedLen > 0 && newTrimmedLen > 0)
            {
                const double origPosStart = startOffsetSamples + manualLoopFracStart * oldTrimmedLen;
                const double origPosEnd = startOffsetSamples + manualLoopFracEnd * oldTrimmedLen;

                manualLoopFracStart = std::min(1.0, std::max(0.0, (origPosStart - newOffset) / newTrimmedLen));
                manualLoopFracEnd = std::min(1.0, std::max(0.0, (origPosEnd - newOffset) / newTrimmedLen));
            }
        }

        startOffsetSamples = newOffset;
        derivedDirty = true;
    }

    LoopRange detectFromWavLoop(u32 totalSamples) const
    {
        double fractionStart = 0.0;
        double fractionEnd = 1.0;
        const u32 trimmedLength = (pcm.sampleCount > startOffsetSamples) ? (pcm.sampleCount - startOffsetSamples) : 0;

        if (trimmedLength > 0)
        {
            const double positionStart = origLoopFracStart * pcm.sampleCount;
            const double positionEnd = origLoopFracEnd * pcm.sampleCount;
            fractionStart = (positionStart - startOffsetSamples) / static_cast<double>(trimmedLength);
            fractionEnd = (positionEnd - startOffsetSamples) / static_cast<double>(trimmedLength);
        }

        fractionStart = std::min(1.0, std::max(0.0, fractionStart));
        fractionEnd = std::min(1.0, std::max(0.0, fractionEnd));

        u32 loopStart = static_cast<u32>(std::lround(fractionStart * totalSamples));
        u32 loopEnd = static_cast<u32>(std::lround(fractionEnd * totalSamples));

        if (loopEnd > totalSamples)
            loopEnd = totalSamples;

        if (totalSamples > 0 && loopStart >= loopEnd)
            loopStart = loopEnd > 0 ? loopEnd - 1 : 0;

        return { loopStart, loopEnd };
    }

    u32 crossfadeSamples(u32 loopStart, u32 loopEnd, float crossfadeMs) const
    {
        s32 crossfade = static_cast<s32>(crossfadeMs / 1000.0f * workingSampleRate);
        const s32 maxByLength = (loopEnd > loopStart) ? static_cast<s32>(loopEnd - loopStart - 1) : -1;
        crossfade = std::min({ crossfade, static_cast<s32>(loopStart), maxByLength });
        return crossfade > 0 ? static_cast<u32>(crossfade) : 0u;
    }

    std::vector<std::vector<float>> bakedChannels(bool isLoop, u32 loopStart, u32 loopEnd, bool equalPower, float crossfadeMs) const
    {
        std::vector<std::vector<float>> out = workingChannels;
        const u32 crossfade = crossfadeSamples(loopStart, loopEnd, crossfadeMs);

        if (!isLoop || crossfade == 0)
            return out;

        for (auto& channel : out)
        {
            if (loopEnd > channel.size())
                continue;

            for (u32 i = 0; i < crossfade; i++)
            {
                const u32 tailIndex = loopEnd - crossfade + i;
                const u32 leadIndex = loopStart - crossfade + i;
                const double fade = static_cast<double>(i + 1) / static_cast<double>(crossfade + 1);
                const double gainOut = equalPower ? std::cos(fade * cPi / 2.0) : (1.0 - fade);
                const double gainIn  = equalPower ? std::sin(fade * cPi / 2.0) : fade;
                channel[tailIndex] = static_cast<float>(channel[tailIndex] * gainOut + channel[leadIndex] * gainIn);
            }
        }

        return out;
    }

    RebuildResult rebuildDerived(bool isNative)
    {
        const u32 oldLength = static_cast<u32>(mono.size());
        workingSampleRate = pcm.sampleRate;

        std::vector<std::vector<float>> trimmed = pcm.channels;

        if (startOffsetSamples > 0)
        {
            for (auto& ch : trimmed)
            {
                const u32 offset = std::min<u32>(startOffsetSamples, static_cast<u32>(ch.size()));
                ch.erase(ch.begin(), ch.begin() + offset);
            }
        }

        if (isNative || isUnmodified())
        {
            workingChannels = trimmed;
        }
        else
        {
            if (static_cast<u32>(trimmed.size()) > 2 && channelIndexFor3Plus > 0)
                workingChannels = AudioProcessing::selectChannelByIndex(trimmed, static_cast<u32>(channelIndexFor3Plus));
            else
                workingChannels = AudioProcessing::selectChannels(trimmed, channelMode);

            if (speedMultiplier != 1.0f)
                workingChannels = AudioProcessing::applySpeed(workingChannels, speedMultiplier).channels;

            if (targetSampleRate != 0 && targetSampleRate != pcm.sampleRate)
            {
                for (auto& ch : workingChannels)
                    ch = AudioProcessing::resample(ch, pcm.sampleRate, targetSampleRate);

                workingSampleRate = targetSampleRate;
            }

            if (normalizeEnabled)
                workingChannels = AudioProcessing::normalizeChannels(workingChannels, true, normalizeTargetDb).channels;
        }

        if (workingChannels.empty())
        {
            mono.clear();
        }
        else if (workingChannels.size() == 1)
        {
            mono = workingChannels[0];
        }
        else
        {
            mono.assign(workingChannels[0].size(), 0.0f);
            const float inverseChannelCount = 1.0f / static_cast<float>(workingChannels.size());

            for (size_t i = 0; i < mono.size(); i++)
            {
                float sum = 0.0f;

                for (const auto& ch : workingChannels)
                    sum += ch[i];

                mono[i] = sum * inverseChannelCount;
            }
        }

        period = mono.empty() ? 0u : LoopAnalysis::estimatePeriod(mono, workingSampleRate);

        const u32 newLength = static_cast<u32>(mono.size());

        if (loopMode == LoopMode::Manual && workingLoopInit)
        {
            const double fracStart = std::min(1.0, std::max(0.0, manualLoopFracStart));
            const double fracEnd = std::min(1.0, std::max(0.0, manualLoopFracEnd));
            workingLoopStart = static_cast<u32>(std::lround(fracStart * newLength));
            workingLoopEnd = static_cast<u32>(std::lround(fracEnd * newLength));

            if (workingLoopEnd > newLength)
                workingLoopEnd = newLength;

            if (newLength > 0 && workingLoopStart >= workingLoopEnd)
                workingLoopStart = workingLoopEnd > 0 ? workingLoopEnd - 1 : 0;
        }
        else
        {
            const LoopRange detected = detectFromWavLoop(newLength);
            workingLoopStart = detected.start;
            workingLoopEnd = detected.end;
        }

        workingLoopInit = true;
        derivedDirty = false;

        return { oldLength, newLength };
    }
};

class WaveImportPanel
{
public:
    void draw(WaveFile::Encoding* encoding, WaveFile::RiffWaveInfo* info);

    std::string finalizeForCommit(WaveFile::RiffWaveInfo* info);

    bool isModified() const
    {
        return touched;
    }

    void reset()
    {
        stopPreview();

        settings.hasDecoded = false;
        settings.cachedPath.clear();
        touched = false;
    }

    void tick()
    {
        if (!drawnThisFrame)
        {
            stopPreview();
            touched = false;
        }

        drawnThisFrame = false;
    }

    bool confirmCancel(bool cancelClicked)
    {
        if (cancelClicked)
        {
            if (!isModified())
                return true;

            ImGui::OpenPopup("###WaveImportDiscard");
        }

        bool confirmed = false;

        if (ImGui::BeginPopupModal(ICON_LC_TRASH_2 " Cancel import?###WaveImportDiscard", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Cancel importing this file?");
            ImGui::Separator();

            const ImVec2 buttonSize((ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2.0f, 0.0f);

            if (ImGui::Button("Yes", buttonSize))
            {
                confirmed = true;
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();
            if (ImGui::Button("No", buttonSize))
                ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
        }

        return confirmed;
    }

private:
    enum class PreviewMode
    {
        None,
        Full,
        Loop,
    };

    struct FrameContext
    {
        WaveFile::Encoding* encoding = nullptr;
        WaveFile::RiffWaveInfo* info = nullptr;

        u32 totalSamples = 0;

        bool manualLoop = false;
        bool loopActive = false;
        bool showLoop = false;
        bool editable = false;

        u32 loopStart = 0;
        u32 loopEnd = 0;

        bool changed = false;
        bool rebuiltThisFrame = false;
    };

    WaveImportSettings settings;
    ImGui::LoopWaveformState waveformState;
    WaveFile* previewWave = nullptr;

    bool touched = false;
    bool drawnThisFrame = false;
    bool previewBuilt = false;
    bool previewStale = false;
    bool loopPlayback = true;
    PreviewMode previewMode = PreviewMode::None;

    void ensureDecoded(const WaveFile::RiffWaveInfo& info)
    {
        if (settings.matchesLoadedPath(info))
            return;

        settings.decode(info);

        touched = false;
        previewBuilt = false;
        previewStale = false;
        loopPlayback = true;
        previewMode = PreviewMode::None;
        waveformState = ImGui::LoopWaveformState{};
    }

    void rescaleWaveformView(u32 oldLength, u32 newLength)
    {
        if (oldLength > 0 && newLength > 0 && oldLength != newLength && waveformState.viewInitialized)
        {
            const double ratio = static_cast<double>(newLength) / static_cast<double>(oldLength);
            u32 viewStart = static_cast<u32>(waveformState.viewStart * ratio);
            u32 viewEnd = static_cast<u32>(waveformState.viewEnd * ratio);

            if (viewEnd > newLength)
                viewEnd = newLength;

            if (viewStart >= viewEnd)
            {
                viewStart = 0;
                viewEnd = newLength;
            }

            waveformState.viewStart = viewStart;
            waveformState.viewEnd = viewEnd;
        }
    }

    void stopPreview()
    {
        if (previewWave && sSoundPlayer.getPlayingWaveFile() == previewWave)
            sSoundPlayer.stopAllPlayers(true);
    }

    void rebuildPreview(u32 loopStart, u32 loopEnd);
    void ensurePreviewFresh(u32 loopStart, u32 loopEnd);
    void restartPreview(u32 loopStart, u32 loopEnd);

    void field(const char* label, float controlWidth, const char* tip = nullptr);
    void outputField(const char* label, const char* tip = nullptr);
    void loopField(const char* label);

    void drawUndecodableSource(WaveFile::Encoding* encoding, WaveFile::RiffWaveInfo* info);
    void drawSourceHeader(WaveFile::RiffWaveInfo* info);
    FrameContext beginFrame(WaveFile::Encoding* encoding, WaveFile::RiffWaveInfo* info, bool isNative);
    void drawWaveform(FrameContext& ctx);
    void drawPreviewTransport(FrameContext& ctx);
    void drawZoomControls(FrameContext& ctx);
    void drawOutputFormat(FrameContext& ctx);
    void drawEncodingField(FrameContext& ctx);
    void drawSampleRateField();
    void drawChannelsField();
    void drawStartOffsetField();
    void drawSpeedField();
    void drawNormalizeField();
    void drawLoopSection(FrameContext& ctx);
    void commitLoop(FrameContext& ctx);
    void refreshPreview(FrameContext& ctx);
    void drawFooter();
};

void WaveImportPanel::rebuildPreview(u32 loopStart, u32 loopEnd)
{
    const bool manualNow = (settings.loopMode == LoopMode::Manual);
    const bool loopActiveNow = (manualNow || (settings.loopMode == LoopMode::DetectFromWav && settings.sourceHadLoop)) && loopPlayback;

    DecodedPcm previewPcm;
    previewPcm.channels = settings.bakedChannels(manualNow, loopStart, loopEnd, waveformState.equalPowerCurve, waveformState.crossfadeMs);
    previewPcm.sampleRate = settings.workingSampleRate;

    if (loopActiveNow && !previewPcm.channels.empty())
    {
        const u32 len = static_cast<u32>(previewPcm.channels[0].size());

        if (loopEnd >= len && loopEnd > loopStart)
        {
            const u32 guard = std::min<u32>(loopEnd - loopStart, 256u);

            for (auto& ch : previewPcm.channels)
            {
                ch.reserve(len + guard);

                for (u32 i = 0; i < guard; i++)
                    ch.push_back(ch[loopStart + i]);
            }
        }
    }

    previewPcm.sampleCount = previewPcm.channels.empty() ? 0u : static_cast<u32>(previewPcm.channels[0].size());

    if (!previewWave)
        previewWave = new WaveFile;

    previewWave->setupPreviewPcm16(previewPcm, loopActiveNow, loopStart, loopEnd);
    previewBuilt = true;
    previewStale = false;
}

void WaveImportPanel::ensurePreviewFresh(u32 loopStart, u32 loopEnd)
{
    if (!previewBuilt || previewStale)
    {
        sSoundPlayer.stopAllPlayers(true);
        rebuildPreview(loopStart, loopEnd);
    }
}

void WaveImportPanel::restartPreview(u32 loopStart, u32 loopEnd)
{
    sSoundPlayer.stopAllPlayers(true);
    rebuildPreview(loopStart, loopEnd);

    const u32 offset = (previewMode == PreviewMode::Loop) ? loopStart : 0u;
    sSoundPlayer.playWaveFile(*previewWave, -1, nullptr, offset, false);
}

void WaveImportPanel::field(const char* label, float controlWidth, const char* tip)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);

    if (tip && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tip);

    ImGui::SameLine(cFieldLabelWidth);
    ImGui::SetNextItemWidth(controlWidth);
}

void WaveImportPanel::outputField(const char* label, const char* tip)
{
    field(label, cOutputControlWidth, tip);
}

void WaveImportPanel::loopField(const char* label)
{
    field(label, cLoopControlWidth);
}

void WaveImportPanel::drawUndecodableSource(WaveFile::Encoding* encoding, WaveFile::RiffWaveInfo* info)
{
    ComboScroll("Encoding", reinterpret_cast<s32*>(encoding), WaveFile::sEncodingTypes, IM_ARRAYSIZE(WaveFile::sEncodingTypes));
    ImGui::Separator();
    DrawWaveLoopInfo(info->isLoop, info->loopStartFrame, info->loopEndFrame,
                     info->sampleCount, info->sampleRate, true, nullptr, true);
}

void WaveImportPanel::drawSourceHeader(WaveFile::RiffWaveInfo* info)
{
    const char* path = info->path.cstr();
    const char* forwardSlash = strrchr(path, '/');
    const char* backSlash = strrchr(path, '\\');
    const char* base = (forwardSlash > backSlash ? forwardSlash : backSlash);
    base = base ? base + 1 : path;

    const u32 channelCount = static_cast<u32>(settings.pcm.channels.size());
    const double durationSec = settings.pcm.sampleRate ? static_cast<double>(settings.pcm.sampleCount) / settings.pcm.sampleRate : 0.0;

    ImGui::TextDisabled("%s  -  %u Hz  -  %s  -  %.2fs", base, settings.pcm.sampleRate, channelCount == 1 ? "mono" : channelCount == 2 ? "stereo" : "multi-ch", durationSec);
    ImGui::Separator();
}

WaveImportPanel::FrameContext WaveImportPanel::beginFrame(WaveFile::Encoding* encoding, WaveFile::RiffWaveInfo* info, bool isNative)
{
    FrameContext ctx;
    ctx.encoding = encoding;
    ctx.info = info;

    if (settings.derivedDirty)
    {
        const WaveImportSettings::RebuildResult rebuilt = settings.rebuildDerived(isNative);
        rescaleWaveformView(rebuilt.oldLength, rebuilt.newLength);
        ctx.rebuiltThisFrame = true;
    }

    ctx.totalSamples = static_cast<u32>(settings.mono.size());
    ctx.manualLoop = (settings.loopMode == LoopMode::Manual);
    ctx.loopActive = ctx.manualLoop || (settings.loopMode == LoopMode::DetectFromWav && settings.sourceHadLoop);
    ctx.showLoop = ctx.loopActive;
    ctx.editable = ctx.manualLoop;
    ctx.loopStart = settings.workingLoopStart;
    ctx.loopEnd = settings.workingLoopEnd;

    return ctx;
}

void WaveImportPanel::drawWaveform(FrameContext& ctx)
{
    char readout[96];
    readout[0] = '\0';

    if (ctx.showLoop)
    {
        const u32 len = (ctx.loopEnd > ctx.loopStart) ? (ctx.loopEnd - ctx.loopStart) : 0;
        const double loopMs = settings.workingSampleRate ? static_cast<double>(len) / settings.workingSampleRate * 1000.0 : 0.0;

        if (settings.period)
            snprintf(readout, sizeof(readout), "loop %.1f ms  -  %u smp  -  %.2f periods", loopMs, len, static_cast<double>(len) / settings.period);
        else
            snprintf(readout, sizeof(readout), "loop %.1f ms  -  %u smp", loopMs, len);
    }

    float playheadSample = -1.0f;

    if (sSoundPlayer.isActive() && sSoundPlayer.getPlayingWaveFile() == previewWave)
        playheadSample = static_cast<float>(sSoundPlayer.getPlaySamplePosition(true));

    double seekSample = -1.0;
    ctx.changed = ImGui::LoopWaveformEditor("import", waveformState, settings.mono, settings.workingSampleRate,
                                            ctx.loopStart, ctx.loopEnd, playheadSample,
                                            ctx.showLoop, ctx.editable,
                                            ctx.showLoop ? readout : nullptr, ImVec2(0.0f, 150.0f),
                                            true, &seekSample);

    if (seekSample >= 0.0)
    {
        const u32 playingSampleCount = sSoundPlayer.getSampleCount();

        if (playingSampleCount > 0)
        {
            const f32 fraction = sead::Mathf::clamp2(0.0f, static_cast<f32>(seekSample / static_cast<double>(playingSampleCount)), 1.0f);
            sSoundPlayer.seek(fraction);
        }
    }
}

void WaveImportPanel::drawPreviewTransport(FrameContext& ctx)
{
    const bool previewActive = sSoundPlayer.isActive() && sSoundPlayer.getPlayingWaveFile() == previewWave;
    const bool previewPaused = previewActive && sSoundPlayer.isPause();
    const bool previewPlaying = previewActive && !previewPaused;

    if (ImGui::Button(previewPlaying ? ICON_LC_PAUSE : ICON_LC_PLAY))
    {
        if (!previewActive)
        {
            ensurePreviewFresh(ctx.loopStart, ctx.loopEnd);
            sSoundPlayer.playWaveFile(*previewWave, -1, nullptr, 0, false);
            previewMode = PreviewMode::Full;
        }
        else
        {
            sSoundPlayer.pause(previewPlaying);
        }
    }

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(previewPlaying ? "Pause" : previewPaused ? "Resume" : "Play preview");

    ImGui::SameLine();
    if (ImGui::Button(ICON_LC_SQUARE))
    {
        sSoundPlayer.stopAllPlayers(true);
        previewMode = PreviewMode::None;
    }

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Stop");

    ImGui::SameLine();
    if (!ctx.loopActive)
        ImGui::BeginDisabled();

    const bool loopOn = loopPlayback && ctx.loopActive;

    if (loopOn)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, gAccentColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, gAccentColor);
    }

    if (ImGui::Button(ICON_LC_REPEAT))
    {
        loopPlayback = !loopPlayback;
        previewStale = true;
    }

    if (loopOn)
        ImGui::PopStyleColor(2);

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(loopPlayback ? "Loop: on" : "Loop: off");

    if (!ctx.loopActive)
        ImGui::EndDisabled();
}

void WaveImportPanel::drawZoomControls(FrameContext& ctx)
{
    const u32 viewStart = waveformState.viewStart;
    const u32 viewEnd = waveformState.viewEnd;
    const f32 zoom = viewEnd > viewStart ? static_cast<f32>(ctx.totalSamples) / static_cast<f32>(viewEnd - viewStart) : 1.0f;

    char zoomLabel[16];

    if (zoom < 1.05f)
        snprintf(zoomLabel, sizeof(zoomLabel), "fit");
    else
        snprintf(zoomLabel, sizeof(zoomLabel), zoom < 10.0f ? "%.1f\xc3\x97" : "%.0f\xc3\x97", zoom);

    ImGui::SameLine(0.0f, cZoomGroupGap);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("zoom %s", zoomLabel);

    ImGui::SameLine();
    if (ImGui::SmallButton("Fit all"))
        ImGui::LoopWaveformFitAll(waveformState, ctx.totalSamples);

    if (ctx.showLoop)
    {
        ImGui::SameLine();
        if (ImGui::SmallButton("Fit loop"))
            ImGui::LoopWaveformZoomToLoop(waveformState, ctx.loopStart, ctx.loopEnd, ctx.totalSamples);
    }
}

void WaveImportPanel::drawOutputFormat(FrameContext& ctx)
{
    ImGui::SeparatorText("Output format");

    drawEncodingField(ctx);
    drawSampleRateField();
    drawChannelsField();
    drawStartOffsetField();
    drawSpeedField();
    drawNormalizeField();
}

void WaveImportPanel::drawEncodingField(FrameContext& ctx)
{
    outputField("Encoding", "How the sample is stored in the bank/archive.");

    if (ComboScroll("##encoding", reinterpret_cast<s32*>(ctx.encoding), WaveFile::sEncodingTypes, IM_ARRAYSIZE(WaveFile::sEncodingTypes)))
        touched = true;
}

void WaveImportPanel::drawSampleRateField()
{
    u32 rates[1 + IM_ARRAYSIZE(cRatePresets)];
    int rateCount = 0;

    rates[rateCount++] = settings.pcm.sampleRate;

    for (u32 preset : cRatePresets)
        if (preset != settings.pcm.sampleRate)
            rates[rateCount++] = preset;

    int current = 0;

    for (int i = 0; i < rateCount; i++)
        if (rates[i] == settings.targetSampleRate)
            current = i;

    char preview[40];
    FormatRate(preview, sizeof(preview), settings.targetSampleRate, current == 0);

    outputField("Sample rate", "Audio is resampled to this rate on import.");

    if (ImGui::BeginCombo("##rate", preview))
    {
        for (int i = 0; i < rateCount; i++)
        {
            char label[40];
            FormatRate(label, sizeof(label), rates[i], i == 0);

            if (ImGui::Selectable(label, i == current))
            {
                settings.targetSampleRate = rates[i];
                settings.derivedDirty = true;
            }
        }

        ImGui::EndCombo();
    }

    if (WheelAdjustIndex(&current, rateCount))
    {
        settings.targetSampleRate = rates[current];
        settings.derivedDirty = true;
    }
}

void WaveImportPanel::drawChannelsField()
{
    const u32 sourceChannels = static_cast<u32>(settings.pcm.channels.size());
    const char* channelModeNames[] = { "Keep all channels", "Left only (mono)", "Right only (mono)", "Mix to mono" };
    s32 channelModeIndex = static_cast<s32>(settings.channelMode);

    outputField("Channels", "Which channel(s) of the source to import.");

    if (ComboScroll("##channels", &channelModeIndex, channelModeNames, IM_ARRAYSIZE(channelModeNames)))
    {
        settings.channelMode = static_cast<AudioProcessing::ChannelMode>(channelModeIndex);
        settings.derivedDirty = true;
    }

    if (sourceChannels > 2)
    {
        outputField("Or pick track", "Source has more than 2 channels; import one specific track as mono.");
        const ImU32 step = 1;

        if (ImGui::InputScalar("##picktrack", ImGuiDataType_S32, &settings.channelIndexFor3Plus, &step))
        {
            settings.channelIndexFor3Plus = std::max<s32>(0, std::min<s32>(settings.channelIndexFor3Plus, sourceChannels - 1));
            settings.derivedDirty = true;
        }

        ImGui::SameLine();
        ImGui::TextDisabled("of %u", sourceChannels);
    }
}

void WaveImportPanel::drawStartOffsetField()
{
    const DecodedPcm& pcm = settings.pcm;
    const double maxOffsetMs = pcm.sampleRate && pcm.sampleCount > 0 ? static_cast<double>(pcm.sampleCount - 1) / pcm.sampleRate * 1000.0 : 0.0;
    float offsetMs = pcm.sampleRate ? static_cast<float>(static_cast<double>(settings.startOffsetSamples) / pcm.sampleRate * 1000.0) : 0.0f;

    outputField("Start offset", "Trims leading samples from the source before any other processing.\nUseful for cutting off silence or a click at the start of a recording.");

    if (ImGui::SliderFloat("##startoffset", &offsetMs, 0.0f, static_cast<float>(maxOffsetMs), "%.1f ms"))
    {
        const u32 newOffsetSamples = pcm.sampleRate ? static_cast<u32>(std::lround(static_cast<double>(offsetMs) / 1000.0 * pcm.sampleRate)) : 0u;
        settings.setStartOffset(newOffsetSamples);
    }

    ImGui::SameLine();
    ImGui::TextDisabled("%u smp", settings.startOffsetSamples);
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset##offset"))
        settings.setStartOffset(0);
}

void WaveImportPanel::drawSpeedField()
{
    outputField("Speed", "Varispeed: changes pitch and length together. 1.00x = unchanged.");

    if (ImGui::SliderFloat("##speed", &settings.speedMultiplier, 0.25f, 4.0f, "%.2fx"))
        settings.derivedDirty = true;

    ImGui::SameLine();
    if (ImGui::SmallButton("Reset##speed"))
    {
        settings.speedMultiplier = 1.0f;
        settings.derivedDirty = true;
    }
}

void WaveImportPanel::drawNormalizeField()
{
    outputField("Normalize", "Scale the peak level to a target. Off = leave levels untouched.");

    if (ImGui::Checkbox("##norm", &settings.normalizeEnabled))
        settings.derivedDirty = true;

    ImGui::SameLine();
    if (settings.normalizeEnabled)
    {
        ImGui::SetNextItemWidth(cOutputControlWidth - 30.0f);

        if (ImGui::SliderFloat("##normdb", &settings.normalizeTargetDb, -24.0f, 0.0f, "%.1f dBFS"))
            settings.derivedDirty = true;
    }
    else
    {
        ImGui::TextDisabled("off");
    }
}

void WaveImportPanel::drawLoopSection(FrameContext& ctx)
{
    ImGui::SeparatorText("Loop");

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Loop mode");
    ImGui::SameLine(cFieldLabelWidth);
    ImGui::SetNextItemWidth(cOutputControlWidth);

    const char* loopModeNames[] = { "Detect from WAV", "Disabled", "Manual" };
    s32 loopModeIndex = static_cast<s32>(settings.loopMode);

    if (ComboScroll("##loopmode", &loopModeIndex, loopModeNames, IM_ARRAYSIZE(loopModeNames)))
    {
        settings.loopMode = static_cast<LoopMode>(loopModeIndex);
        ctx.changed = true;

        ImGui::SetWindowSize(ImVec2(ImGui::GetWindowWidth(), 0.0f));

        if (settings.loopMode == LoopMode::DetectFromWav)
        {
            const LoopRange detected = settings.detectFromWavLoop(ctx.totalSamples);
            ctx.loopStart = detected.start;
            ctx.loopEnd = detected.end;
        }
    }

    if (settings.loopMode != LoopMode::Manual)
        return;

    const ImU32 step = 1;
    s32 loopStartField = static_cast<s32>(ctx.loopStart);
    s32 loopEndField = static_cast<s32>(ctx.loopEnd);
    bool fieldsChanged = false;

    loopField("Loop start");

    if (ImGui::InputScalar("##loopstart", ImGuiDataType_S32, &loopStartField, &step))
        fieldsChanged = true;

    ImGui::SameLine();
    ImGui::TextDisabled("smp");

    loopField("Loop end");

    if (ImGui::InputScalar("##loopend", ImGuiDataType_S32, &loopEndField, &step))
        fieldsChanged = true;

    ImGui::SameLine();
    ImGui::TextDisabled("smp");

    if (fieldsChanged)
    {
        loopStartField = std::max<s32>(0, std::min<s32>(loopStartField, static_cast<s32>(ctx.totalSamples) - 1));
        loopEndField = std::max<s32>(loopStartField + 1, std::min<s32>(loopEndField, static_cast<s32>(ctx.totalSamples)));
        ctx.loopStart = static_cast<u32>(loopStartField);
        ctx.loopEnd = static_cast<u32>(loopEndField);
        ctx.changed = true;
    }

    if (ImGui::Button("Suggest loop"))
    {
        LoopAnalysis::SuggestResult suggestion = LoopAnalysis::suggestLoop(settings.mono, settings.workingSampleRate);

        if (suggestion.ok)
        {
            ctx.loopStart = suggestion.loopStart;
            ctx.loopEnd = suggestion.loopEnd;
            ImGui::LoopWaveformZoomToLoop(waveformState, suggestion.loopStart, suggestion.loopEnd, ctx.totalSamples);
            ctx.changed = true;
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Snap to frame + period"))
    {
        LoopAnalysis::FrameSnapResult snap = LoopAnalysis::snapFramePeriod(ctx.loopStart, ctx.loopEnd, settings.period, ctx.totalSamples);
        ctx.loopStart = snap.loopStart;
        ctx.loopEnd = snap.loopEnd;
        ctx.changed = true;
    }

    ImGui::SameLine();
    HelpMarker(
        "Drag on the waveform: left-click sets the loop start, right-click sets the end.\n"
        "Scroll to zoom; shift-scroll or middle-drag to pan.\n\n"
        "Suggest loop: auto-detect a clean loop region.\n"
        "Snap to frame + period: align the loop to the 14-sample DSP-ADPCM frame grid and a\n"
        "whole number of waveform periods, for the cleanest seam.");

    const u32 loopLength = (ctx.loopEnd > ctx.loopStart) ? (ctx.loopEnd - ctx.loopStart) : 0;
    const u32 crossfadeMaxSamples = std::min(ctx.loopStart, loopLength > 0 ? loopLength - 1 : 0);
    const float crossfadeMaxMs = std::max(1.0f, settings.workingSampleRate ? static_cast<float>(crossfadeMaxSamples) / settings.workingSampleRate * 1000.0f : 1.0f);

    loopField("Crossfade");

    if (ImGui::SliderFloat("##xfade", &waveformState.crossfadeMs, 0.0f, crossfadeMaxMs, "%.0f ms"))
        ctx.changed = true;

    ImGui::SameLine();
    if (ImGui::Checkbox("Equal-power", &waveformState.equalPowerCurve))
        ctx.changed = true;
}

void WaveImportPanel::commitLoop(FrameContext& ctx)
{
    settings.workingLoopStart = ctx.loopStart;
    settings.workingLoopEnd = ctx.loopEnd;

    if (ctx.manualLoop && ctx.totalSamples > 0)
    {
        settings.manualLoopFracStart = std::min(1.0, std::max(0.0, static_cast<double>(ctx.loopStart) / ctx.totalSamples));
        settings.manualLoopFracEnd = std::min(1.0, std::max(0.0, static_cast<double>(ctx.loopEnd) / ctx.totalSamples));
    }

    ctx.info->isLoop = ctx.loopActive;
    ctx.info->loopStartFrame = ctx.loopStart;
    ctx.info->loopEndFrame = ctx.loopEnd;
}

void WaveImportPanel::refreshPreview(FrameContext& ctx)
{
    const bool previewPlaying = sSoundPlayer.isActive() && sSoundPlayer.getPlayingWaveFile() == previewWave;
    const bool dragging = ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right);

    if (!previewBuilt)
    {
        rebuildPreview(ctx.loopStart, ctx.loopEnd);
    }
    else if (ctx.changed || ctx.rebuiltThisFrame)
    {
        if (!previewPlaying)
            rebuildPreview(ctx.loopStart, ctx.loopEnd);
        else if (dragging)
            previewStale = true;
        else
            restartPreview(ctx.loopStart, ctx.loopEnd);
    }
    else if (previewStale)
    {
        if (!previewPlaying)
            rebuildPreview(ctx.loopStart, ctx.loopEnd);
        else if (!dragging)
            restartPreview(ctx.loopStart, ctx.loopEnd);
    }
}

void WaveImportPanel::drawFooter()
{
    ImGui::Separator();
    HelpMarker(
        "To avoid multiple re-encodes which degrade audio quality,\nit is recommended to set your looping parameters upfront here.\n"
        "Alternatively, import as Pcm16 which lets you edit parameters without re-encodes,\nthen convert to DspAdpcm once at the end.");
}

void WaveImportPanel::draw(WaveFile::Encoding* encoding, WaveFile::RiffWaveInfo* info)
{
    drawnThisFrame = true;

    const bool isNative = IsNativeWaveFile(info->path);

    ensureDecoded(*info);

    if (!settings.pcm.isValid())
    {
        drawUndecodableSource(encoding, info);
        return;
    }

    drawSourceHeader(info);

    FrameContext ctx = beginFrame(encoding, info, isNative);

    drawWaveform(ctx);
    drawPreviewTransport(ctx);
    drawZoomControls(ctx);
    drawOutputFormat(ctx);
    drawLoopSection(ctx);

    if (ctx.changed || settings.derivedDirty)
        touched = true;

    commitLoop(ctx);
    refreshPreview(ctx);
    drawFooter();
}

std::string WaveImportPanel::finalizeForCommit(WaveFile::RiffWaveInfo* info)
{
    if (!info || !settings.hasDecoded)
        return {};

    if (IsNativeWaveFile(info->path))
        return {};

    if (std::strcmp(settings.cachedPath.cstr(), info->path.cstr()) != 0)
        return {};

    if (settings.workingChannels.empty())
        return {};

    const bool isLoop = info->isLoop;
    const u32 loopStart = settings.workingLoopStart;
    u32 loopEnd = settings.workingLoopEnd;
    const u32 workingLength = static_cast<u32>(settings.workingChannels[0].size());

    const bool manualLoop = (settings.loopMode == LoopMode::Manual);
    const bool bakeNeeded = isLoop && manualLoop && settings.crossfadeSamples(loopStart, loopEnd, waveformState.crossfadeMs) > 0;
    const bool truncateNeeded = isLoop && loopEnd < workingLength;

    if (settings.isUnmodified() && !bakeNeeded && !truncateNeeded)
        return {};

    char tempPathBuf[600];
    snprintf(tempPathBuf, sizeof(tempPathBuf), "%s.loopbloom_processed.wav", settings.cachedPath.cstr());

    std::vector<std::vector<float>> baked = settings.bakedChannels(manualLoop, loopStart, loopEnd, waveformState.equalPowerCurve, waveformState.crossfadeMs);

    if (truncateNeeded)
    {
        for (auto& ch : baked)
            if (ch.size() > loopEnd)
                ch.resize(loopEnd);
    }

    std::string written = TempWavWriter::write(baked, settings.workingSampleRate, isLoop, loopStart, loopEnd, tempPathBuf);

    if (written.empty())
        return {};

    info->path.copy(written.c_str());
    WaveFile::readRiffWavInfo(info);

    info->isLoop = isLoop;
    info->loopStartFrame = loopStart;
    info->loopEndFrame = loopEnd;

    return written;
}

WaveImportPanel sPanel;

}

void DrawWaveImportInfo(WaveFile::Encoding* encoding, WaveFile::RiffWaveInfo* info)
{
    sPanel.draw(encoding, info);
}

std::string FinalizeImportInfoForCommit(WaveFile::RiffWaveInfo* info)
{
    return sPanel.finalizeForCommit(info);
}

bool WaveImportModified()
{
    return sPanel.isModified();
}

void ResetWaveImport()
{
    sPanel.reset();
}

void WaveImportTick()
{
    sPanel.tick();
}

bool WaveImportConfirmCancel(bool cancelClicked)
{
    return sPanel.confirmCancel(cancelClicked);
}
