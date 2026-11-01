#include <ui/wave/WaveImportPanelInternal.h>

#include <messages/Messages.h>
#include <ui/player/PlayerViews.h>
#include <ui/widgets/Properties.h>
#include <ui/wave/WaveFormatUI.h>
#include <ui/widgets/Widgets.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_internal.h>

#include <algorithm>

namespace waveimport
{

constexpr f32 cWaveformHeightEm = 8.0f;
constexpr f32 cToolGroupGapEm   = 0.6f;

bool WaveImportPanel::isPreviewActive() const
{
    return sSoundPlayer.isActive() && sSoundPlayer.getPlayingWaveFile() == mPreviewWave;
}

void WaveImportPanel::stopPreview()
{
    mPlayRequested = false;

    if (mPreviewWave && sSoundPlayer.getPlayingWaveFile() == mPreviewWave)
        sSoundPlayer.stopAllPlayers();
}

WaveFileScale WaveImportPanel::calcPreviewScale() const
{
    WaveFileScale scale;
    scale.volume = mSettings.normalizeGain() / mPreview.gain;
    scale.pitch = mSettings.shapedParams.passThrough ? 1.0f : mSettings.speedMultiplier / mPreview.speed;

    return scale;
}

bool WaveImportPanel::playPreview(u32 offset, bool muted)
{
    WaveFileScale scale = calcPreviewScale();

    if (muted)
        scale.volume = 0.0f;

    return sSoundPlayer.playWaveFile(*mPreviewWave, -1, nullptr, offset, false, scale);
}

f32 WaveImportPanel::getPlayheadSample() const
{
    const f64 position = static_cast<f64>(sSoundPlayer.getPlaySamplePosition(true)) + mPreview.cropStart;

    if (mPreview.totalSamples == 0)
        return static_cast<f32>(position);

    return static_cast<f32>(position * static_cast<f64>(mSettings.mono.size()) / mPreview.totalSamples);
}

void WaveImportPanel::seekPreview(f64 sample)
{
    const u32 playingSampleCount = sSoundPlayer.getSampleCount();
    const size_t totalSamples = mSettings.mono.size();

    if (playingSampleCount == 0 || totalSamples == 0)
        return;

    const f64 position = sample * mPreview.totalSamples / static_cast<f64>(totalSamples) - mPreview.cropStart;

    sSoundPlayer.seek(sead::Mathf::clamp2(0.0f, static_cast<f32>(position / playingSampleCount), 1.0f));
}

void WaveImportPanel::refreshPreview(FrameContext& ctx)
{
    const bool dragging = ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right);
    const bool active = isPreviewActive();

    if (ctx.changed || ctx.rebuiltThisFrame)
        mPreviewStale = true;

    if (mPeakStale && !dragging)
    {
        mSettings.derivedDirty = true;
        mPeakStale = false;
    }

    if (active)
        sSoundPlayer.setWaveFileScale(calcPreviewScale());

    const bool wanted = mPlayRequested || (active && mPreviewStale && !dragging);

    if (wanted && !mJob.isBusy() && !mSettings.derivedDirty)
        startPreviewJob(ctx);
}

void WaveImportPanel::drawWaveformBox(FrameContext& ctx)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 em = ImGui::GetFontSize();
    const f32 buttonSize = ImGui::GetFrameHeight();
    const f32 groupGap = ImFloor(em * cToolGroupGapEm);
    const f32 fitAllWidth = CalcTextButtonWidth(messages::import::cWaveFitAll);
    const f32 fitLoopWidth = CalcTextButtonWidth(messages::import::cWaveFitLoop);
    const f32 curveWidth = CalcTextButtonWidth(messages::common::cCurve);
    const f32 toolsWidth = buttonSize * 3.0f + style.ItemSpacing.x * 3.0f + groupGap * 2.0f + curveWidth + fitAllWidth + fitLoopWidth;

    if (!BeginGroupBoxWithTools(messages::wavefile::cWaveform, toolsWidth))
        return;

    BeginBoxTools(toolsWidth);

    drawPreviewTransport(ctx, buttonSize);
    drawWaveformTools(ctx, groupGap);

    EndBoxTools();

    drawWaveform(ctx);

    EndGroupBox();
}

void WaveImportPanel::drawWaveformTools(FrameContext& ctx, f32 groupGap)
{
    // Volume curve
    const bool curveOn = mSettings.curveEnabled && ctx.cropEditable;

    ImGui::SameLine(0.0f, groupGap);
    ImGui::BeginDisabled(!ctx.cropEditable);

    if (ToggleButton(messages::common::cCurve, curveOn, ImVec2(CalcTextButtonWidth(messages::common::cCurve), 0.0f)))
    {
        mSettings.curveEnabled = !mSettings.curveEnabled;

        if (mWaveformState.curve.empty())
            mWaveformState.curve = { AudioProcessing::CurvePoint{ 0.0f, 1.0f }, AudioProcessing::CurvePoint{ 1.0f, 1.0f } };

        mPreviewStale = true;
        mTouched = true;
    }

    ImGui::SetItemTooltip("%s", messages::import::cWaveCurveTip);
    ImGui::EndDisabled();

    ImGui::SameLine(0.0f, groupGap);
    if (TextButton(messages::import::cWaveFitAll))
        ImGui::LoopWaveformFitAll(mWaveformState, ctx.totalSamples);

    ImGui::BeginDisabled(!ctx.showLoop);

    ImGui::SameLine();
    if (TextButton(messages::import::cWaveFitLoop))
        ImGui::LoopWaveformZoomToLoop(mWaveformState, ctx.loopStart, ctx.loopEnd, ctx.totalSamples);

    ImGui::EndDisabled();
}

void WaveImportPanel::drawWaveform(FrameContext& ctx)
{
    sead::FixedSafeString<96> readout;

    if (ctx.showLoop)
    {
        const u32 len = (ctx.loopEnd > ctx.loopStart) ? (ctx.loopEnd - ctx.loopStart) : 0;

        sead::FixedSafeString<32> time;
        FormatSampleTime(&time, len, mSettings.workingSampleRate);

        readout.format(messages::import::cWaveLoopReadoutFormat, time.cstr());
    }

    f32 playheadSample = -1.0f;

    if (isPreviewActive())
        playheadSample = getPlayheadSample();

    mWaveformState.showCrop = ctx.cropEditable;
    mWaveformState.cropStart = ctx.cropStart;
    mWaveformState.cropEnd = ctx.cropEnd;

    mWaveformState.showCurve = ctx.cropEditable && mSettings.curveEnabled;
    mWaveformState.displayGain = mSettings.normalizeGain();

    f64 seekSample = -1.0;
    ctx.changed = ImGui::LoopWaveformEditor("import", mWaveformState, mSettings.mono, mSettings.workingSampleRate,
                                            ctx.loopStart, ctx.loopEnd, playheadSample,
                                            ctx.showLoop, ctx.editable,
                                            ctx.showLoop ? readout.cstr() : nullptr, ImVec2(0.0f, ImFloor(ImGui::GetFontSize() * cWaveformHeightEm)),
                                            true, &seekSample);

    ctx.cropStart = mWaveformState.cropStart;
    ctx.cropEnd = mWaveformState.cropEnd;

    if (seekSample >= 0.0 && mPreviewWave && sSoundPlayer.getPlayingWaveFile() == mPreviewWave)
        seekPreview(seekSample);
}

void WaveImportPanel::drawPreviewTransport(FrameContext& ctx, f32 buttonSize)
{
    const bool previewActive = isPreviewActive();
    const bool previewPaused = previewActive && sSoundPlayer.isPause();
    const bool previewPlaying = previewActive && !previewPaused;
    const ImVec2 size(buttonSize, buttonSize);

    if (PlayToggleButton(previewPlaying ? TransportGlyph::Pause : TransportGlyph::Play, "###Play", size, previewPlaying))
    {
        if (!previewActive)
        {
            const bool fresh = mPreviewBuilt && !mPreviewStale && mJobKind != JobKind::Preview && !mSettings.derivedDirty;

            if (fresh)
                playPreview(0);
            else
                mPlayRequested = true;
        }
        else
        {
            sSoundPlayer.pause(previewPlaying);
        }
    }

    ImGui::SetItemTooltip("%s", previewPlaying ? messages::common::cPause : previewPaused ? messages::import::cWavePreviewResume : messages::import::cWavePreviewPlay);

    ImGui::SameLine();
    if (TransportButton(TransportGlyph::Stop, "###Stop", size))
    {
        mPlayRequested = false;
        sSoundPlayer.stopAllPlayers();
    }

    ImGui::SetItemTooltip("%s", messages::common::cStop);

    ImGui::SameLine();
    ImGui::BeginDisabled(!ctx.loopActive);

    const bool loopOn = mLoopPlayback && ctx.loopActive;

    if (ToggleButton(ICON_LC_REPEAT "###LoopPlayback", loopOn, size))
    {
        mLoopPlayback = !mLoopPlayback;
        mPreviewStale = true;
    }

    ImGui::SetItemTooltip("%s", mLoopPlayback ? messages::common::cLoopOn : messages::common::cLoopOff);

    ImGui::EndDisabled();
}

}
