#include <bfsar/LoopWaveformEditor.h>

#include <imgui/imgui_internal.h>
#include <math/seadMathCalcCommon.h>
#include <ui/TimeUtil.h>
#include <ui/UI.h>

#include <algorithm>

namespace ImGui
{

using s32 = int32_t;
using s64 = int64_t;

constexpr double cPi = 3.14159265358979323846;

constexpr u32    cMinViewSpan      = 16;
constexpr double cViewAnimRate     = 40.0;
constexpr double cViewSnapEpsilon  = 0.75;
constexpr double cWheelZoomFactor  = 1.2;
constexpr double cButtonZoomFactor = 1.4;
constexpr double cWheelPanFraction = 0.05;
constexpr double cLoopZoomPadFrac  = 0.3;
constexpr u32    cLoopZoomMinPad   = 64;

constexpr float  cDefaultCanvasH   = 240.0f;
constexpr float  cSeekRulerH       = 30.0f;
constexpr float  cMinimapTrackH    = 12.0f;
constexpr float  cControlPad       = 4.0f;
constexpr float  cWaveAmpScale     = 0.92f;
constexpr float  cTickTargetPx     = 100.0f;
constexpr float  cSeekPlayedAlpha  = 0.18f;
constexpr int    cSeekLoopTintA    = 26;

constexpr double cTickStepsMs[] =
{
    0.1,   0.2,   0.5,
    1,     2,     5,
    10,    20,    50,
    100,   200,   500,
    1000,  2000,  5000,
    10000, 20000, 30000, 60000,
};

struct WaveformPalette
{
    ImU32 wave, waveLit, loop, loopGlow, introGlow, xfadeFill, good, edge, edgeSoft, inkFaint, bg, seamBg;
};

static const WaveformPalette& Palette()
{
    static constexpr WaveformPalette cDark =
    {
        .wave      = IM_COL32(0x3d, 0x5a, 0x73, 255),
        .waveLit   = IM_COL32(0x7f, 0xd1, 0xff, 255),
        .loop      = IM_COL32(0xff, 0xb3, 0x47, 255),
        .loopGlow  = IM_COL32(0xff, 0xb3, 0x47, 40),
        .introGlow = IM_COL32(0x4a, 0x9f, 0xd4, 26),
        .xfadeFill = IM_COL32(0x9b, 0x6d, 0xff, 36),
        .good      = IM_COL32(0x5f, 0xd3, 0x8a, 255),
        .edge      = IM_COL32(0x2a, 0x33, 0x40, 255),
        .edgeSoft  = IM_COL32(0x2a, 0x33, 0x40, 153),
        .inkFaint  = IM_COL32(0x5a, 0x66, 0x75, 255),
        .bg        = IM_COL32(0x16, 0x1b, 0x22, 255),
        .seamBg    = IM_COL32(0x0c, 0x10, 0x14, 255),
    };
    static constexpr WaveformPalette cLight =
    {
        .wave      = IM_COL32(0x8a, 0x9b, 0xac, 255),
        .waveLit   = IM_COL32(0x1f, 0x6f, 0xa8, 255),
        .loop      = IM_COL32(0xd9, 0x7a, 0x0a, 255),
        .loopGlow  = IM_COL32(0xd9, 0x7a, 0x0a, 55),
        .introGlow = IM_COL32(0x1f, 0x6f, 0xa8, 35),
        .xfadeFill = IM_COL32(0x7a, 0x4a, 0xc9, 45),
        .good      = IM_COL32(0x1f, 0x8f, 0x52, 255),
        .edge      = IM_COL32(0xd4, 0xd8, 0xde, 255),
        .edgeSoft  = IM_COL32(0xd4, 0xd8, 0xde, 140),
        .inkFaint  = IM_COL32(0x60, 0x68, 0x70, 255),
        .bg        = IM_COL32(0xf2, 0xf3, 0xf5, 255),
        .seamBg    = IM_COL32(0xe6, 0xe8, 0xec, 255),
    };
    return gThemeIsDark ? cDark : cLight;
}

static ImU32 WithAlpha(ImU32 color, int alpha)
{
    return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(alpha) << IM_COL32_A_SHIFT);
}

static u32 ViewSpan(const LoopWaveformState& state)
{
    return state.viewEnd > state.viewStart ? (state.viewEnd - state.viewStart) : 1u;
}

static float SampleToX(const LoopWaveformState& state, u32 sample, float canvasWidth)
{
    const float span = static_cast<float>(ViewSpan(state));
    return (static_cast<float>(sample) - static_cast<float>(state.viewStart)) / span * canvasWidth;
}

static double XToSample(const LoopWaveformState& state, float x, float canvasWidth)
{
    const double span = ViewSpan(state);
    return static_cast<double>(state.viewStart) + (static_cast<double>(x) / canvasWidth) * span;
}

static u32 ClampIndex(s64 index, u32 count)
{
    if (count == 0)
        return 0;

    if (index < 0)
        return 0;

    if (index >= static_cast<s64>(count))
        return count - 1;

    return static_cast<u32>(index);
}

static void ClampView(LoopWaveformState& state, u32 totalSamples)
{
    s64 viewStart = state.viewStart;
    s64 viewEnd = state.viewEnd;
    const s64 total = totalSamples;

    if (viewStart < 0)
    {
        viewEnd -= viewStart;
        viewStart = 0;
    }

    if (viewEnd > total)
    {
        viewStart -= (viewEnd - total);
        viewEnd = total;
    }

    if (viewStart < 0)
        viewStart = 0;

    if (viewEnd - viewStart < cMinViewSpan)
        viewEnd = std::min<s64>(total, viewStart + cMinViewSpan);

    state.viewStart = static_cast<u32>(std::max<s64>(0, viewStart));
    state.viewEnd = static_cast<u32>(std::max<s64>(state.viewStart, viewEnd));
}

static void ClampRange(double& viewStart, double& viewEnd, u32 totalSamples)
{
    const double total = totalSamples;

    if (viewStart < 0.0)
    {
        viewEnd -= viewStart;
        viewStart = 0.0;
    }

    if (viewEnd > total)
    {
        viewStart -= (viewEnd - total);
        viewEnd = total;
    }

    if (viewStart < 0.0)
        viewStart = 0.0;

    if (viewEnd - viewStart < cMinViewSpan)
        viewEnd = std::min<double>(total, viewStart + cMinViewSpan);

    if (viewEnd < viewStart)
        viewEnd = viewStart;
}

static void SnapView(LoopWaveformState& state)
{
    state.viewStartTarget = state.viewStartAnim = static_cast<double>(state.viewStart);
    state.viewEndTarget = state.viewEndAnim = static_cast<double>(state.viewEnd);
}

static void ResetView(LoopWaveformState& state, u32 totalSamples)
{
    state.viewStartTarget = 0.0;
    state.viewEndTarget = static_cast<double>(totalSamples);
    state.viewInitialized = true;
}

static void ZoomAt(LoopWaveformState& state, double sampleCenter, double factor, u32 totalSamples)
{
    const double currentStart = state.viewStartTarget;
    const double currentSpan = std::max(1.0, state.viewEndTarget - state.viewStartTarget);
    const double newSpan = std::max<double>(cMinViewSpan, std::min(static_cast<double>(totalSamples), currentSpan * factor));
    const double centerFraction = (sampleCenter - currentStart) / currentSpan;

    double newStart = sampleCenter - centerFraction * newSpan;
    double newEnd = newStart + newSpan;

    ClampRange(newStart, newEnd, totalSamples);

    state.viewStartTarget = newStart;
    state.viewEndTarget = newEnd;
}

static void ZoomToLoop(LoopWaveformState& state, u32 loopStart, u32 loopEnd, u32 totalSamples)
{
    const u32 loopLength = loopEnd > loopStart ? (loopEnd - loopStart) : 0;
    const u32 padding = std::max<u32>(cLoopZoomMinPad, static_cast<u32>(std::lround(loopLength * cLoopZoomPadFrac)));

    double viewStart = static_cast<double>(loopStart) - static_cast<double>(padding);
    double viewEnd = static_cast<double>(loopEnd) + static_cast<double>(padding);

    ClampRange(viewStart, viewEnd, totalSamples);

    state.viewStartTarget = viewStart;
    state.viewEndTarget = viewEnd;
}

static void InitViewIfNeeded(LoopWaveformState& state, u32 sampleCount)
{
    if (!state.viewInitialized && sampleCount > 0)
    {
        state.viewStart = 0;
        state.viewEnd = sampleCount;
        state.viewStartAnim = 0.0;
        state.viewEndAnim = static_cast<double>(sampleCount);
        state.viewStartTarget = 0.0;
        state.viewEndTarget = static_cast<double>(sampleCount);
        state.viewInitialized = true;
        state.viewAnimInit = true;
    }

    if (!state.viewAnimInit)
    {
        state.viewStartAnim = state.viewStartTarget = static_cast<double>(state.viewStart);
        state.viewEndAnim = state.viewEndTarget = static_cast<double>(state.viewEnd);
        state.viewAnimInit = true;
    }
}

static void AdvanceViewAnimation(LoopWaveformState& state, u32 sampleCount)
{
    const double deltaTime = GetIO().DeltaTime > 0.0f ? GetIO().DeltaTime : (1.0 / 60.0);
    const double smoothing = 1.0 - std::exp(-deltaTime * cViewAnimRate);

    state.viewStartAnim += (state.viewStartTarget - state.viewStartAnim) * smoothing;
    state.viewEndAnim += (state.viewEndTarget - state.viewEndAnim) * smoothing;

    if (std::fabs(state.viewStartTarget - state.viewStartAnim) < cViewSnapEpsilon && std::fabs(state.viewEndTarget - state.viewEndAnim) < cViewSnapEpsilon)
    {
        state.viewStartAnim = state.viewStartTarget;
        state.viewEndAnim = state.viewEndTarget;
    }

    s64 viewStart = std::llround(state.viewStartAnim);
    s64 viewEnd = std::llround(state.viewEndAnim);

    if (viewStart < 0)
        viewStart = 0;

    if (viewEnd > static_cast<s64>(sampleCount))
        viewEnd = sampleCount;

    if (viewEnd <= viewStart)
        viewEnd = std::min<s64>(sampleCount, viewStart + 1);

    if (viewStart >= viewEnd)
        viewStart = viewEnd > 0 ? viewEnd - 1 : 0;

    state.viewStart = static_cast<u32>(viewStart);
    state.viewEnd = static_cast<u32>(viewEnd);
}

static void DrawHandle(ImDrawList* draw, ImVec2 origin, float x, float height, bool isStart, bool focused)
{
    const float triangleSize = 6.0f;
    draw->AddLine(ImVec2(origin.x + x, origin.y), ImVec2(origin.x + x, origin.y + height), Palette().loop, focused ? 2.0f : 1.0f);

    if (isStart)
        draw->AddTriangleFilled(ImVec2(origin.x + x, origin.y), ImVec2(origin.x + x + triangleSize, origin.y),
                                ImVec2(origin.x + x, origin.y + triangleSize), Palette().loop);
    else
        draw->AddTriangleFilled(ImVec2(origin.x + x, origin.y), ImVec2(origin.x + x - triangleSize, origin.y),
                                ImVec2(origin.x + x, origin.y + triangleSize), Palette().loop);
}

static void DrawWaveform(ImDrawList* draw, ImVec2 origin, float width, float height,
                         LoopWaveformState& state, const std::vector<float>& mono, u32 sampleRate,
                         u32 loopStart, u32 loopEnd, float playheadSample, bool showLoop)
{
    const u32 sampleCount = static_cast<u32>(mono.size());
    const float midY = origin.y + height * 0.5f;

    draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), Palette().bg);

    const float loopStartX = SampleToX(state, loopStart, width);
    const float loopEndX = SampleToX(state, loopEnd, width);

    if (showLoop)
    {
        const float clampedStartX = sead::Mathf::clamp2(0.0f, loopStartX, width);
        const float clampedEndX = sead::Mathf::clamp2(0.0f, loopEndX, width);

        draw->AddRectFilled(origin, ImVec2(origin.x + clampedStartX, origin.y + height), Palette().introGlow);
        draw->AddRectFilled(ImVec2(origin.x + clampedStartX, origin.y), ImVec2(origin.x + clampedEndX, origin.y + height), Palette().loopGlow);

        const s32 loopLengthMinus1 = (loopEnd > loopStart) ? static_cast<s32>(loopEnd - loopStart - 1) : -1;
        s32 crossfadeSamples = static_cast<s32>(state.crossfadeMs / 1000.0f * sampleRate);
        crossfadeSamples = std::min({crossfadeSamples, static_cast<s32>(loopStart), loopLengthMinus1});

        if (crossfadeSamples > 0)
        {
            const float crossfadeWidth = static_cast<float>(crossfadeSamples) / ViewSpan(state) * width;

            draw->AddRectFilled(ImVec2(origin.x + std::max(0.0f, loopStartX), origin.y), ImVec2(origin.x + std::max(0.0f, loopStartX) + crossfadeWidth, origin.y + height), Palette().xfadeFill);
            draw->AddRectFilled(ImVec2(origin.x + std::max(0.0f, loopEndX - crossfadeWidth), origin.y), ImVec2(origin.x + std::max(0.0f, loopEndX), origin.y + height), Palette().xfadeFill);
        }
    }

    auto valueToY = [midY, height](float value)
    { return midY - value * (height * 0.5f) * cWaveAmpScale; };

    const float samplesPerPixel = static_cast<float>(ViewSpan(state)) / width;

    if (samplesPerPixel <= 1.0f)
    {
        const u32 firstSample = state.viewStart;
        const u32 lastSample = std::min(sampleCount > 0 ? sampleCount - 1 : 0, state.viewEnd);

        for (u32 sample = firstSample; sample < lastSample; sample++)
        {
            const bool inLoop = showLoop && (sample >= loopStart && sample < loopEnd);
            const ImU32 color = inLoop ? Palette().waveLit : Palette().wave;
            const float x0 = SampleToX(state, sample, width), x1 = SampleToX(state, sample + 1, width);

            draw->AddLine(ImVec2(origin.x + x0, valueToY(mono[sample])), ImVec2(origin.x + x1, valueToY(mono[sample + 1])), color, 1.4f);
        }
        if (samplesPerPixel < 0.3f)
        {
            for (u32 sample = firstSample; sample <= lastSample; sample++)
            {
                const bool inLoop = showLoop && (sample >= loopStart && sample < loopEnd);
                const ImU32 color = inLoop ? Palette().waveLit : Palette().wave;
                const float x = SampleToX(state, sample, width);

                draw->AddCircleFilled(ImVec2(origin.x + x, valueToY(mono[sample])), 1.7f, color);
            }
        }
    }
    else
    {
        float prevX = 0.0f, prevMax = 0.0f, prevMin = 0.0f;
        bool havePrev = false;

        for (int xi = 0; xi < static_cast<int>(width); xi++)
        {
            const float sampleAtX = static_cast<float>(state.viewStart) + xi * samplesPerPixel;
            const u32 columnStart = static_cast<u32>(std::max(0.0f, std::floor(sampleAtX)));
            const u32 columnEnd = std::max(columnStart + 1, static_cast<u32>(std::floor(static_cast<float>(state.viewStart) + (xi + 1) * samplesPerPixel)));
            float minValue = 1.0f, maxValue = -1.0f;

            for (u32 sample = columnStart; sample < columnEnd && sample < sampleCount; sample++)
            {
                const float value = mono[sample];

                if (value < minValue)
                    minValue = value;

                if (value > maxValue)
                    maxValue = value;
            }
            if (maxValue < minValue)
                continue;

            const bool inLoop = showLoop && (sampleAtX >= loopStart && sampleAtX <= loopEnd);
            const ImU32 color = inLoop ? Palette().waveLit : Palette().wave;
            const float x = xi + 0.5f;

            if (havePrev)
            {
                const float connectFromTop = std::fabs(prevMax - maxValue);
                const float connectFromBottom = std::fabs(prevMin - minValue);

                if (connectFromTop <= connectFromBottom)
                    draw->AddLine(ImVec2(origin.x + prevX, valueToY(prevMax)), ImVec2(origin.x + x, valueToY(maxValue)), color, 1.0f);
                else
                    draw->AddLine(ImVec2(origin.x + prevX, valueToY(prevMin)), ImVec2(origin.x + x, valueToY(minValue)), color, 1.0f);
            }

            draw->AddLine(ImVec2(origin.x + x, valueToY(maxValue)), ImVec2(origin.x + x, valueToY(minValue)), color, 1.0f);

            prevX = x;
            prevMax = maxValue;
            prevMin = minValue;
            havePrev = true;
        }
    }

    if (showLoop)
    {
        if (static_cast<float>(loopStart) + samplesPerPixel >= state.viewStart && static_cast<float>(loopStart) <= state.viewEnd + samplesPerPixel)
            DrawHandle(draw, origin, loopStartX, height, true, state.focus == LoopWaveformState::Focus::Start);

        if (static_cast<float>(loopEnd) + samplesPerPixel >= state.viewStart && static_cast<float>(loopEnd) <= state.viewEnd + samplesPerPixel)
            DrawHandle(draw, origin, loopEndX, height, false, state.focus == LoopWaveformState::Focus::End);
    }

    if (playheadSample >= 0.0f && playheadSample + samplesPerPixel >= state.viewStart && playheadSample <= state.viewEnd + samplesPerPixel)
    {
        const float playheadX = SampleToX(state, static_cast<u32>(playheadSample), width);

        draw->AddLine(ImVec2(origin.x + playheadX, origin.y), ImVec2(origin.x + playheadX, origin.y + height), Palette().good, 1.5f);
        draw->AddTriangleFilled(ImVec2(origin.x + playheadX - 4, origin.y + height), ImVec2(origin.x + playheadX + 4, origin.y + height), ImVec2(origin.x + playheadX, origin.y + height - 6), Palette().good);
    }
}

static bool HandleInteraction(LoopWaveformState& state, const std::vector<float>& mono,
                              float canvasWidth, ImVec2 canvasOrigin,
                              u32& loopStart, u32& loopEnd, bool editable)
{
    const u32 sampleCount = static_cast<u32>(mono.size());

    if (sampleCount == 0)
        return false;

    bool changed = false;
    ImGuiIO& io = GetIO();

    const bool hovered = IsItemHovered();
    const float mouseX = sead::Mathf::clamp2(0.0f, io.MousePos.x - canvasOrigin.x, canvasWidth);

    if (hovered && state.dragging == LoopWaveformState::Dragging::None)
    {
        if (editable && IsMouseClicked(ImGuiMouseButton_Left))
        {
            state.dragging = LoopWaveformState::Dragging::Start;
            state.focus = LoopWaveformState::Focus::Start;
        }
        else if (editable && IsMouseClicked(ImGuiMouseButton_Right))
        {
            state.dragging = LoopWaveformState::Dragging::End;
            state.focus = LoopWaveformState::Focus::End;
        }
        else if (IsMouseClicked(ImGuiMouseButton_Middle))
        {
            state.dragging = LoopWaveformState::Dragging::Pan;
            state.dragAnchorMouseX = io.MousePos.x;
            state.dragAnchorViewStart = state.viewStart;
            state.dragAnchorViewEnd = state.viewEnd;
        }
    }

    if (state.dragging == LoopWaveformState::Dragging::Pan)
    {
        if (IsMouseDown(ImGuiMouseButton_Middle))
        {
            const double deltaSamples = static_cast<double>(io.MousePos.x - state.dragAnchorMouseX) / canvasWidth *
                                        (static_cast<double>(state.dragAnchorViewEnd) - state.dragAnchorViewStart);

            const s64 newStart = static_cast<s64>(state.dragAnchorViewStart) - static_cast<s64>(std::lround(deltaSamples));
            const s64 newEnd = static_cast<s64>(state.dragAnchorViewEnd) - static_cast<s64>(std::lround(deltaSamples));

            state.viewStart = static_cast<u32>(std::max<s64>(0, newStart));
            state.viewEnd = static_cast<u32>(std::max<s64>(static_cast<s64>(state.viewStart), newEnd));

            ClampView(state, sampleCount);
            SnapView(state);
        }
        else
        {
            state.dragging = LoopWaveformState::Dragging::None;
        }
    }
    else if (state.dragging == LoopWaveformState::Dragging::Start || state.dragging == LoopWaveformState::Dragging::End)
    {
        const bool stillDown = (state.dragging == LoopWaveformState::Dragging::Start) ? IsMouseDown(ImGuiMouseButton_Left) : IsMouseDown(ImGuiMouseButton_Right);

        if (stillDown)
        {
            u32 sample = static_cast<u32>(std::max(0.0, std::round(XToSample(state, mouseX, canvasWidth))));
            sample = std::min(sample, sampleCount);

            if (state.dragging == LoopWaveformState::Dragging::Start)
            {
                const u32 newStart = std::min(sample, loopEnd > 0 ? loopEnd - 1 : 0);

                if (newStart != loopStart)
                {
                    loopStart = newStart;
                    changed = true;
                }
            }
            else
            {
                const u32 newEnd = std::max(sample, loopStart + 1);

                if (newEnd != loopEnd)
                {
                    loopEnd = newEnd;
                    changed = true;
                }
            }
        }
        else
        {
            state.dragging = LoopWaveformState::Dragging::None;
        }
    }

    if (hovered && io.MouseWheel != 0.0f)
    {
        if (io.KeyShift)
        {
            const double deltaSamples = -io.MouseWheel * cWheelPanFraction * ViewSpan(state);
            const s64 newStart = static_cast<s64>(state.viewStart) + static_cast<s64>(std::lround(deltaSamples));
            const s64 newEnd = static_cast<s64>(state.viewEnd) + static_cast<s64>(std::lround(deltaSamples));

            state.viewStart = static_cast<u32>(std::max<s64>(0, newStart));
            state.viewEnd = static_cast<u32>(std::max<s64>(static_cast<s64>(state.viewStart), newEnd));

            ClampView(state, sampleCount);
            SnapView(state);
        }
        else
        {
            const double factor = io.MouseWheel > 0 ? (1.0 / cWheelZoomFactor) : cWheelZoomFactor;
            ZoomAt(state, XToSample(state, mouseX, canvasWidth), factor, sampleCount);
        }
    }

    if (hovered && io.MouseWheelH != 0.0f)
    {
        const double deltaSamples = -io.MouseWheelH * cWheelPanFraction * ViewSpan(state);
        const s64 newStart = static_cast<s64>(state.viewStart) + static_cast<s64>(std::lround(deltaSamples));
        const s64 newEnd = static_cast<s64>(state.viewEnd) + static_cast<s64>(std::lround(deltaSamples));

        state.viewStart = static_cast<u32>(std::max<s64>(0, newStart));
        state.viewEnd = static_cast<u32>(std::max<s64>(static_cast<s64>(state.viewStart), newEnd));

        ClampView(state, sampleCount);
        SnapView(state);
    }

    return changed;
}

static void FormatTickLabel(char* buffer, size_t bufferSize, double timeMs, double majorStepMs)
{
    timeutil::FormatTimeLabel(buffer, bufferSize, timeMs / 1000.0, majorStepMs / 1000.0);
}

static bool DrawSeekRuler(const char* idStr, ImVec2 origin, float width, float height,
                          const LoopWaveformState& state, u32 sampleRate,
                          float playheadSample, bool showLoop, u32 loopStart, u32 loopEnd,
                          double* outSeekSample)
{
    const WaveformPalette& palette = Palette();
    ImDrawList* draw = GetWindowDrawList();

    const float left = origin.x;
    const float top = origin.y;
    const float bottom = origin.y + height;
    const float span = static_cast<float>(ViewSpan(state));
    const bool playing = playheadSample >= 0.0f;

    const float majorTickLen = 13.0f;
    const float minorTickLen = 6.0f;
    const float loopFootLen = 5.0f;

    draw->AddRectFilled(origin, ImVec2(left + width, bottom), palette.bg, 3.0f);
    draw->AddLine(origin, ImVec2(left + width, top), palette.edge);

    if (playing)
    {
        const float playheadX = sead::Mathf::clamp2(0.0f, SampleToX(state, static_cast<u32>(playheadSample), width), width);
        const ImU32 playedColor = ColorConvertFloat4ToU32(ImVec4(gAccentColor.x, gAccentColor.y, gAccentColor.z, cSeekPlayedAlpha));

        draw->AddRectFilled(ImVec2(left, top), ImVec2(left + playheadX, bottom), playedColor, 3.0f);
    }

    if (showLoop)
    {
        const float clampedLoopStartX = sead::Mathf::clamp2(0.0f, SampleToX(state, loopStart, width), width);
        const float clampedLoopEndX = sead::Mathf::clamp2(0.0f, SampleToX(state, loopEnd, width), width);

        if (clampedLoopEndX > clampedLoopStartX)
            draw->AddRectFilled(ImVec2(left + clampedLoopStartX, top), ImVec2(left + clampedLoopEndX, bottom), WithAlpha(palette.loop, cSeekLoopTintA));
    }

    if (sampleRate > 0)
    {
        const double startMs = timeutil::SamplesToSeconds(state.viewStart, sampleRate) * 1000.0;
        const double endMs = timeutil::SamplesToSeconds(state.viewEnd, sampleRate) * 1000.0;
        const double spanMs = endMs - startMs;

        if (spanMs > 0.0)
        {
            const double targetDivisions = std::max(4.0, static_cast<double>(width) / cTickTargetPx);
            const double roughStepMs = spanMs / targetDivisions;
            double majorStepMs = cTickStepsMs[IM_ARRAYSIZE(cTickStepsMs) - 1];

            for (double stepMs : cTickStepsMs)
            {
                if (stepMs >= roughStepMs)
                {
                    majorStepMs = stepMs;
                    break;
                }
            }

            const double majorStepPx = majorStepMs / spanMs * width;
            int subdivisions = 1;

            if (majorStepPx / 5.0 >= 7.0)
                subdivisions = 5;
            else if (majorStepPx / 4.0 >= 7.0)
                subdivisions = 4;
            else if (majorStepPx / 2.0 >= 6.0)
                subdivisions = 2;

            const double minorStepMs = majorStepMs / subdivisions;
            char label[32];

            for (s64 i = static_cast<s64>(std::ceil(startMs / minorStepMs)); i * minorStepMs <= endMs; i++)
            {
                const double timeMs = i * minorStepMs;
                const float tickX = SampleToX(state, static_cast<u32>(timeMs / 1000.0 * sampleRate + 0.5), width);

                if (tickX < 0.0f || tickX > width)
                    continue;

                if (i % subdivisions == 0)
                {
                    draw->AddLine(ImVec2(left + tickX, bottom - majorTickLen), ImVec2(left + tickX, bottom), palette.inkFaint);
                    FormatTickLabel(label, sizeof(label), timeMs, majorStepMs);
                    draw->AddText(ImVec2(left + tickX + 3, top + 2), palette.inkFaint, label);
                }
                else
                {
                    draw->AddLine(ImVec2(left + tickX, bottom - minorTickLen), ImVec2(left + tickX, bottom), palette.edgeSoft);
                }
            }
        }
    }

    if (showLoop)
    {
        const float loopStartX = SampleToX(state, loopStart, width);
        const float loopEndX = SampleToX(state, loopEnd, width);

        if (loopStartX >= 0.0f && loopStartX <= width)
        {
            draw->AddLine(ImVec2(left + loopStartX, top), ImVec2(left + loopStartX, bottom), palette.loop, 2.0f);
            draw->AddLine(ImVec2(left + loopStartX, top + 1), ImVec2(left + loopStartX + loopFootLen, top + 1), palette.loop, 2.0f);
            draw->AddLine(ImVec2(left + loopStartX, bottom - 1), ImVec2(left + loopStartX + loopFootLen, bottom - 1), palette.loop, 2.0f);
        }

        if (loopEndX >= 0.0f && loopEndX <= width)
        {
            draw->AddLine(ImVec2(left + loopEndX, top), ImVec2(left + loopEndX, bottom), palette.loop, 2.0f);
            draw->AddLine(ImVec2(left + loopEndX, top + 1), ImVec2(left + loopEndX - loopFootLen, top + 1), palette.loop, 2.0f);
            draw->AddLine(ImVec2(left + loopEndX, bottom - 1), ImVec2(left + loopEndX - loopFootLen, bottom - 1), palette.loop, 2.0f);
        }
    }

    if (playing)
    {
        const float playheadX = SampleToX(state, static_cast<u32>(playheadSample), width);

        if (playheadX >= 0.0f && playheadX <= width)
        {
            draw->AddLine(ImVec2(left + playheadX, top), ImVec2(left + playheadX, bottom), palette.good, 2.0f);
            draw->AddTriangleFilled(ImVec2(left + playheadX - 5, top), ImVec2(left + playheadX + 5, top), ImVec2(left + playheadX, top + 6), palette.good);
        }
    }

    InvisibleButton(idStr, ImVec2(width, height));

    if (IsItemHovered())
        SetMouseCursor(ImGuiMouseCursor_Hand);

    if (IsItemActivated())
    {
        const float fraction = sead::Mathf::clamp2(0.0f, (GetIO().MousePos.x - left) / width, 1.0f);

        if (outSeekSample)
            *outSeekSample = static_cast<double>(state.viewStart) + static_cast<double>(fraction) * span;

        return true;
    }
    return false;
}

static void DrawMinimap(LoopWaveformState& state, ImDrawList* draw, float width, u32 sampleCount)
{
    const float buttonWidth = GetFrameHeight();
    const float rowHeight = GetFrameHeight();

    if (Button("-##zoomout", ImVec2(buttonWidth, 0.0f)))
        ZoomAt(state, (state.viewStart + state.viewEnd) / 2.0, cButtonZoomFactor, sampleCount);

    if (IsItemHovered())
        SetTooltip("Zoom out");

    SameLine(0.0f, cControlPad);

    const float trackWidth = std::max(16.0f, width - 2.0f * (buttonWidth + cControlPad));

    const ImVec2 rowPos = GetCursorScreenPos();
    const ImVec2 trackOrigin(rowPos.x, rowPos.y + (rowHeight - cMinimapTrackH) * 0.5f);

    draw->AddRectFilled(trackOrigin, ImVec2(trackOrigin.x + trackWidth, trackOrigin.y + cMinimapTrackH), Palette().edge, 3.0f);

    const float thumbLeft = (static_cast<float>(state.viewStart) / sampleCount) * trackWidth;
    const float thumbWidth = std::max(8.0f, (static_cast<float>(ViewSpan(state)) / sampleCount) * trackWidth);
    const ImU32 thumbColor = (state.dragging == LoopWaveformState::Dragging::Scrollbar) ? Palette().waveLit : Palette().inkFaint;

    draw->AddRectFilled(ImVec2(trackOrigin.x + thumbLeft, trackOrigin.y + 1),
                        ImVec2(trackOrigin.x + std::min(trackWidth, thumbLeft + thumbWidth), trackOrigin.y + cMinimapTrackH - 1),
                        thumbColor, 3.0f);

    InvisibleButton("##scrollbar", ImVec2(trackWidth, rowHeight));

    if (IsItemHovered() || state.dragging == LoopWaveformState::Dragging::Scrollbar)
        SetMouseCursor(ImGuiMouseCursor_Hand);

    ImGuiIO& io = GetIO();

    if (IsItemHovered() && IsMouseClicked(ImGuiMouseButton_Left))
    {
        const double clickedSample = ((io.MousePos.x - trackOrigin.x) / trackWidth) * sampleCount;
        const double half = ViewSpan(state) / 2.0;

        state.viewStart = static_cast<u32>(std::max(0.0, clickedSample - half));
        state.viewEnd = state.viewStart + ViewSpan(state);

        ClampView(state, sampleCount);
        SnapView(state);

        state.dragging = LoopWaveformState::Dragging::Scrollbar;
        state.dragAnchorMouseX = io.MousePos.x;
        state.dragAnchorViewStart = state.viewStart;
        state.dragAnchorViewEnd = state.viewEnd;
    }
    else if (state.dragging == LoopWaveformState::Dragging::Scrollbar)
    {
        if (IsMouseDown(ImGuiMouseButton_Left))
        {
            const double deltaSamples = static_cast<double>(io.MousePos.x - state.dragAnchorMouseX) / trackWidth * sampleCount;
            const s64 newStart = static_cast<s64>(state.dragAnchorViewStart) + static_cast<s64>(std::lround(deltaSamples));
            const s64 span = static_cast<s64>(state.dragAnchorViewEnd) - static_cast<s64>(state.dragAnchorViewStart);

            state.viewStart = static_cast<u32>(std::max<s64>(0, newStart));
            state.viewEnd = state.viewStart + static_cast<u32>(span);

            ClampView(state, sampleCount);
            SnapView(state);
        }
        else
        {
            state.dragging = LoopWaveformState::Dragging::None;
        }
    }

    SameLine(0.0f, cControlPad);

    if (Button("+##zoomin", ImVec2(buttonWidth, 0.0f)))
        ZoomAt(state, (state.viewStart + state.viewEnd) / 2.0, 1.0 / cButtonZoomFactor, sampleCount);

    if (IsItemHovered())
        SetTooltip("Zoom in");
}

bool LoopWaveformEditor(const char* idStr, LoopWaveformState& state,
                        const std::vector<float>& mono, u32 sampleRate,
                        u32& loopStart, u32& loopEnd,
                        float playheadSample, bool showLoop, bool editable,
                        const char* readoutText, ImVec2 size,
                        bool showSeekBar, double* outSeekSample)
{
    const u32 sampleCount = static_cast<u32>(mono.size());
    InitViewIfNeeded(state, sampleCount);

    if (sampleCount > 0)
        AdvanceViewAnimation(state, sampleCount);

    if (size.x <= 0.0f)
        size.x = GetContentRegionAvail().x;

    size.x = std::max(1.0f, size.x);

    if (size.y <= 0.0f)
        size.y = cDefaultCanvasH;

    size.y = std::max(1.0f, size.y);

    PushID(idStr);

    const ImVec2 origin = GetCursorScreenPos();
    ImDrawList* draw = GetWindowDrawList();

    if (sampleCount > 0)
        DrawWaveform(draw, origin, size.x, size.y, state, mono, sampleRate, loopStart, loopEnd, playheadSample, showLoop);
    else
        draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), Palette().bg);

    if (sampleCount > 0 && readoutText && readoutText[0])
        draw->AddText(ImVec2(origin.x + 9.0f, origin.y + 6.0f), Palette().inkFaint, readoutText);

    InvisibleButton("##canvas", size);

    SetItemKeyOwner(ImGuiKey_MouseWheelY);
    SetItemKeyOwner(ImGuiKey_MouseWheelX);

    bool changed = false;

    if (sampleCount > 0)
        changed = HandleInteraction(state, mono, size.x, origin, loopStart, loopEnd, showLoop && editable);

    if (sampleCount > 0 && showSeekBar)
        DrawSeekRuler("##seek", GetCursorScreenPos(), size.x, cSeekRulerH, state, sampleRate,playheadSample, showLoop, loopStart, loopEnd, outSeekSample);

    if (sampleCount > 0)
        DrawMinimap(state, draw, size.x, sampleCount);

    PopID();
    return changed;
}

void LoopSeamPreview(const std::vector<float>& mono, u32 loopStart, u32 loopEnd, u32 period, u32 sampleRate, float crossfadeMs, bool equalPowerCurve, ImVec2 size)
{
    const u32 sampleCount = static_cast<u32>(mono.size());
    const ImVec2 origin = GetCursorScreenPos();
    ImDrawList* draw = GetWindowDrawList();

    draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), Palette().seamBg);
    Dummy(size);

    if (sampleCount == 0 || loopEnd <= loopStart)
        return;

    const s32 loopLengthMinus1 = static_cast<s32>(loopEnd - loopStart - 1);
    s32 crossfade = static_cast<s32>(crossfadeMs / 1000.0f * sampleRate);
    crossfade = std::min({crossfade, static_cast<s32>(loopStart), loopLengthMinus1});

    if (crossfade < 0)
        crossfade = 0;

    const u32 crossfadeSamples = static_cast<u32>(crossfade);
    const u32 view = std::max({crossfadeSamples * 2, period * 2, 256u});
    const u32 half = view / 2;
    const float midY = origin.y + size.y * 0.5f;

    draw->AddLine(ImVec2(origin.x, midY), ImVec2(origin.x + size.x, midY), Palette().edge);

    auto plot = [&](auto sampleAt, ImU32 color)
    {
        for (u32 i = 0; i + 1 < view; i++)
        {
            const float x0 = static_cast<float>(i) / view * size.x;
            const float x1 = static_cast<float>(i + 1) / view * size.x;
            const float y0 = midY - sampleAt(i) * (size.y * 0.5f) * 0.9f;
            const float y1 = midY - sampleAt(i + 1) * (size.y * 0.5f) * 0.9f;

            draw->AddLine(ImVec2(origin.x + x0, y0), ImVec2(origin.x + x1, y1), color);
        }
    };

    plot([&](u32 index)
    { return mono[ClampIndex(static_cast<s64>(loopEnd) - static_cast<s64>(half) + index, sampleCount)]; }, Palette().waveLit);
    plot([&](u32 index)
         { return mono[ClampIndex(static_cast<s64>(loopStart) - static_cast<s64>(half) + index, sampleCount)]; }, Palette().loop);

    for (u32 i = 0; i + 1 < view; i++)
    {
        auto valueAt = [&](u32 index) -> float
        {
            if (index < half)
            {
                const s64 tailIndex = static_cast<s64>(loopEnd) - static_cast<s64>(half) + index;

                if (tailIndex >= static_cast<s64>(loopEnd) - crossfadeSamples && crossfadeSamples > 0)
                {
                    const s64 offset = tailIndex - (static_cast<s64>(loopEnd) - crossfadeSamples);
                    const double progress = static_cast<double>(offset + 1) / (crossfadeSamples + 1);
                    const double gainOut = equalPowerCurve ? std::cos(progress * cPi / 2.0) : (1.0 - progress);
                    const double gainIn = equalPowerCurve ? std::sin(progress * cPi / 2.0) : progress;

                    return static_cast<float>(mono[ClampIndex(tailIndex, sampleCount)] * gainOut + mono[ClampIndex(static_cast<s64>(loopStart) - crossfadeSamples + offset, sampleCount)] * gainIn);
                }

                return mono[ClampIndex(tailIndex, sampleCount)];
            }

            return mono[ClampIndex(static_cast<s64>(loopStart) + (static_cast<s64>(index) - half), sampleCount)];
        };

        const float x0 = static_cast<float>(i) / view * size.x;
        const float x1 = static_cast<float>(i + 1) / view * size.x;
        const float y0 = midY - valueAt(i) * (size.y * 0.5f) * 0.9f;
        const float y1 = midY - valueAt(i + 1) * (size.y * 0.5f) * 0.9f;

        draw->AddLine(ImVec2(origin.x + x0, y0), ImVec2(origin.x + x1, y1), Palette().good, 1.6f);
    }

    draw->AddLine(ImVec2(origin.x + size.x * 0.5f, origin.y), ImVec2(origin.x + size.x * 0.5f, origin.y + size.y), IM_COL32(0x5a, 0x66, 0x75, 128));
}

void LoopWaveformZoomToLoop(LoopWaveformState& state, u32 loopStart, u32 loopEnd, u32 totalSamples)
{
    ZoomToLoop(state, loopStart, loopEnd, totalSamples);
}

void LoopWaveformFitAll(LoopWaveformState& state, u32 totalSamples)
{
    ResetView(state, totalSamples);
}

}
