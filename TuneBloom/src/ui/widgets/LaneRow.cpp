#include <ui/widgets/LaneRow.h>

#include <ui/wave/LoopWaveformInternal.h>
#include <ui/widgets/ContextMenu.h>
#include <ui/widgets/Widgets.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_custom.h>
#include <imgui/imgui_internal.h>

#include <math/seadMathCalcCommon.h>

static constexpr f32 cLaneRowHoveredAlpha = 0.05f;
static constexpr f32 cLanePadYEm          = 0.15f;
static constexpr f32 cLaneMenuHoverEm     = 1.3f;

namespace
{

struct LaneMenuButton
{
    ImGuiID menuId = 0;
    ImVec2 min;
    ImVec2 max;
};

}

static LaneMenuButton sLaneMenuButton;

static void SetLaneRowTint(bool lit, bool hovered)
{
    const f32 hoverRatio = ImGui::GetHoverRatio(ImGui::GetID("###RowFade"), hovered && !lit);

    if (lit)
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32(ImGuiCol_CheckMark, uicolors::Pick(cLaneRowSelectedAlpha)));
    else if (hoverRatio > 0.0f)
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32(ImGuiCol_Text, cLaneRowHoveredAlpha * hoverRatio));
}

bool DrawLaneRowSelectable(const char* id, bool selected, bool playing, f32 height, bool* outHovered)
{
    ImGui::SetNextItemAllowOverlap();
    const bool pressed = DrawBareRowSelectable(id, selected, ImVec2(0.0f, height));

    *outHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlappedByItem);
    SetLaneRowTint(selected || playing, *outHovered);

    return pressed;
}

void DrawLaneIndexBadge(s32 index, const ImVec2& cellMin, const ImVec2& cellSize, bool selected)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const sead::FormatFixedSafeString<16> number("%d", index);
    const ImVec2 textSize = ImGui::CalcTextSize(number.cstr());
    const f32 badgeHeight = ImGui::GetFrameHeight();
    const ImVec2 badgeSize(sead::Mathf::max(badgeHeight, textSize.x + style.FramePadding.x), badgeHeight);
    const ImVec2 badgeMin = ImFloor(cellMin + (cellSize - badgeSize) * 0.5f);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    if (selected)
        draw->AddRectFilled(badgeMin, badgeMin + badgeSize, ImGui::GetColorU32(ImGuiCol_CheckMark, uicolors::Pick(cLaneRowIndexAlpha)), style.FrameRounding);

    const ImVec2 textPos(ImFloor(badgeMin.x + (badgeSize.x - textSize.x) * 0.5f), badgeMin.y + style.FramePadding.y);

    draw->AddText(textPos, ImGui::GetColorU32(ImGuiCol_Text), number.cstr());
}

f32 CalcLaneIndexWidth(const char* widest)
{
    return ImCeil(sead::Mathf::max(ImGui::GetFrameHeight(), ImGui::CalcTextSize(widest).x + ImGui::GetStyle().FramePadding.x));
}

ImVec2 CalcLanePad()
{
    return ImVec2(ImGui::GetStyle().FrameRounding, ImFloor(ImGui::GetFontSize() * cLanePadYEm));
}

void DrawLaneFill(ImDrawList* draw, const ImVec2& min, const ImVec2& max, bool selected)
{
    const f32 rounding = ImGui::GetStyle().FrameRounding;

    draw->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_FrameBg), rounding);

    if (selected)
        draw->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_CheckMark, uicolors::Pick(cLaneRowSelectedAlpha)), rounding);
}

void DrawLaneRuler(u32 sampleCount, u32 sampleRate)
{
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight());
    const f32 padX = CalcLanePad().x;

    if (sampleCount != 0)
        ImGui::DrawTimeRuler(ImGui::GetWindowDrawList(), ImVec2(min.x + padX, min.y), ImVec2(min.x + size.x - padX, min.y + size.y), sampleCount, sampleRate);

    ImGui::Dummy(size);
}

LaneSeekRuler DrawLaneSeekRuler(ImGui::LoopWaveformState& view, u32 sampleCount, u32 sampleRate, f32 playSample, bool showLoop, u32 loopStart, u32 loopEnd)
{
    const ImVec2 cell = ImGui::GetCursorScreenPos();
    const f32 padX = CalcLanePad().x;
    const ImVec2 origin(cell.x + padX, cell.y);
    const ImVec2 size(sead::Mathf::max(1.0f, ImGui::GetContentRegionAvail().x - padX * 2.0f), ImGui::GetFrameHeight());

    LaneSeekRuler ruler;
    ruler.laneLeft = origin.x;
    ruler.laneWidth = size.x;

    ImGui::SetCursorScreenPos(origin);
    ImGui::DrawSeekRuler("##LaneRuler", origin, size.x, size.y, view, sampleRate, playSample, showLoop, loopStart, loopEnd, &ruler.seekSample, ImGui::SeekRulerForm::Header);
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelX);

    const ImGui::WaveformCanvas canvas = ImGui::ReadCanvasInput(origin, size, sampleCount);

    ImGui::HandleViewWheel(view, canvas);

    if (canvas.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
        ImGui::StartViewPan(view);

    if (playSample < 0.0f)
        ruler.seekSample = -1.0;

    return ruler;
}

f32 CalcLaneZoomBarHeight()
{
    return ImGui::MinimapHeight() + ImGui::GetStyle().ItemSpacing.y;
}

void DrawLaneZoomBar(ImGui::LoopWaveformState& view, f32 laneLeft, f32 laneWidth, u32 sampleCount)
{
    if (sampleCount == 0)
        return;

    if (view.dragging == ImGui::LoopWaveformState::Dragging::Pan)
    {
        ImGui::WaveformCanvas canvas;
        canvas.width = laneWidth;
        canvas.sampleCount = sampleCount;

        ImGui::DragViewPan(view, canvas);
    }

    ImGui::SetCursorScreenPos(ImVec2(laneLeft, ImGui::GetCursorScreenPos().y));
    ImGui::DrawMinimap(view, laneWidth, sampleCount);
}

f32 CalcLaneViewRatio(const ImGui::LoopWaveformState& view, f32 sample)
{
    if (sample < 0.0f)
        return -1.0f;

    return (sample - static_cast<f32>(view.viewStart)) / static_cast<f32>(ImGui::ViewSpan(view));
}

void DrawLaneRowPlayhead(f32 laneLeft, f32 laneRight, f32 rowHeight, f32 playRatio)
{
    const ImGuiTable* table = ImGui::GetCurrentTable();
    const f32 rowTop = table->RowPosY1;
    const f32 rowBottom = rowTop + rowHeight + table->RowCellPaddingY * 2.0f + 1.0f;

    ImGui::DrawOverviewPlayhead(ImGui::GetWindowDrawList(), ImVec2(laneLeft, rowTop), ImVec2(laneRight, rowBottom), playRatio);
}

void DrawLaneRowMenuButton(const char* popupId, bool rowHovered, const char* tooltip)
{
    const ImGuiID menuId = ImGui::GetID(popupId);
    const bool shown = rowHovered || ImGui::IsPopupOpen(popupId);
    const f32 size = ImGui::GetFrameHeight();

    if (DrawRowHoverButton("###RowMenuButton", ICON_LC_CHEVRON_DOWN, shown, tooltip, ImVec2(size, size), ImFloor(ImGui::GetFontSize() * cLaneMenuHoverEm)))
    {
        ImGui::OpenPopup(popupId);
        sLaneMenuButton.menuId = menuId;
    }

    if (sLaneMenuButton.menuId == menuId)
    {
        sLaneMenuButton.min = ImGui::GetItemRectMin();
        sLaneMenuButton.max = ImGui::GetItemRectMax();
    }
}

void PlaceLaneRowMenu(const char* popupId)
{
    if (sLaneMenuButton.menuId != ImGui::GetID(popupId))
        return;

    if (!ImGui::IsPopupOpen(popupId) || ImGui::IsMouseReleased(ImGuiMouseButton_Right))
    {
        sLaneMenuButton.menuId = 0;
        return;
    }

    PlaceContextMenuBelow(popupId, sLaneMenuButton.min, sLaneMenuButton.max, true);
}
