#include <ui/stream/StreamRegions.h>

#include <ui/file/File.h>
#include <ui/wave/LoopWaveformInternal.h>
#include <ui/Messages.h>
#include <ui/settings/Theme.h>

#include <bfsar/files/StreamRegion.h>
#include <bfsar/files/WaveFile.h>

#include <imgui/imgui_custom.h>
#include <imgui/imgui_internal.h>

#include <math/seadMathCalcCommon.h>

#include <algorithm>
#include <cmath>

static constexpr f32 cLaneHeightEm      = 1.3f;
static constexpr f32 cLaneBlockInset    = 2.0f;
static constexpr f32 cLaneBlockRounding = 3.0f;
static constexpr f32 cLaneEdgeGrabPx    = 5.0f;
static constexpr f32 cLaneTextPadPx     = 5.0f;
static constexpr f32 cLaneTextMinEm     = 1.0f;
static constexpr f32 cLaneFillMix       = 0.30f;
static constexpr f32 cLaneHoverMix      = 0.42f;
static constexpr f32 cLanePlayingMix    = 0.75f;
static constexpr f32 cLaneBorderMix     = 0.60f;
static constexpr f32 cMarkMinWidth      = 3.0f;
static constexpr f32 cMarkRounding      = 1.0f;
static constexpr f32 cMarkIdleAlpha     = 0.65f;

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
};

struct Lane
{
    WaveFile* wave;
    ImGui::LoopWaveformState& state;
    ImVec2 origin;
    f32 width;
    f32 height;
    s32 playingNo;
    s32 nextNo;
    bool editable;
};

}

static LaneState sLane;

static ImU32 MixLaneColor(s32 regionNo, f32 mix)
{
    const ImGui::WaveformPalette& palette = ImGui::Palette();

    return ImGui::ColorConvertFloat4ToU32(ImLerp(ImGui::ColorConvertU32ToFloat4(palette.bg), GetSeriesColor(regionNo), mix));
}

static void CalcLaneBlock(const Lane& lane, const StreamRegion& region, f32* left, f32* right)
{
    *left = lane.origin.x + sead::Mathf::clamp2(0.0f, ImGui::SampleToX(lane.state, region.start, lane.width), lane.width);
    *right = lane.origin.x + sead::Mathf::clamp2(0.0f, ImGui::SampleToX(lane.state, region.end, lane.width), lane.width);
}

static s32 FindLaneBlock(const Lane& lane, f32 mouseX, LaneDrag* part)
{
    const std::vector<StreamRegion>& regions = lane.wave->getStreamFileLayout().regions;

    for (s32 regionNo = static_cast<s32>(regions.size()) - 1; regionNo >= 0; regionNo--)
    {
        f32 left = 0.0f;
        f32 right = 0.0f;
        CalcLaneBlock(lane, regions[regionNo], &left, &right);

        if (mouseX < left - cLaneEdgeGrabPx || mouseX > right + cLaneEdgeGrabPx || right <= left)
            continue;

        const f32 grab = sead::Mathf::min(cLaneEdgeGrabPx, (right - left) * 0.25f);

        if (mouseX <= left + grab)
            *part = LaneDrag::Start;
        else if (mouseX >= right - grab)
            *part = LaneDrag::End;
        else
            *part = LaneDrag::Move;

        return regionNo;
    }

    *part = LaneDrag::None;

    return -1;
}

static void DrawLaneBlock(const Lane& lane, s32 regionNo, bool hovered, ImGuiID id)
{
    const StreamRegion& region = lane.wave->getStreamFileLayout().regions[regionNo];
    const bool selected = GetSelectedStreamRegion(lane.wave) == regionNo;
    const bool playing = regionNo == lane.playingNo;

    f32 left = 0.0f;
    f32 right = 0.0f;
    CalcLaneBlock(lane, region, &left, &right);

    if (right - left < 1.0f)
        return;

    const ImVec2 min(ImFloor(left), lane.origin.y + cLaneBlockInset);
    const ImVec2 max(ImFloor(right), lane.origin.y + lane.height - cLaneBlockInset);
    const ImU32 fill = playing ? MixLaneColor(regionNo, cLanePlayingMix) : ImGui::GetHoverColorU32(id, hovered, MixLaneColor(regionNo, cLaneFillMix), MixLaneColor(regionNo, cLaneHoverMix));
    const ImU32 border = selected ? ImGui::GetColorU32(ImGuiCol_Text) : MixLaneColor(regionNo, cLaneBorderMix);

    ImDrawList* draw = ImGui::GetWindowDrawList();

    draw->AddRectFilled(min, max, fill, cLaneBlockRounding);
    draw->AddRect(min, max, border, cLaneBlockRounding);

    if (max.x - min.x < ImGui::GetFontSize() * cLaneTextMinEm)
        return;

    sead::FixedSafeString<96> text;
    text.format("%d %s", regionNo, region.name.cstr());

    draw->PushClipRect(ImVec2(min.x + 1.0f, min.y), ImVec2(max.x - cLaneTextPadPx, max.y), true);
    draw->AddText(ImVec2(min.x + cLaneTextPadPx, ImFloor(min.y + (max.y - min.y - ImGui::GetFontSize()) * 0.5f)), ImGui::GetColorU32(ImGuiCol_Text), text.cstr());
    draw->PopClipRect();
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

static void HandleLaneInput(const Lane& lane, s32 hoveredNo, LaneDrag hoveredPart)
{
    const f32 mouseX = ImGui::GetIO().MousePos.x;

    if (sLane.wave != lane.wave || sLane.regionNo >= static_cast<s32>(lane.wave->getStreamFileLayout().regions.size()))
        sLane = LaneState();

    if (sLane.drag != LaneDrag::None)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            DragLaneBlock(lane, mouseX);
        }
        else
        {
            if (sLane.moved)
                CommitStreamRegionRange(lane.wave, sLane.regionNo);
            else
                ClickStreamRegion(lane.wave, sLane.regionNo);

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
    }

    const LaneDrag part = sLane.drag != LaneDrag::None ? sLane.drag : hoveredPart;

    if (lane.editable && (part == LaneDrag::Start || part == LaneDrag::End))
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    else if (lane.editable && sLane.moved)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
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
    lane.editable = sBfsar.getFormat() == ArchiveFormat::BFSAR;

    GetPlayingStreamRegions(wave, &lane.playingNo, &lane.nextNo);

    const ImGui::WaveformPalette& palette = ImGui::Palette();
    const ImVec2 max(lane.origin.x + lane.width, lane.origin.y + lane.height);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    draw->AddRectFilled(lane.origin, max, palette.bg);
    draw->AddLine(ImVec2(lane.origin.x, max.y - 1.0f), ImVec2(max.x, max.y - 1.0f), palette.edge);

    ImGui::InvisibleButton("##regions", ImVec2(lane.width, lane.height));
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelX);

    const ImGuiID id = ImGui::GetItemID();
    const bool hovered = ImGui::IsItemHovered();

    ImGui::WaveformCanvas canvas;
    canvas.origin = lane.origin;
    canvas.width = lane.width;
    canvas.height = lane.height;
    canvas.sampleCount = sampleCount;
    canvas.hovered = hovered;
    canvas.mouseX = sead::Mathf::clamp2(0.0f, ImGui::GetIO().MousePos.x - lane.origin.x, lane.width);
    canvas.mouse = ImVec2(ImGui::GetIO().MousePos.x - lane.origin.x, ImGui::GetIO().MousePos.y - lane.origin.y);

    LaneDrag hoveredPart = LaneDrag::None;
    const s32 hoveredNo = hovered ? FindLaneBlock(lane, ImGui::GetIO().MousePos.x, &hoveredPart) : -1;

    HandleLaneInput(lane, hoveredNo, hoveredPart);
    ImGui::HandleViewWheel(state, canvas);

    const s32 regionCount = static_cast<s32>(wave->getStreamFileLayout().regions.size());
    const s32 litNo = sLane.drag != LaneDrag::None ? sLane.regionNo : hoveredNo;

    draw->PushClipRect(lane.origin, max, true);

    for (s32 regionNo = 0; regionNo < regionCount; regionNo++)
        DrawLaneBlock(lane, regionNo, regionNo == litNo, id + 1 + regionNo);

    draw->PopClipRect();

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y);
}

static void DrawRegionMark(ImDrawList* draw, const ImVec2& min, const ImVec2& max, const StreamRegion& region, u32 sampleCount, ImU32 color)
{
    const f32 scale = (max.x - min.x) / static_cast<f32>(sampleCount);
    const f32 right = sead::Mathf::max(min.x + cMarkMinWidth, ImFloor(min.x + sead::Mathf::min(region.end, sampleCount) * scale));
    const f32 left = sead::Mathf::min(ImFloor(min.x + sead::Mathf::min(region.start, sampleCount) * scale), right - cMarkMinWidth);

    draw->AddRectFilled(ImVec2(left, min.y), ImVec2(right, max.y), color, cMarkRounding);
}

void DrawStreamRegionMarks(ImDrawList* draw, const ImVec2& min, const ImVec2& max, const WaveFile* wave, s32 onlyNo)
{
    const std::vector<StreamRegion>& regions = wave->getStreamFileLayout().regions;
    const u32 sampleCount = wave->getSampleCount();
    const s32 regionCount = static_cast<s32>(regions.size());

    if (sampleCount == 0 || max.x - min.x < cMarkMinWidth)
        return;

    s32 playingNo = -1;
    s32 nextNo = -1;
    GetPlayingStreamRegions(wave, &playingNo, &nextNo);

    for (s32 regionNo = 0; regionNo < regionCount; regionNo++)
    {
        if (onlyNo != -1 && regionNo != onlyNo)
            continue;

        if (regionNo == playingNo || regionNo == nextNo)
            continue;

        ImVec4 color = GetSeriesColor(regionNo);
        color.w *= cMarkIdleAlpha;

        DrawRegionMark(draw, min, max, regions[regionNo], sampleCount, ImGui::GetColorU32(color));
    }

    if (nextNo >= 0 && nextNo < regionCount && (onlyNo == -1 || nextNo == onlyNo))
        DrawRegionMark(draw, min, max, regions[nextNo], sampleCount, ImGui::GetColorU32(ImGuiCol_Text));

    if (playingNo >= 0 && playingNo < regionCount && (onlyNo == -1 || playingNo == onlyNo))
        DrawRegionMark(draw, min, max, regions[playingNo], sampleCount, ImGui::GetColorU32(GetSeriesColor(playingNo)));
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
