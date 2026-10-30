#include <ui/widgets/RangeCell.h>

#include <messages/Messages.h>
#include <ui/settings/UiColors.h>
#include <ui/wave/WaveFormatUI.h>
#include <ui/widgets/Knob.h>

#include <imgui/imgui_internal.h>

#include <math/seadMathCalcCommon.h>

static constexpr f32 cTrackLine     = 2.0f;
static constexpr f32 cTrackAlpha    = 0.35f;
static constexpr f32 cMarkMinWidth  = 3.0f;
static constexpr f32 cMarkRounding  = 1.0f;
static constexpr f32 cFieldPadY     = 2.0f;
static constexpr f32 cFieldDragSpan = 2000.0f;

RangeCell BeginRangeCell(f32 blockHeight)
{
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const f32 right = cursor.x + ImGui::GetContentRegionAvail().x;
    const f32 centerY = ImFloor(cursor.y + ImGui::GetFrameHeight() * 0.5f);

    RangeCell cell;
    cell.draw = ImGui::GetWindowDrawList();
    cell.min = ImVec2(cursor.x, centerY - blockHeight * 0.5f);
    cell.max = ImVec2(right, centerY + blockHeight * 0.5f);

    cell.draw->AddRectFilled(ImVec2(cursor.x, centerY - cTrackLine * 0.5f), ImVec2(right, centerY + cTrackLine * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled, cTrackAlpha), cTrackLine * 0.5f);

    return cell;
}

void DrawRangeMark(ImDrawList* draw, const ImVec2& min, const ImVec2& max, u32 start, u32 end, u32 sampleCount, ImU32 color)
{
    if (sampleCount == 0 || max.x - min.x < cMarkMinWidth)
        return;

    const f32 scale = (max.x - min.x) / static_cast<f32>(sampleCount);
    const f32 right = sead::Mathf::max(min.x + cMarkMinWidth, ImFloor(min.x + sead::Mathf::min(end, sampleCount) * scale));
    const f32 left = sead::Mathf::min(ImFloor(min.x + sead::Mathf::min(start, sampleCount) * scale), right - cMarkMinWidth);

    draw->AddRectFilled(ImVec2(left, min.y), ImVec2(right, max.y), color, cMarkRounding);
}

bool DrawCellTimeField(const char* id, u32* value, u32 min, u32 max, u32 sampleCount, u32 sampleRate, bool disabled)
{
    const ImGuiStyle& style = ImGui::GetStyle();

    sead::FixedSafeString<24> time;
    FormatSampleTime(&time, *value, sampleRate);

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + style.FramePadding.y - cFieldPadY);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, cFieldPadY));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, uicolors::cTransparent);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::BeginDisabled(disabled);

    SetNextKnobText(time.cstr());

    const bool changed = DragField(id, ImGuiDataType_U32, value, sead::Mathf::max(1.0f, static_cast<f32>(sampleCount) / cFieldDragSpan), &min, &max);

    ImGui::EndDisabled();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    KnobTooltip(messages::wavefile::cSamplesFormat, *value);

    return changed;
}
