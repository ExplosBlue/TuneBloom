#include <ui/wave/LoopWaveformEditor.h>

#include <ui/wave/LoopWaveformInternal.h>

#include <imgui/imgui_internal.h>

#include <math/seadMathCalcCommon.h>

#include <algorithm>
#include <cmath>

namespace ImGui
{

using Dragging = LoopWaveformState::Dragging;

constexpr float  cDefaultCanvasH   = 240.0f;
constexpr float  cSeekRulerH       = 30.0f;
constexpr float  cReadoutTextScale = 0.85f;
constexpr float  cCropGrabPx       = 8.0f;
constexpr float  cMarkerGrabPx     = 6.0f;

static bool IsCropDrag(Dragging dragging)
{
    return dragging == Dragging::CropStart || dragging == Dragging::CropEnd;
}

static bool IsLoopDrag(Dragging dragging)
{
    return dragging == Dragging::Start || dragging == Dragging::End;
}

static WaveformCanvas ReadCanvasInput(ImVec2 origin, ImVec2 size, u32 sampleCount)
{
    const ImGuiIO& io = GetIO();

    WaveformCanvas canvas;
    canvas.origin = origin;
    canvas.width = size.x;
    canvas.height = size.y;
    canvas.sampleCount = sampleCount;
    canvas.hovered = IsItemHovered();
    canvas.mouseX = sead::Mathf::clamp2(0.0f, io.MousePos.x - origin.x, size.x);
    canvas.mouse = ImVec2(io.MousePos.x - origin.x, io.MousePos.y - origin.y);

    return canvas;
}

static Dragging FindEdgeHover(const LoopWaveformState& state, const WaveformCanvas& canvas, u32 start, u32 end, float grabPx, Dragging startEdge, Dragging endEdge)
{
    const float startDistance = std::fabs(canvas.mouseX - SampleToX(state, start, canvas.width));
    const float endDistance = std::fabs(canvas.mouseX - SampleToX(state, end, canvas.width));

    if (startDistance <= grabPx && startDistance <= endDistance)
        return startEdge;

    if (endDistance <= grabPx)
        return endEdge;

    return Dragging::None;
}

static u32 FindMouseSample(const LoopWaveformState& state, const WaveformCanvas& canvas)
{
    const u32 sample = static_cast<u32>(std::max(0.0, std::round(XToSample(state, canvas.mouseX, canvas.width))));

    return std::min(sample, canvas.sampleCount);
}

static void BeginDrag(LoopWaveformState& state, Dragging cropHover, Dragging markerHover, bool grabMarkers, ImGuiMouseButton endButton, bool editable)
{
    const bool startClicked = IsMouseClicked(ImGuiMouseButton_Left) && (!grabMarkers || markerHover == Dragging::Start);
    const bool endClicked = IsMouseClicked(endButton) && (!grabMarkers || markerHover == Dragging::End);

    if (cropHover != Dragging::None && IsMouseClicked(ImGuiMouseButton_Left))
    {
        state.dragging = cropHover;
    }
    else if (editable && startClicked)
    {
        state.dragging = Dragging::Start;
        state.focus = LoopWaveformState::Focus::Start;
    }
    else if (editable && endClicked && !state.lockEnd)
    {
        state.dragging = Dragging::End;
        state.focus = LoopWaveformState::Focus::End;
    }
    else if (IsMouseClicked(ImGuiMouseButton_Middle))
    {
        StartViewPan(state);
    }
}

static bool DragCropEdge(LoopWaveformState& state, const WaveformCanvas& canvas)
{
    if (!IsMouseDown(ImGuiMouseButton_Left))
    {
        state.dragging = Dragging::None;
        return false;
    }

    const u32 sample = FindMouseSample(state, canvas);

    if (state.dragging == Dragging::CropStart)
    {
        const u32 newStart = std::min(sample, state.cropEnd > 0 ? state.cropEnd - 1 : 0);

        if (newStart == state.cropStart)
            return false;

        state.cropStart = newStart;
        return true;
    }

    const u32 newEnd = std::max(sample, state.cropStart + 1);

    if (newEnd == state.cropEnd)
        return false;

    state.cropEnd = newEnd;
    return true;
}

static bool DragLoopEdge(LoopWaveformState& state, const WaveformCanvas& canvas, u32& loopStart, u32& loopEnd, ImGuiMouseButton endButton)
{
    const bool stillDown = (state.dragging == Dragging::Start) ? IsMouseDown(ImGuiMouseButton_Left) : IsMouseDown(endButton);

    if (!stillDown)
    {
        state.dragging = Dragging::None;
        return false;
    }

    const u32 sample = FindMouseSample(state, canvas);

    if (state.dragging == Dragging::Start)
    {
        const u32 newStart = std::min(sample, loopEnd > 0 ? loopEnd - 1 : 0);

        if (newStart == loopStart)
            return false;

        loopStart = newStart;
        return true;
    }

    const u32 newEnd = std::max(sample, loopStart + 1);

    if (newEnd == loopEnd)
        return false;

    loopEnd = newEnd;
    return true;
}

static int FindCueHover(const LoopWaveformState& state, const WaveformCanvas& canvas)
{
    for (int i = static_cast<int>(state.cues.size()) - 1; i >= 0; i--)
    {
        const LoopWaveformState::CueLabel label = i < static_cast<int>(state.cueLabels.size()) ? state.cueLabels[i] : LoopWaveformState::CueLabel();
        const bool onLabel = canvas.mouse.x >= label.minX && canvas.mouse.x < label.maxX && canvas.mouse.y >= label.minY && canvas.mouse.y < label.maxY;
        const bool onLine = std::fabs(canvas.mouseX - SampleToX(state, state.cues[i].start, canvas.width)) <= cMarkerGrabPx;

        if (onLabel || onLine)
            return i;
    }

    return -1;
}

static void BeginCueDrag(LoopWaveformState& state, const WaveformCanvas& canvas)
{
    state.cueSelected = state.cueHover;

    if (!state.cuesEditable)
        return;

    state.dragging = Dragging::Cue;
    state.cueDragIndex = state.cueHover;
    state.dragAnchorMouseX = canvas.mouseX - SampleToX(state, state.cues[state.cueHover].start, canvas.width);
}

static void DragCue(LoopWaveformState& state, const WaveformCanvas& canvas)
{
    if (!IsMouseDown(ImGuiMouseButton_Left) || state.cueDragIndex < 0 || state.cueDragIndex >= static_cast<int>(state.cues.size()))
    {
        state.dragging = Dragging::None;
        state.cueDragIndex = -1;
        return;
    }

    LoopWaveformState::Cue& cue = state.cues[state.cueDragIndex];
    const double sample = std::max(0.0, std::round(XToSample(state, canvas.mouseX - state.dragAnchorMouseX, canvas.width)));
    const u32 maxStart = canvas.sampleCount > cue.length ? canvas.sampleCount - cue.length : 0;

    cue.start = std::min(static_cast<u32>(sample), maxStart);
}

static bool HandleInteraction(LoopWaveformState& state, const WaveformCanvas& canvas, u32& loopStart, u32& loopEnd, bool editable)
{
    bool changed = false;

    Dragging cropHover = Dragging::None;

    if (state.showCrop && canvas.hovered)
        cropHover = FindEdgeHover(state, canvas, state.cropStart, state.cropEnd, cCropGrabPx, Dragging::CropStart, Dragging::CropEnd);

    UpdateCurveHover(state, canvas);

    if (state.curveHoverIndex >= 0)
    {
        cropHover = Dragging::None;
        SetMouseCursor(state.curveHoverTension ? ImGuiMouseCursor_ResizeNS : ImGuiMouseCursor_ResizeAll);
    }

    const bool grabMarkers = state.grabMarkers || state.showCurve;
    Dragging markerHover = Dragging::None;

    if (grabMarkers && editable && canvas.hovered && state.curveHoverIndex < 0)
        markerHover = FindEdgeHover(state, canvas, loopStart, loopEnd, cMarkerGrabPx, Dragging::Start, state.lockEnd ? Dragging::None : Dragging::End);

    if (cropHover != Dragging::None && markerHover != Dragging::None)
    {
        if (canvas.mouse.y < canvas.height * 0.5f)
            cropHover = Dragging::None;
        else
            markerHover = Dragging::None;
    }

    const bool cropDragging = IsCropDrag(state.dragging);

    if (cropDragging || (cropHover != Dragging::None && state.dragging == Dragging::None))
        SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    if (grabMarkers && (IsLoopDrag(state.dragging) || (markerHover != Dragging::None && state.dragging == Dragging::None)))
        SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    if (state.dragging == Dragging::None)
        state.cueHover = canvas.hovered && markerHover == Dragging::None && cropHover == Dragging::None ? FindCueHover(state, canvas) : -1;

    if (state.cuesEditable && (state.dragging == Dragging::Cue || (state.cueHover >= 0 && state.dragging == Dragging::None)))
        SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    const ImGuiMouseButton endButton = grabMarkers ? ImGuiMouseButton_Left : ImGuiMouseButton_Right;
    const bool curveEdit = state.showCurve && canvas.hovered && state.dragging == Dragging::None && BeginCurveEdit(state, canvas.mouse, canvas.width, canvas.height, &changed);
    const bool canBegin = canvas.hovered && !curveEdit && state.dragging == Dragging::None;

    if (canBegin && state.cueHover >= 0 && IsMouseClicked(ImGuiMouseButton_Left))
        BeginCueDrag(state, canvas);
    else if (canBegin)
        BeginDrag(state, cropHover, markerHover, grabMarkers, endButton, editable);

    if (state.dragging == Dragging::Cue)
    {
        DragCue(state, canvas);
    }
    else if (state.dragging == Dragging::Pan)
    {
        DragViewPan(state, canvas);
    }
    else if (IsCurveDrag(state.dragging))
    {
        if (IsMouseDown(state.curveDragButton))
        {
            changed |= DragCurve(state, canvas.mouse, canvas.width, canvas.height);
        }
        else
        {
            state.dragging = Dragging::None;
            state.curveDragIndex = -1;
        }
    }
    else if (cropDragging)
    {
        changed |= DragCropEdge(state, canvas);
    }
    else if (IsLoopDrag(state.dragging))
    {
        changed |= DragLoopEdge(state, canvas, loopStart, loopEnd, endButton);
    }

    HandleViewWheel(state, canvas);
    DrawCurveTooltip(state);

    return changed;
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

    UpdatePeakCache(state, mono);

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
        draw->AddText(GetFont(), GetFontSize() * cReadoutTextScale, ImVec2(origin.x + 6.0f, origin.y + 4.0f), Palette().inkFaint, readoutText);

    InvisibleButton("##canvas", size);

    SetItemKeyOwner(ImGuiKey_MouseWheelY);
    SetItemKeyOwner(ImGuiKey_MouseWheelX);

    bool changed = false;

    if (sampleCount > 0)
        changed = HandleInteraction(state, ReadCanvasInput(origin, size, sampleCount), loopStart, loopEnd, showLoop && editable);

    if (sampleCount > 0 && state.showCurve)
        changed |= DrawCurvePointMenu(state);

    if (sampleCount > 0 && showSeekBar)
        DrawSeekRuler("##seek", GetCursorScreenPos(), size.x, cSeekRulerH, state, sampleRate, playheadSample, showLoop, loopStart, loopEnd, outSeekSample);

    if (sampleCount > 0)
        DrawMinimap(state, size.x, sampleCount);

    PopID();
    return changed;
}

}
