#include <ui/project/Sidebar.h>

#include <ui/browser/FilesPanel.h>
#include <ui/file/DocumentMgr.h>
#include <ui/project/ProjectViews.h>
#include <ui/Messages.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_custom.h>
#include <imgui/imgui_internal.h>

#include <prim/seadSafeString.h>

static constexpr f32 cSwitchGapEm    = 0.25f;
static constexpr f32 cSwitchHoverMix = 0.5f;

static constexpr const char* cSidebarWindowId = "###ProjectWindow";

enum class SidebarView
{
    Browser,
    Project,
};

struct SidebarViewInfo
{
    const char* icon;
    const char* label;
};

static const SidebarViewInfo cSidebarViews[] = {
    { ICON_LC_FOLDER_SEARCH, messages::common::cBrowser },
    { ICON_LC_PACKAGE,       messages::common::cProject },
};

static SidebarView sView = SidebarView::Browser;
static bool sIsSwitchInStrip = false;

static const SidebarViewInfo& GetViewInfo(SidebarView view)
{
    return cSidebarViews[static_cast<s32>(view)];
}

static void DrawSwitchTab(SidebarView view, const ImRect& bb)
{
    const SidebarViewInfo& info = GetViewInfo(view);
    const bool isSelected = sView == view;

    ImGui::PushID(static_cast<s32>(view));
    ImGui::SetCursorScreenPos(bb.Min);

    if (ImGui::InvisibleButton("##View", bb.GetSize(), ImGuiButtonFlags_PressedOnClick))
        sView = view;

    const bool isHovered = ImGui::IsItemHovered();
    const ImVec4& tabColor = ImGui::GetStyleColorVec4(ImGuiCol_Tab);
    const ImVec4& selectedColor = ImGui::GetStyleColorVec4(ImGuiCol_TabUnfocusedActive);
    const ImU32 idleColor = ImGui::GetColorU32(isSelected ? selectedColor : tabColor);
    const ImU32 hoverColor = ImGui::GetColorU32(isSelected ? selectedColor : ImLerp(tabColor, selectedColor, cSwitchHoverMix));
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    ImGui::TabItemBackground(drawList, bb, ImGuiTabItemFlags_None, ImGui::GetHoverColorU32(ImGui::GetItemID(), isHovered, idleColor, hoverColor));

    const sead::FormatFixedSafeString<64> label("%s %s", info.icon, info.label);
    const f32 padX = ImGui::GetStyle().FramePadding.x;
    const f32 textX = ImMax(bb.Min.x + padX, bb.Min.x + ImFloor((bb.GetWidth() - ImGui::CalcTextSize(label.cstr()).x) * 0.5f));
    const f32 textY = bb.Min.y + ImFloor((bb.GetHeight() - ImGui::GetFontSize()) * 0.5f);
    const f32 right = bb.Max.x - padX;

    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(isSelected || isHovered ? ImGuiCol_Text : ImGuiCol_TextDisabled));
    ImGui::RenderTextEllipsis(drawList, ImVec2(textX, textY), ImVec2(right, textY + ImGui::GetFontSize()), right, right, label.cstr(), nullptr, nullptr);
    ImGui::PopStyleColor();

    ImGui::PopID();
}

static bool HasProjectView()
{
    return !GetOpenDocuments().empty();
}

static void DrawSwitch(const ImRect& bb, f32 lineLeft, f32 lineRight)
{
    const f32 gap = ImFloor(ImGui::GetFontSize() * cSwitchGapEm);
    const f32 half = ImFloor((bb.GetWidth() - gap) * 0.5f);
    const f32 lineTop = bb.Max.y - ImGui::GetStyle().TabBarBorderSize;

    if (half < 1.0f)
        return;

    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(lineLeft, lineTop), ImVec2(lineRight, bb.Max.y), ImGui::GetColorU32(ImGuiCol_TabUnfocusedActive));

    DrawSwitchTab(SidebarView::Browser, ImRect(bb.Min, ImVec2(bb.Min.x + half, bb.Max.y)));

    if (HasProjectView())
        DrawSwitchTab(SidebarView::Project, ImRect(ImVec2(bb.Max.x - half, bb.Min.y), bb.Max));
}

static void DrawSwitchInWindow()
{
    const ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const f32 height = ImGui::GetFrameHeight();

    DrawSwitch(ImRect(pos, ImVec2(pos.x + ImGui::GetContentRegionAvail().x, pos.y + height)), window->Pos.x, window->Pos.x + window->Size.x);

    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy(ImVec2(0.0f, height));
}

bool FindSidebarStripColumn(ImRect* outColumn)
{
    ImGuiWindow* window = ImGui::FindWindowByName(cSidebarWindowId);

    if (!window || !window->WasActive || !window->DockIsActive || !window->DockNode)
        return false;

    ImGuiDockNode* node = window->DockNode;
    const ImGuiDockNode* root = ImGui::DockNodeGetRootNode(node);

    if (!root->IsDockSpace() || node->Pos.x > root->Pos.x + 0.5f || node->Pos.y > root->Pos.y + 0.5f)
        return false;

    *outColumn = node->Rect();
    return true;
}

void DrawSidebarSwitchInStrip(const ImRect& column, f32 top, f32 bottom)
{
    const f32 padX = ImGui::GetStyle().WindowPadding.x;

    DrawSwitch(ImRect(column.Min.x + padX, top, column.Max.x - padX, bottom), column.Min.x, column.Max.x);

    sIsSwitchInStrip = true;
}

void ShowSidebarProject()
{
    sView = SidebarView::Project;
}

void DrawSidebarUI()
{
    if (TakeFilesPanelRequests())
    {
        sView = SidebarView::Browser;
        ImGui::SetNextWindowFocus();
    }

    if (!HasProjectView())
        sView = SidebarView::Browser;

    const SidebarViewInfo& info = GetViewInfo(sView);
    const sead::FormatFixedSafeString<64> title("%s %s%s", info.icon, info.label, cSidebarWindowId);

    if (ImGui::Begin(title.cstr()))
    {
        if (!sIsSwitchInStrip)
            DrawSwitchInWindow();

        if (sView == SidebarView::Project)
            DrawProjectList();
        else
            DrawFilesPanel();
    }

    ImGui::End();

    sIsSwitchInStrip = false;
}
