#include <ui/widgets/TabRowTools.h>

#include <ui/items/ItemColumns.h>

#include <libs/IconsLucide.h>

#include <math/seadMathCalcCommon.h>

f32 CalcTabRowToolsWidth(s32 toolNum)
{
    const ImGuiStyle& style = ImGui::GetStyle();

    return (ImGui::CalcTextSize(ICON_LC_PLUS).x + style.FramePadding.x * 2.0f) * toolNum + style.ItemInnerSpacing.x * (toolNum - 1);
}

bool BeginTabRowTools(ImGuiDockNode* node, f32 toolsWidth)
{
    if (!ImGui::DockNodeBeginAmendTabBar(node))
        return false;

    const ImGuiTabBar* tabBar = node->TabBar;
    const ImRect& barRect = tabBar->BarRect;
    const f32 tabsEnd = barRect.Min.x + tabBar->WidthAllTabs + ImGui::GetStyle().ItemSpacing.x;
    const f32 toolsX = sead::Mathf::max(tabsEnd, barRect.Max.x - toolsWidth);
    const bool isFocused = (tabBar->Flags & ImGuiTabBarFlags_IsFocused) != 0;

    ImGui::PushClipRect(barRect.Min, barRect.Max, true);
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(isFocused ? ImGuiCol_Tab : ImGuiCol_TabUnfocused));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_TabHovered));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_TabHovered));
    ImGui::SetCursorScreenPos(ImVec2(ImFloor(toolsX), barRect.Min.y));

    return true;
}

void EndTabRowTools()
{
    ImGui::PopStyleColor(3);
    ImGui::PopClipRect();
    ImGui::DockNodeEndAmendTabBar();
}

bool DrawTabRowTool(const char* label, bool inTabBar)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const bool pressed = ImGui::Button(label, ImVec2(0.0f, inTabBar ? ImGui::GetFrameHeight() - style.TabBarBorderSize : 0.0f));

    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);

    return pressed;
}

bool DrawTabRowCommand(const char* label, const char* tooltip, const char* disabledReason, bool inTabBar)
{
    ImGui::BeginDisabled(disabledReason != nullptr);
    const bool pressed = DrawTabRowTool(label, inTabBar);
    ImGui::EndDisabled();

    ImGui::SetItemTooltip("%s", disabledReason ? disabledReason : tooltip);

    return pressed;
}

static s32 CalcTabRowCountDigits(const TabRowTitle& title, s32 count)
{
    s32 digits = 1;

    for (s32 rest = count; rest >= 10; rest /= 10)
        digits++;

    return sead::Mathi::max(digits, title.countDigits);
}

void FormatTabRowTitle(sead::BufferedSafeString* name, const TabRowTitle& title, s32 count)
{
    const f32 slotWidth = ImGui::CalcTextSize("0").x * CalcTabRowCountDigits(title, count);
    const s32 spaceNum = static_cast<s32>(ImCeil(slotWidth / ImGui::CalcTextSize(" ").x));

    name->format(" %s %s %*s %s", title.icon, title.title, spaceNum, "", title.id);
}

void DrawTabRowCount(const TabRowTitle& title, s32 count)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImGuiDockNode* node = window->DockNode;

    if (count < 0 || !node || node->IsHiddenTabBar() || !ImGui::DockNodeBeginAmendTabBar(node))
        return;

    const ImRect& barRect = node->TabBar->BarRect;
    const ImRect tabRect = window->DockTabItemRect;
    const ImVec2& padding = node->TabBar->FramePadding;
    const sead::FormatFixedSafeString<64> lead(" %s %s ", title.icon, title.title);
    const sead::FormatFixedSafeString<16> text("%d", count);
    const ImVec2 pos(tabRect.Min.x + padding.x + ImGui::CalcTextSize(lead.cstr()).x, tabRect.Min.y + padding.y);

    ImGui::PushClipRect(barRect.Min, barRect.Max, true);
    ImGui::PushClipRect(tabRect.Min, ImVec2(tabRect.Max.x - padding.x, tabRect.Max.y), true);
    ImGui::GetWindowDrawList()->AddText(pos, ImGui::GetColorU32(GetItemValueColor()), text.cstr());
    ImGui::PopClipRect();
    ImGui::PopClipRect();

    ImGui::DockNodeEndAmendTabBar();
}

void SetTabRowDisabled(const char* disabledReason)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImGuiDockNode* node = window->DockNode;
    const ImRect tabRect = window->DockTabItemRect;

    window->DockTabDisabled = disabledReason != nullptr;

    if (!disabledReason || !node || node->IsHiddenTabBar() || node->VisibleWindow == window)
        return;

    if (!ImGui::DockNodeBeginAmendTabBar(node))
        return;

    ImGui::ItemAdd(tabRect, 0, nullptr, ImGuiItemFlags_NoNav);
    ImGui::SetItemTooltip("%s", disabledReason);
    ImGui::DockNodeEndAmendTabBar();
}
