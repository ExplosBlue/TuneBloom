#pragma once

#include <basis/seadTypes.h>
#include <math/seadMathCalcCommon.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

inline constexpr f32 cSoundPanelPadXEm  = 0.5f;
inline constexpr f32 cSoundPanelPadYEm  = 0.3f;
inline constexpr f32 cSoundSlotLabelGap = 2.0f;

template <typename DrawFn>
inline void DrawSoundPanels(const char* id, const f32* widths, s32 panelNum, DrawFn draw, bool stretch = true)
{
    const f32 em = ImGui::GetFontSize();
    const f32 avail = ImGui::GetContentRegionAvail().x;
    const ImVec2 padding(ImFloor(em * cSoundPanelPadXEm), ImFloor(em * cSoundPanelPadYEm));
    const f32 cellExtra = padding.x * 2.0f + 1.0f;

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, padding);

    for (s32 begin = 0; begin < panelNum;)
    {
        s32 end = begin;
        f32 lineWidth = 0.0f;

        while (end < panelNum && (end == begin || lineWidth + widths[end] + cellExtra <= avail))
        {
            lineWidth += widths[end] + cellExtra;
            end++;
        }

        ImGui::PushID(begin);

        const bool alone = !stretch || end - begin == 1;
        const f32 extra = sead::Mathf::max(0.0f, (avail - lineWidth) / (end - begin));
        const ImGuiTableFlags flags = ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV | (alone ? ImGuiTableFlags_NoHostExtendX | ImGuiTableFlags_SizingFixedFit : 0);

        if (ImGui::BeginTable(id, end - begin, flags))
        {
            for (s32 i = begin; i < end; i++)
                ImGui::TableSetupColumn(nullptr, alone ? ImGuiTableColumnFlags_WidthFixed : ImGuiTableColumnFlags_WidthStretch, alone ? sead::Mathf::min(widths[i], avail - cellExtra) : widths[i] + extra);

            for (s32 i = begin; i < end; i++)
            {
                ImGui::TableNextColumn();
                draw(i, sead::Mathf::max(0.0f, ImFloor((ImGui::GetContentRegionAvail().x - widths[i]) * 0.5f)));
            }

            ImGui::EndTable();
        }

        ImGui::PopID();

        begin = end;
    }

    ImGui::PopStyleVar();
}

inline f32 CalcSoundSlotHeight()
{
    return ImGui::GetFrameHeight() + cSoundSlotLabelGap + ImGui::GetFontSize();
}

template <typename DrawFn>
inline void DrawSoundSlot(const char* label, f32 slotWidth, const ImVec2& controlSize, f32 slotHeight, DrawFn draw)
{
    const f32 em = ImGui::GetFontSize();
    const f32 labelWidth = ImGui::CalcTextSize(label).x;
    const f32 width = slotWidth > 0.0f ? sead::Mathf::max(slotWidth, controlSize.x) : sead::Mathf::max(controlSize.x, labelWidth);

    ImGui::BeginGroup();

    const ImVec2 origin = ImGui::GetCursorPos();

    ImGui::SetCursorPos(ImVec2(origin.x + ImFloor((width - controlSize.x) * 0.5f), origin.y + ImFloor((slotHeight - em - controlSize.y) * 0.5f)));
    draw();

    ImGui::SetCursorPos(ImVec2(origin.x + ImFloor((width - labelWidth) * 0.5f), origin.y + slotHeight - em));
    ImGui::TextUnformatted(label);

    ImGui::SetCursorPos(origin);
    ImGui::Dummy(ImVec2(width, slotHeight));

    ImGui::EndGroup();
}
