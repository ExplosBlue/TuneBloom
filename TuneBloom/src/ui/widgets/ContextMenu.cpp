#include <ui/widgets/ContextMenu.h>

#include <ui/items/ItemColumns.h>
#include <ui/app/MenuBar.h>
#include <ui/Messages.h>
#include <ui/settings/UiColors.h>
#include <ui/widgets/Widgets.h>
#include <ui/widgets/WindowAnimation.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_internal.h>

static constexpr s32    cMenuStyleColorCount = 2;

static bool sMenuSeparatorPending = false;
static u32 sMenuItemCount = 0;

static void PushMenuWindowStyle()
{
    const ImVec4 background = uicolors::Opaque(ImGui::GetStyleColorVec4(ImGuiCol_PopupBg));

    ImGui::PushStyleColor(ImGuiCol_PopupBg, background);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, ImFloor(ImGui::GetFontSize() * cMenuRoundingEm));
}

static bool OpenMenu(bool open)
{
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    if (!open)
        return false;

    const ImVec4 hoverColor = ImGui::GetStyleColorVec4(ImGuiCol_Header);
    const ImVec4 heldColor = ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive);

    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, hoverColor);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, heldColor);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, ImFloor(ImGui::GetFontSize() * cMenuItemGapEm)));

    sMenuSeparatorPending = false;
    sMenuItemCount = 0;

    return true;
}

bool BeginContextMenu(const char* id)
{
    PushMenuWindowStyle();
    return OpenMenu(ImGui::BeginPopup(id));
}

bool BeginItemContextMenu(const char* id)
{
    PushMenuWindowStyle();
    return OpenMenu(ImGui::BeginPopupContextItem(id));
}

bool BeginWindowContextMenu(const char* id)
{
    PushMenuWindowStyle();
    return OpenMenu(ImGui::BeginPopupContextWindow(id, ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems));
}

bool BeginTableContextMenu()
{
    ImGuiTable* table = ImGui::GetCurrentTable();
    SEAD_ASSERT(table);

    table->DisableDefaultContextMenu = true;

    PushMenuWindowStyle();
    return OpenMenu(ImGui::TableBeginContextMenuPopup(table));
}

void EndContextMenu()
{
    if (sMenuItemCount == 0)
        ImGui::CloseCurrentPopup();
    else
        AnimateWindow(WindowAnimation::Menu);

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(cMenuStyleColorCount);
    ImGui::EndPopup();
}

static bool DrawMenuRow(const char* icon, const char* label, bool checked, bool enabled)
{
    if (sMenuSeparatorPending)
        ImGui::Separator();

    sMenuSeparatorPending = false;
    sMenuItemCount++;

    ImDrawList* drawList = BeginMenuHighlight();
    const bool pressed = ImGui::MenuItemEx(label, icon, nullptr, checked, enabled);
    EndMenuHighlight(drawList, false);

    return pressed;
}

bool ContextMenuItem(const char* icon, const char* label, bool enabled, const char* disabledReason)
{
    const bool pressed = DrawMenuRow(icon, label, false, enabled);

    if (!enabled && disabledReason)
        SetDisabledTooltip(disabledReason);

    return pressed;
}

bool ContextMenuDangerItem(const char* icon, const char* label)
{
    ImGui::PushStyleColor(ImGuiCol_Text, uicolors::Marker());
    const bool pressed = DrawMenuRow(icon, label, false, true);
    ImGui::PopStyleColor();

    return pressed;
}

bool ContextMenuCheckItem(const char* label, bool checked, bool enabled)
{
    return DrawMenuRow(nullptr, label, checked, enabled);
}

void ContextMenuSeparator()
{
    sMenuSeparatorPending = sMenuItemCount > 0;
}

bool BeginContextSubMenu(const char* icon, const char* label)
{
    if (sMenuSeparatorPending)
        ImGui::Separator();

    sMenuSeparatorPending = false;
    sMenuItemCount++;

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const f32 right = window->Pos.x + ImGui::GetWindowContentRegionMax().x;
    const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);

    PushMenuWindowStyle();

    ImDrawList* drawList = BeginMenuHighlight();
    ImGui::PushStyleColor(ImGuiCol_Text, uicolors::cTransparent);
    const bool open = ImGui::BeginMenuEx(label, icon);
    ImGui::PopStyleColor();
    EndMenuHighlight(drawList, open);

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    drawList->AddText(ImVec2(pos.x + window->DC.MenuColumns.OffsetIcon, pos.y), textColor, icon);
    drawList->AddText(ImVec2(pos.x + window->DC.MenuColumns.OffsetLabel, pos.y), textColor, label);
    drawList->AddText(ImVec2(right - ImGui::CalcTextSize(ICON_LC_CHEVRON_RIGHT).x, pos.y), ImGui::GetColorU32(GetItemValueColor()), ICON_LC_CHEVRON_RIGHT);

    return open;
}

void EndContextSubMenu()
{
    ImGui::EndMenu();
}

void DrawTableContextMenu()
{
    ImGuiTable* table = ImGui::GetCurrentTable();

    if (!BeginTableContextMenu())
        return;

    const s32 columnIndex = (table->ContextPopupColumn >= 0 && table->ContextPopupColumn < table->ColumnsCount) ? table->ContextPopupColumn : -1;

    if (table->Flags & ImGuiTableFlags_Resizable)
    {
        if (columnIndex != -1)
        {
            const ImGuiTableColumn& column = table->Columns[columnIndex];
            const bool canResize = !(column.Flags & ImGuiTableColumnFlags_NoResize) && column.IsEnabled;

            if (ContextMenuItem(ICON_LC_MOVE_HORIZONTAL, messages::menu::cSizeColumnToFit, canResize))
                ImGui::TableSetColumnWidthAutoSingle(table, columnIndex);
        }

        if (ContextMenuItem(ICON_LC_COLUMNS_3, messages::menu::cSizeAllColumnsToFit))
            ImGui::TableSetColumnWidthAutoAll(table);
    }

    if (table->Flags & ImGuiTableFlags_Hideable)
    {
        ContextMenuSeparator();

        ImGui::PushItemFlag(ImGuiItemFlags_SelectableDontClosePopup, true);

        for (s32 i = 0; i < table->ColumnsCount; i++)
        {
            ImGuiTableColumn& column = table->Columns[i];

            if (column.Flags & (ImGuiTableColumnFlags_Disabled | ImGuiTableColumnFlags_NoHeaderLabel))
                continue;

            const char* name = ImGui::TableGetColumnName(table, i);
            const bool isLastVisible = column.IsUserEnabled && table->ColumnsEnabledCount <= 1;
            const bool canHide = !(column.Flags & ImGuiTableColumnFlags_NoHide) && !isLastVisible;

            ImGui::PushID(i);

            if (ContextMenuCheckItem(name, column.IsUserEnabled, canHide))
                column.IsUserEnabledNextFrame = !column.IsUserEnabled;

            ImGui::PopID();
        }

        ImGui::PopItemFlag();
    }

    EndContextMenu();
}
