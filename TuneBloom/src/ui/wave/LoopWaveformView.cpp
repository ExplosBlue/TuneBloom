#include <ui/wave/LoopWaveformInternal.h>

#include <ui/app/Shortcuts.h>
#include <ui/widgets/ZoomScrollBar.h>

#include <algorithm>
#include <cmath>

namespace ImGui
{

constexpr double cViewAnimRate     = 40.0;
constexpr double cViewSnapEpsilon  = 0.75;
constexpr double cWheelZoomFactor  = 1.2;
constexpr double cWheelPanFraction = 0.05;
constexpr double cLoopZoomPadFrac  = 0.3;
constexpr u32    cLoopZoomMinPad   = 64;
constexpr float  cControlPad       = 4.0f;

u32 ViewSpan(const LoopWaveformState& state)
{
    return state.viewEnd > state.viewStart ? (state.viewEnd - state.viewStart) : 1u;
}

float SampleToX(const LoopWaveformState& state, u32 sample, float canvasWidth)
{
    const float span = static_cast<float>(ViewSpan(state));
    return (static_cast<float>(sample) - static_cast<float>(state.viewStart)) / span * canvasWidth;
}

double XToSample(const LoopWaveformState& state, float x, float canvasWidth)
{
    const double span = ViewSpan(state);
    return static_cast<double>(state.viewStart) + (static_cast<double>(x) / canvasWidth) * span;
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

void InitViewIfNeeded(LoopWaveformState& state, u32 sampleCount)
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

void AdvanceViewAnimation(LoopWaveformState& state, u32 sampleCount)
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

static void SetView(LoopWaveformState& state, s64 viewStart, s64 viewEnd, u32 sampleCount)
{
    state.viewStart = static_cast<u32>(std::max<s64>(0, viewStart));
    state.viewEnd = static_cast<u32>(std::max<s64>(static_cast<s64>(state.viewStart), viewEnd));

    ClampView(state, sampleCount);
    SnapView(state);
}

static void PanViewByWheel(LoopWaveformState& state, float wheel, u32 sampleCount)
{
    const double deltaSamples = -wheel * cWheelPanFraction * ViewSpan(state);
    const s64 shift = static_cast<s64>(std::lround(deltaSamples));

    SetView(state, static_cast<s64>(state.viewStart) + shift, static_cast<s64>(state.viewEnd) + shift, sampleCount);
}

void StartViewPan(LoopWaveformState& state)
{
    state.dragging = LoopWaveformState::Dragging::Pan;
    state.dragAnchorMouseX = GetIO().MousePos.x;
    state.dragAnchorViewStart = state.viewStart;
    state.dragAnchorViewEnd = state.viewEnd;
}

void DragViewPan(LoopWaveformState& state, const WaveformCanvas& canvas)
{
    if (!IsMouseDown(ImGuiMouseButton_Middle))
    {
        state.dragging = LoopWaveformState::Dragging::None;
        return;
    }

    const double deltaSamples = static_cast<double>(GetIO().MousePos.x - state.dragAnchorMouseX) / canvas.width *
                                (static_cast<double>(state.dragAnchorViewEnd) - state.dragAnchorViewStart);
    const s64 shift = static_cast<s64>(std::lround(deltaSamples));

    SetView(state, static_cast<s64>(state.dragAnchorViewStart) - shift, static_cast<s64>(state.dragAnchorViewEnd) - shift, canvas.sampleCount);
}

void HandleViewWheel(LoopWaveformState& state, const WaveformCanvas& canvas)
{
    const ImGuiIO& io = GetIO();

    if (!canvas.hovered)
        return;

    if (io.MouseWheel != 0.0f)
    {
        if (shortcuts::Held(shortcuts::Modifier::PanHorizontal))
        {
            PanViewByWheel(state, io.MouseWheel, canvas.sampleCount);
        }
        else
        {
            const double factor = io.MouseWheel > 0 ? (1.0 / cWheelZoomFactor) : cWheelZoomFactor;
            ZoomAt(state, XToSample(state, canvas.mouseX, canvas.width), factor, canvas.sampleCount);
        }
    }

    if (io.MouseWheelH != 0.0f)
        PanViewByWheel(state, io.MouseWheelH, canvas.sampleCount);
}

void DrawMinimap(LoopWaveformState& state, float width, u32 sampleCount)
{
    float left = static_cast<float>(state.viewStart) / sampleCount;
    float right = static_cast<float>(state.viewEnd) / sampleCount;

    Dummy(ImVec2(0.0f, cControlPad - GetStyle().ItemSpacing.y));

    const float prevLeft = left;
    const float prevRight = right;
    const u32 prevSpan = ViewSpan(state);

    const ZoomScrollResult result = ZoomScrollBar("##scrollbar", width, &left, &right, std::min(1.0f, static_cast<float>(cMinViewSpan) / sampleCount), 1.0f);

    if (result == ZoomScrollResult::Reset)
    {
        ResetView(state, sampleCount);
    }
    else if (result == ZoomScrollResult::Scrolled)
    {
        if (left == prevLeft)
            return;

        const u32 maxStart = sampleCount > prevSpan ? sampleCount - prevSpan : 0;

        state.viewStart = std::min(maxStart, static_cast<u32>(std::lround(static_cast<double>(left) / std::max(1.0e-9, 1.0 - (static_cast<double>(prevRight) - prevLeft)) * maxStart)));
        state.viewEnd = state.viewStart + prevSpan;

        ClampView(state, sampleCount);
        SnapView(state);
    }
    else if (result == ZoomScrollResult::Zoomed)
    {
        if (left != prevLeft)
            state.viewStart = static_cast<u32>(std::lround(static_cast<double>(left) * sampleCount));

        if (right != prevRight)
            state.viewEnd = static_cast<u32>(std::lround(static_cast<double>(right) * sampleCount));

        ClampView(state, sampleCount);
        SnapView(state);
    }
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
