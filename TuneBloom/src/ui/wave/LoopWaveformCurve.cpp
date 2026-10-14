#include <ui/wave/LoopWaveformInternal.h>

#include <ui/widgets/ContextMenu.h>
#include <ui/Messages.h>
#include <ui/settings/UiColors.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_custom.h>
#include <imgui/imgui_internal.h>

#include <math/seadMathCalcCommon.h>

#include <algorithm>
#include <cmath>

namespace ImGui
{

constexpr float  cCurveInset        = 7.0f;
constexpr float  cCurvePointRadius  = 4.0f;
constexpr float  cCurveHandleRadius = 3.0f;
constexpr float  cCurveHoverGrow    = 1.5f;
constexpr float  cCurveGrabPx       = 7.0f;
constexpr float  cCurveHandleMinPx  = 24.0f;
constexpr float  cCurveTensionPx    = 60.0f;
constexpr float  cCurvePointGap     = 0.001f;
constexpr float  cCurveLineWidth    = 1.5f;
constexpr int    cCurveLineAlpha    = 200;

bool IsCurveDrag(LoopWaveformState::Dragging dragging)
{
    return dragging == LoopWaveformState::Dragging::CurvePoint || dragging == LoopWaveformState::Dragging::CurveTension;
}

static float CurveSpan(const LoopWaveformState& state)
{
    return state.cropEnd > state.cropStart ? static_cast<float>(state.cropEnd - state.cropStart) : 1.0f;
}

float CurveGainAt(const LoopWaveformState& state, float sample)
{
    const float position = (sample - static_cast<float>(state.cropStart)) / CurveSpan(state);

    if (position < 0.0f || position > 1.0f)
        return 1.0f;

    return AudioProcessing::curveValue(state.curve, position);
}

static float CurveToX(const LoopWaveformState& state, float position, float width)
{
    const float sample = static_cast<float>(state.cropStart) + position * CurveSpan(state);

    return (sample - static_cast<float>(state.viewStart)) / static_cast<float>(ViewSpan(state)) * width;
}

static float CurveFromX(const LoopWaveformState& state, float x, float width)
{
    return sead::Mathf::clamp2(0.0f, (static_cast<float>(XToSample(state, x, width)) - static_cast<float>(state.cropStart)) / CurveSpan(state), 1.0f);
}

static float CurveToY(float value, float height)
{
    return cCurveInset + (1.0f - value) * (height - cCurveInset * 2.0f);
}

static float CurveFromY(float y, float height)
{
    return sead::Mathf::clamp2(0.0f, 1.0f - (y - cCurveInset) / (height - cCurveInset * 2.0f), 1.0f);
}

static ImVec2 CurvePointPos(const LoopWaveformState& state, int index, float width, float height)
{
    const AudioProcessing::CurvePoint& point = state.curve[index];

    return ImVec2(CurveToX(state, point.x, width), CurveToY(point.y, height));
}

static ImVec2 CurveHandlePos(const LoopWaveformState& state, int index, float width, float height)
{
    const float position = (state.curve[index - 1].x + state.curve[index].x) * 0.5f;

    return ImVec2(CurveToX(state, position, width), CurveToY(AudioProcessing::curveValue(state.curve, position), height));
}

static bool HasCurveHandle(const LoopWaveformState& state, int index, float width)
{
    if (index < 1 || index >= static_cast<int>(state.curve.size()))
        return false;

    const AudioProcessing::CurvePoint& from = state.curve[index - 1];
    const AudioProcessing::CurvePoint& to = state.curve[index];

    if (to.shape == AudioProcessing::CurveShape::Hold || to.shape == AudioProcessing::CurveShape::HalfSine || from.y == to.y)
        return false;

    return CurveToX(state, to.x, width) - CurveToX(state, from.x, width) >= cCurveHandleMinPx;
}

void DrawCurve(ImDrawList* draw, ImVec2 origin, float width, float height, const LoopWaveformState& state)
{
    if (state.curve.empty())
        return;

    const int pointNum = static_cast<int>(state.curve.size());
    const ImU32 ink = uicolors::WithAlpha(GetColorU32(ImGuiCol_Text), 255);
    const ImGuiID baseId = GetID("##curve");
    const float firstX = std::max(0.0f, CurveToX(state, 0.0f, width));
    const float lastX = std::min(width, CurveToX(state, 1.0f, width));
    const float pointReach = cCurvePointRadius + cCurveHoverGrow;

    draw->PushClipRect(ImVec2(origin.x - pointReach, origin.y), ImVec2(origin.x + width + pointReach, origin.y + height), true);

    // Line
    for (float x = firstX; x < lastX; x += 1.0f)
        draw->PathLineTo(ImVec2(origin.x + x, origin.y + CurveToY(AudioProcessing::curveValue(state.curve, CurveFromX(state, x, width)), height)));

    if (lastX >= firstX)
        draw->PathLineTo(ImVec2(origin.x + lastX, origin.y + CurveToY(AudioProcessing::curveValue(state.curve, CurveFromX(state, lastX, width)), height)));

    draw->PathStroke(uicolors::WithAlpha(ink, cCurveLineAlpha), ImDrawFlags_None, cCurveLineWidth);

    // Tension handles
    for (int i = 1; i < pointNum; i++)
    {
        if (!HasCurveHandle(state, i, width))
            continue;

        const ImVec2 pos = CurveHandlePos(state, i, width, height);
        const ImVec2 center(origin.x + pos.x, origin.y + pos.y);
        const bool lit = state.curveHoverIndex == i && state.curveHoverTension;
        const float radius = cCurveHandleRadius + GetHoverRatio(baseId + static_cast<ImGuiID>(i * 2 + 1), lit) * cCurveHoverGrow;

        draw->AddCircleFilled(center, radius, Palette().bg);
        draw->AddCircle(center, radius, ink, 0, 1.0f);
    }

    // Points
    for (int i = 0; i < pointNum; i++)
    {
        const ImVec2 pos = CurvePointPos(state, i, width, height);
        const bool lit = state.curveHoverIndex == i && !state.curveHoverTension;
        const float radius = cCurvePointRadius + GetHoverRatio(baseId + static_cast<ImGuiID>(i * 2), lit) * cCurveHoverGrow;

        draw->AddCircleFilled(ImVec2(origin.x + pos.x, origin.y + pos.y), radius, ink);
    }

    draw->PopClipRect();
}

static void FindCurveHover(const LoopWaveformState& state, ImVec2 mouse, float width, float height, int* index, bool* isTension)
{
    const int pointNum = static_cast<int>(state.curve.size());
    const float reach = cCurveGrabPx * cCurveGrabPx;

    *index = -1;
    *isTension = false;

    for (int i = 0; i < pointNum; i++)
    {
        const ImVec2 pos = CurvePointPos(state, i, width, height);

        if (ImLengthSqr(ImVec2(mouse.x - pos.x, mouse.y - pos.y)) <= reach)
        {
            *index = i;
            return;
        }
    }

    for (int i = 1; i < pointNum; i++)
    {
        if (!HasCurveHandle(state, i, width))
            continue;

        const ImVec2 pos = CurveHandlePos(state, i, width, height);

        if (ImLengthSqr(ImVec2(mouse.x - pos.x, mouse.y - pos.y)) <= reach)
        {
            *index = i;
            *isTension = true;
            return;
        }
    }
}

void UpdateCurveHover(LoopWaveformState& state, const WaveformCanvas& canvas)
{
    if (IsCurveDrag(state.dragging))
        return;

    state.curveHoverIndex = -1;
    state.curveHoverTension = false;

    if (state.showCurve && canvas.hovered && state.dragging == LoopWaveformState::Dragging::None)
        FindCurveHover(state, canvas.mouse, canvas.width, canvas.height, &state.curveHoverIndex, &state.curveHoverTension);
}

void DrawCurveTooltip(const LoopWaveformState& state)
{
    if (state.curveHoverIndex < 0 || state.curveHoverIndex >= static_cast<int>(state.curve.size()))
        return;

    const AudioProcessing::CurvePoint& point = state.curve[state.curveHoverIndex];

    if (state.curveHoverTension)
        SetTooltip(messages::import::cWaveCurveTensionTipFormat, point.tension * 100.0f);
    else if (point.y <= 0.0f)
        SetTooltip("%s", messages::import::cWaveCurveSilentTip);
    else
        SetTooltip(messages::import::cWaveCurveLevelTipFormat, 20.0f * std::log10(point.y));
}

static void StartCurveDrag(LoopWaveformState& state, int index, bool isTension, ImGuiMouseButton button, float mouseY)
{
    state.dragging = isTension ? LoopWaveformState::Dragging::CurveTension : LoopWaveformState::Dragging::CurvePoint;
    state.curveHoverIndex = index;
    state.curveHoverTension = isTension;
    state.curveDragIndex = index;
    state.curveDragButton = button;
    state.curveDragTension = state.curve[index].tension;
    state.curveDragMouseY = mouseY;
}

static void AddCurvePoint(LoopWaveformState& state, ImVec2 mouse, float width, float height)
{
    const float position = CurveFromX(state, mouse.x, width);
    int at = state.curve.empty() ? 0 : 1;

    while (at + 1 < static_cast<int>(state.curve.size()) && state.curve[at].x < position)
        at++;

    AudioProcessing::CurvePoint point;
    point.x = position;
    point.y = CurveFromY(mouse.y, height);

    state.curve.insert(state.curve.begin() + at, point);

    StartCurveDrag(state, at, false, ImGuiMouseButton_Right, mouse.y);
}

bool BeginCurveEdit(LoopWaveformState& state, ImVec2 mouse, float width, float height, bool* changed)
{
    const int index = state.curveHoverIndex;

    if (IsMouseClicked(ImGuiMouseButton_Right))
    {
        if (index >= 0 && !state.curveHoverTension)
        {
            state.curveMenuIndex = index;
            OpenPopup("##curvePoint");
        }
        else if (index >= 0)
        {
            state.curve[index].tension = 0.0f;
            *changed = true;
        }
        else if (mouse.x >= CurveToX(state, 0.0f, width) && mouse.x <= CurveToX(state, 1.0f, width))
        {
            AddCurvePoint(state, mouse, width, height);
            *changed = true;
        }

        return true;
    }

    if (index < 0 || !IsMouseClicked(ImGuiMouseButton_Left))
        return false;

    if (state.curveHoverTension && GetIO().KeyAlt)
    {
        state.curve[index].tension = 0.0f;
        *changed = true;
        return true;
    }

    StartCurveDrag(state, index, state.curveHoverTension, ImGuiMouseButton_Left, mouse.y);
    return true;
}

bool DragCurve(LoopWaveformState& state, ImVec2 mouse, float width, float height)
{
    const int index = state.curveDragIndex;
    const int pointNum = static_cast<int>(state.curve.size());

    if (index < 0 || index >= pointNum)
        return false;

    AudioProcessing::CurvePoint& point = state.curve[index];
    const AudioProcessing::CurvePoint before = point;

    if (state.dragging == LoopWaveformState::Dragging::CurvePoint)
    {
        if (index > 0 && index + 1 < pointNum)
        {
            const float low = state.curve[index - 1].x + cCurvePointGap;
            const float high = std::max(low, state.curve[index + 1].x - cCurvePointGap);

            point.x = sead::Mathf::clamp2(low, CurveFromX(state, mouse.x, width), high);
        }

        point.y = CurveFromY(mouse.y, height);
    }
    else if (index > 0)
    {
        const float up = (state.curveDragMouseY - mouse.y) / cCurveTensionPx;
        const bool bends = point.shape == AudioProcessing::CurveShape::SingleCurve || point.shape == AudioProcessing::CurveShape::DoubleCurve;
        const float direction = bends && point.y > state.curve[index - 1].y ? -1.0f : 1.0f;

        point.tension = sead::Mathf::clamp2(-1.0f, state.curveDragTension + direction * up, 1.0f);
    }

    return point.x != before.x || point.y != before.y || point.tension != before.tension;
}

bool DrawCurvePointMenu(LoopWaveformState& state)
{
    static const char* const cShapeNames[] =
    {
        messages::import::cWaveCurveSingle, messages::import::cWaveCurveDouble, messages::common::cHold, messages::import::cWaveCurveStairs,
        messages::import::cWaveCurveSmoothStairs, messages::import::cWaveCurvePulse, messages::common::cWave, messages::import::cWaveCurveHalfSine,
    };

    if (!BeginContextMenu("##curvePoint"))
        return false;

    const int index = state.curveMenuIndex;
    const int pointNum = static_cast<int>(state.curve.size());
    bool changed = false;

    if (index >= 0 && index < pointNum)
    {
        const bool isEnd = index == 0 || index + 1 == pointNum;
        const AudioProcessing::CurveShape shape = state.curve[index].shape;
        bool remove = false;

        if (isEnd)
            ContextMenuItem(ICON_LC_TRASH_2, messages::import::cWaveCurveDeletePoint, false, messages::import::cWaveCurveEndPointReason);
        else
            remove = ContextMenuDangerItem(ICON_LC_TRASH_2, messages::import::cWaveCurveDeletePoint);

        ContextMenuSeparator();

        for (int i = 0; i < static_cast<int>(AudioProcessing::CurveShape::Num); i++)
        {
            if (ContextMenuCheckItem(cShapeNames[i], index > 0 && static_cast<int>(shape) == i, index > 0))
            {
                state.curve[index].shape = static_cast<AudioProcessing::CurveShape>(i);
                changed = true;
            }
        }

        if (remove)
        {
            state.curve.erase(state.curve.begin() + index);
            state.curveMenuIndex = -1;
            changed = true;
        }
    }

    EndContextMenu();

    return changed;
}

}
