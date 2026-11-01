#include <ui/stream/StreamRegions.h>

#include <ui/file/File.h>
#include <ui/wave/LoopWaveformInternal.h>
#include <messages/Messages.h>
#include <ui/settings/Theme.h>
#include <ui/app/Shortcuts.h>
#include <ui/widgets/LaneRow.h>
#include <ui/widgets/RangeCell.h>
#include <ui/widgets/RangeStrip.h>

#include <bfsar/files/StreamRegion.h>
#include <bfsar/files/WaveFile.h>

#include <imgui/imgui_custom.h>
#include <imgui/imgui_internal.h>

#include <math/seadMathCalcCommon.h>

#include <algorithm>
#include <cmath>
#include <vector>

static constexpr f32 cLaneHeightEm   = 1.3f;
static constexpr f32 cLaneEdgeGrabPx = 5.0f;
static constexpr f32 cMarkIdleAlpha  = 0.65f;
static constexpr f32 cRowHoverLift   = 0.43f;
static constexpr f32 cRowGripWidth   = 2.0f;
static constexpr f32 cRowGripAlpha   = 0.8f;

namespace
{

enum class LaneDrag
{
    None,
    Start,
    End,
    Move
};

struct LaneState
{
    const WaveFile* wave = nullptr;
    s32 regionNo = -1;
    LaneDrag drag = LaneDrag::None;
    u32 anchorStart = 0;
    u32 anchorEnd = 0;
    f64 anchorSample = 0.0;
    bool moved = false;
    ImGuiID ownerId = 0;
    s32 ownerFrame = 0;
};

struct Lane
{
    WaveFile* wave;
    ImGui::LoopWaveformState& state;
    ImVec2 origin;
    f32 width;
    f32 height;
    s32 playingNo;
    bool editable;
    s32 onlyNo = -1;
};

}

static LaneState sLane;

static void CalcLaneBlock(const Lane& lane, const StreamRegion& region, f32* left, f32* right)
{
    *left = lane.origin.x + sead::Mathf::clamp2(0.0f, ImGui::SampleToX(lane.state, region.start, lane.width), lane.width);
    *right = lane.origin.x + sead::Mathf::clamp2(0.0f, ImGui::SampleToX(lane.state, region.end, lane.width), lane.width);
}

static bool IsLaneSampleShown(const Lane& lane, u32 sample)
{
    const f32 x = ImGui::SampleToX(lane.state, sample, lane.width);

    return x >= 0.0f && x <= lane.width;
}

static s32 FindLaneBlock(const Lane& lane, f32 mouseX, LaneDrag* part)
{
    const std::vector<StreamRegion>& regions = lane.wave->getStreamFileLayout().regions;

    for (s32 regionNo = static_cast<s32>(regions.size()) - 1; regionNo >= 0; regionNo--)
    {
        if (lane.onlyNo != -1 && regionNo != lane.onlyNo)
            continue;

        const StreamRegion& region = regions[regionNo];

        f32 left = 0.0f;
        f32 right = 0.0f;
        CalcLaneBlock(lane, region, &left, &right);

        if (mouseX < left - cLaneEdgeGrabPx || mouseX > right + cLaneEdgeGrabPx || right <= left)
            continue;

        const f32 grab = sead::Mathf::min(cLaneEdgeGrabPx, (right - left) * 0.25f);

        if (mouseX <= left + grab && IsLaneSampleShown(lane, region.start))
            *part = LaneDrag::Start;
        else if (mouseX >= right - grab && IsLaneSampleShown(lane, region.end))
            *part = LaneDrag::End;
        else
            *part = LaneDrag::Move;

        return regionNo;
    }

    *part = LaneDrag::None;

    return -1;
}

static bool CalcLaneSegment(const Lane& lane, s32 regionNo, ImVec2* min, ImVec2* max)
{
    f32 left = 0.0f;
    f32 right = 0.0f;
    CalcLaneBlock(lane, lane.wave->getStreamFileLayout().regions[regionNo], &left, &right);

    *min = ImVec2(ImFloor(left), lane.origin.y);
    *max = ImVec2(ImFloor(right), lane.origin.y + lane.height - 1.0f);

    return max->x - min->x >= 1.0f;
}

static std::vector<RangeSpan> CollectLaneSpans(const Lane& lane)
{
    const s32 regionCount = static_cast<s32>(lane.wave->getStreamFileLayout().regions.size());

    std::vector<RangeSpan> spans(regionCount, RangeSpan{ 0.0f, 0.0f });
    for (s32 regionNo = 0; regionNo < regionCount; regionNo++)
    {
        ImVec2 min;
        ImVec2 max;
        if (CalcLaneSegment(lane, regionNo, &min, &max))
            spans[regionNo] = { min.x, max.x };
    }

    return spans;
}

static void DrawLaneBlock(const Lane& lane, const std::vector<RangeSpan>& spans, s32 regionNo)
{
    const StreamRegion& region = lane.wave->getStreamFileLayout().regions[regionNo];

    RangeSegment segment;
    if (!CalcLaneSegment(lane, regionNo, &segment.min, &segment.max))
        return;

    const sead::FormatFixedSafeString<16> number("%d", regionNo);
    const sead::FormatFixedSafeString<96> label("%d %s", regionNo, region.name.cstr());
    const bool hasLabelSpace = SetRangeSegmentCover(&segment, spans.data(), static_cast<s32>(spans.size()), regionNo);

    segment.color = GetSeriesColor(regionNo);
    segment.backdrop = ImGui::ColorConvertU32ToFloat4(ImGui::Palette().bg);
    segment.label = hasLabelSpace && !region.name.isEmpty() ? label.cstr() : nullptr;
    segment.shortLabel = hasLabelSpace ? number.cstr() : nullptr;
    segment.strong = regionNo == lane.playingNo;
    segment.marked = GetSelectedStreamRegion(lane.wave) == regionNo;

    DrawRangeSegment(ImGui::GetWindowDrawList(), segment);
}

static void DragLaneBlock(const Lane& lane, f32 mouseX)
{
    const StreamRegion& region = lane.wave->getStreamFileLayout().regions[sLane.regionNo];
    const f64 sampleCount = lane.wave->getSampleCount();
    const f64 sample = std::clamp(std::round(ImGui::XToSample(lane.state, mouseX - lane.origin.x, lane.width)), 0.0, sampleCount);

    if (!sLane.moved && !ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        return;

    sLane.moved = true;

    if (sLane.drag == LaneDrag::Start)
    {
        SetStreamRegionRange(lane.wave, sLane.regionNo, static_cast<u32>(std::min(sample, region.end - 1.0)), region.end);
    }
    else if (sLane.drag == LaneDrag::End)
    {
        SetStreamRegionRange(lane.wave, sLane.regionNo, region.start, static_cast<u32>(std::max(sample, region.start + 1.0)));
    }
    else
    {
        const f64 length = static_cast<f64>(sLane.anchorEnd) - sLane.anchorStart;
        const f64 start = std::clamp(sLane.anchorStart + sample - sLane.anchorSample, 0.0, sampleCount - length);

        SetStreamRegionRange(lane.wave, sLane.regionNo, static_cast<u32>(start), static_cast<u32>(start + length));
    }

    ImGui::SetTooltip(messages::stream::cRegionRangeFormat, region.start, region.end);
}

static bool IsLaneDragOfOther(ImGuiID laneId)
{
    if (sLane.drag == LaneDrag::None || sLane.ownerId == laneId)
        return false;

    if (sLane.ownerFrame >= ImGui::GetFrameCount() - 1)
        return true;

    sLane = LaneState();

    return false;
}

static s32 HandleLaneInput(const Lane& lane, ImGuiID laneId, s32 hoveredNo, LaneDrag hoveredPart)
{
    const f32 mouseX = ImGui::GetIO().MousePos.x;

    if (IsLaneDragOfOther(laneId))
        return -1;

    if (sLane.wave != lane.wave || sLane.regionNo >= static_cast<s32>(lane.wave->getStreamFileLayout().regions.size()))
        sLane = LaneState();

    s32 clickedNo = -1;

    if (sLane.drag != LaneDrag::None)
    {
        sLane.ownerFrame = ImGui::GetFrameCount();

        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            if (lane.editable)
                DragLaneBlock(lane, mouseX);
        }
        else
        {
            if (sLane.moved)
                CommitStreamRegionRange(lane.wave, sLane.regionNo);
            else
                clickedNo = sLane.regionNo;

            sLane = LaneState();
        }
    }
    else if (hoveredNo != -1 && ImGui::IsItemActivated())
    {
        const StreamRegion& region = lane.wave->getStreamFileLayout().regions[hoveredNo];

        sLane.wave = lane.wave;
        sLane.regionNo = hoveredNo;
        sLane.drag = lane.editable ? hoveredPart : LaneDrag::Move;
        sLane.anchorStart = region.start;
        sLane.anchorEnd = region.end;
        sLane.anchorSample = std::round(ImGui::XToSample(lane.state, mouseX - lane.origin.x, lane.width));
        sLane.moved = false;
        sLane.ownerId = laneId;
        sLane.ownerFrame = ImGui::GetFrameCount();
    }

    const LaneDrag part = sLane.drag != LaneDrag::None ? sLane.drag : hoveredPart;

    if (lane.editable && (part == LaneDrag::Start || part == LaneDrag::End))
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    else if (lane.editable && sLane.moved)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

    return clickedNo;
}

static ImGui::WaveformCanvas ReadLaneCanvas(const Lane& lane)
{
    return ImGui::ReadCanvasInput(lane.origin, ImVec2(lane.width, lane.height), lane.wave->getSampleCount());
}

void DrawStreamRegionLane(WaveFile* wave, ImGui::LoopWaveformState& state)
{
    const u32 sampleCount = wave->getSampleCount();

    if (sampleCount == 0)
        return;

    ImGui::InitViewIfNeeded(state, sampleCount);

    Lane lane = { wave, state };
    lane.origin = ImGui::GetCursorScreenPos();
    lane.width = sead::Mathf::max(1.0f, ImGui::GetContentRegionAvail().x);
    lane.height = ImFloor(ImGui::GetFontSize() * cLaneHeightEm);
    lane.editable = wave->getTarget().format == ArchiveFormat::BFSAR;

    lane.playingNo = GetPlayingStreamRegion(wave);

    NoteStreamRegionSelectionShown();

    const ImGui::WaveformPalette& palette = ImGui::Palette();
    const ImVec2 max(lane.origin.x + lane.width, lane.origin.y + lane.height);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    draw->AddRectFilled(lane.origin, max, palette.bg);
    draw->AddLine(ImVec2(lane.origin.x, max.y - 1.0f), ImVec2(max.x, max.y - 1.0f), palette.edge);

    ImGui::InvisibleButton("##regions", ImVec2(lane.width, lane.height));
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelX);

    const ImGuiID laneId = ImGui::GetItemID();
    const bool hovered = ImGui::IsItemHovered();

    LaneDrag hoveredPart = LaneDrag::None;
    const s32 hoveredNo = hovered ? FindLaneBlock(lane, ImGui::GetIO().MousePos.x, &hoveredPart) : -1;
    const s32 clickedNo = HandleLaneInput(lane, laneId, hoveredNo, hoveredPart);

    if (clickedNo != -1)
        SelectStreamRegion(wave, clickedNo);

    ImGui::HandleViewWheel(state, ReadLaneCanvas(lane));

    const s32 regionCount = static_cast<s32>(wave->getStreamFileLayout().regions.size());
    const s32 litNo = sLane.drag != LaneDrag::None ? sLane.regionNo : hoveredNo;

    const std::vector<RangeSpan> spans = CollectLaneSpans(lane);

    draw->PushClipRect(lane.origin, max, true);

    for (s32 regionNo = 0; regionNo < regionCount; regionNo++)
        DrawLaneBlock(lane, spans, regionNo);

    ImVec2 litMin;
    ImVec2 litMax;
    if (litNo != -1 && CalcLaneSegment(lane, litNo, &litMin, &litMax))
        DrawRangeSegmentHover(draw, litMin, litMax);

    draw->PopClipRect();

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y);
}

void DrawStreamRegionMarks(ImDrawList* draw, const ImVec2& min, const ImVec2& max, const WaveFile* wave, s32 onlyNo, const ImGui::LoopWaveformState* view)
{
    const std::vector<StreamRegion>& regions = wave->getStreamFileLayout().regions;
    const u32 sampleCount = wave->getSampleCount();
    const s32 regionCount = static_cast<s32>(regions.size());
    const u32 viewStart = view ? view->viewStart : 0;
    const u32 viewEnd = view ? view->viewEnd : sampleCount;

    if (sampleCount == 0)
        return;

    const s32 playingNo = GetPlayingStreamRegion(wave);

    for (s32 regionNo = 0; regionNo < regionCount; regionNo++)
    {
        if (onlyNo != -1 && regionNo != onlyNo)
            continue;

        if (regionNo == playingNo)
            continue;

        ImVec4 color = GetSeriesColor(regionNo);
        color.w *= cMarkIdleAlpha;

        DrawRangeMarkInView(draw, min, max, regions[regionNo].start, regions[regionNo].end, viewStart, viewEnd, ImGui::GetColorU32(color));
    }

    if (playingNo >= 0 && playingNo < regionCount && (onlyNo == -1 || playingNo == onlyNo))
        DrawRangeMarkInView(draw, min, max, regions[playingNo].start, regions[playingNo].end, viewStart, viewEnd, ImGui::GetColorU32(GetSeriesColor(playingNo)));
}

static void TakeRowLaneWheel()
{
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelX);

    if (shortcuts::Held(shortcuts::Modifier::ZoomHorizontal) || shortcuts::Held(shortcuts::Modifier::PanHorizontal))
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
}

static void DrawRowLaneEdgeGrip(const Lane& lane, LaneDrag part)
{
    if (!lane.editable || (part != LaneDrag::Start && part != LaneDrag::End))
        return;

    f32 left = 0.0f;
    f32 right = 0.0f;
    CalcLaneBlock(lane, lane.wave->getStreamFileLayout().regions[lane.onlyNo], &left, &right);

    const f32 x = part == LaneDrag::Start ? ImFloor(left) : ImFloor(right) - cRowGripWidth;

    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(x, lane.origin.y), ImVec2(x + cRowGripWidth, lane.origin.y + lane.height), ImGui::GetColorU32(ImGuiCol_Text, cRowGripAlpha));
}

static void DrawRowLaneBlockLit(const Lane& lane, bool hovered, bool dragged, LaneDrag part)
{
    const f32 hoverRatio = ImGui::GetHoverRatio(ImGui::GetID("###BlockLit"), hovered || dragged);
    const f32 lift = dragged ? 1.0f : hoverRatio * cRowHoverLift;

    if (lift > 0.0f && lane.onlyNo != lane.playingNo)
    {
        const StreamRegion& region = lane.wave->getStreamFileLayout().regions[lane.onlyNo];
        const ImVec2 max(lane.origin.x + lane.width, lane.origin.y + lane.height);

        ImVec4 color = GetSeriesColor(lane.onlyNo);
        color.w *= lift;

        DrawRangeMarkInView(ImGui::GetWindowDrawList(), lane.origin, max, region.start, region.end, lane.state.viewStart, lane.state.viewEnd, ImGui::GetColorU32(color));
    }

    DrawRowLaneEdgeGrip(lane, part);
}

RegionLaneClick DrawStreamRegionRowLane(WaveFile* wave, ImGui::LoopWaveformState& state, s32 regionNo, const ImVec2& min, const ImVec2& max, bool editable)
{
    const ImVec2 pad = CalcLanePad();

    Lane lane = { wave, state };
    lane.origin = min + pad;
    lane.width = sead::Mathf::max(1.0f, max.x - min.x - pad.x * 2.0f);
    lane.height = sead::Mathf::max(1.0f, max.y - min.y - pad.y * 2.0f);
    lane.playingNo = GetPlayingStreamRegion(wave);
    lane.editable = editable;
    lane.onlyNo = regionNo;

    const bool pressed = ImGui::InvisibleButton("##Lane", max - min);
    const ImGuiID laneId = ImGui::GetItemID();
    const bool hovered = ImGui::IsItemHovered();
    const bool rightClicked = hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right);

    if (hovered)
        TakeRowLaneWheel();

    LaneDrag hoveredPart = LaneDrag::None;
    const s32 hoveredNo = hovered ? FindLaneBlock(lane, ImGui::GetIO().MousePos.x, &hoveredPart) : -1;
    const bool onBlock = sLane.drag != LaneDrag::None && sLane.ownerId == laneId;
    const bool clicked = HandleLaneInput(lane, laneId, hoveredNo, hoveredPart) != -1;

    ImGui::HandleViewWheel(state, ReadLaneCanvas(lane), false);

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
        ImGui::StartViewPan(state);

    DrawStreamRegionMarks(ImGui::GetWindowDrawList(), lane.origin, ImVec2(lane.origin.x + lane.width, lane.origin.y + lane.height), wave, regionNo, &state);
    const bool dragged = sLane.drag != LaneDrag::None && sLane.ownerId == laneId;

    DrawRowLaneBlockLit(lane, hoveredNo != -1, dragged, dragged ? sLane.drag : hoveredPart);

    if (rightClicked)
        return RegionLaneClick::Menu;

    return clicked || (pressed && !onBlock) ? RegionLaneClick::Select : RegionLaneClick::None;
}

s32 FindStreamRegionAt(const WaveFile* wave, u32 sample)
{
    const std::vector<StreamRegion>& regions = wave->getStreamFileLayout().regions;

    for (s32 regionNo = static_cast<s32>(regions.size()) - 1; regionNo >= 0; regionNo--)
    {
        if (sample >= regions[regionNo].start && sample < regions[regionNo].end)
            return regionNo;
    }

    return -1;
}
