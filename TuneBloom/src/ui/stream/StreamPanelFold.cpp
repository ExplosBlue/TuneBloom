#include <ui/stream/StreamPanelFold.h>

#include <ui/widgets/DockNode.h>
#include <ui/Messages.h>
#include <ui/widgets/TabRowTools.h>

#include <libs/IconsLucide.h>

static constexpr f32 cTrackUnfoldHeightEm = 11.0f;

static bool sTrackFolded = false;
static f32 sTrackUnfoldedHeight = 0.0f;

bool IsTrackFolded()
{
    return sTrackFolded;
}

void SetTrackFolded(bool folded)
{
    sTrackFolded = folded;
}

f32 GetTrackUnfoldedHeight()
{
    return sTrackUnfoldedHeight;
}

void SetTrackUnfoldedHeight(f32 height)
{
    sTrackUnfoldedHeight = height;
}

static ImGuiDockNode* FindFoldNode()
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImGuiDockNode* node = window->DockNode;
    const ImGuiWindow* tracksWindow = ImGui::FindWindowByName("###SubInfoWindow");

    if (!node || node->VisibleWindow != window || !tracksWindow || tracksWindow->DockNode != node)
        return nullptr;

    return node->ParentNode && node->ParentNode->SplitAxis == ImGuiAxis_Y ? node : nullptr;
}

static bool IsFoldTabDoubleClicked(const ImGuiDockNode* node)
{
    const ImRect& tabRect = ImGui::GetCurrentWindow()->DockTabItemRect;

    if (GImGui->HoveredWindow != node->HostWindow || !ImGui::IsMouseHoveringRect(tabRect.Min, tabRect.Max, false))
        return false;

    return ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
}

bool CanFoldStreamPanel()
{
    return FindFoldNode() != nullptr;
}

bool DrawStreamPanelFoldTool()
{
    const bool pressed = DrawTabRowTool(sTrackFolded ? ICON_LC_CHEVRON_UP "###TrackFold" : ICON_LC_CHEVRON_DOWN "###TrackFold", true);

    ImGui::SetItemTooltip("%s", sTrackFolded ? messages::common::cRestore : messages::stream::cTrackFold);

    return pressed;
}

bool UpdateStreamPanelFold(bool toggleFold)
{
    ImGuiDockNode* node = FindFoldNode();

    if (!node)
        return false;

    const f32 tabRowHeight = ImGui::GetFrameHeight();
    const bool drawFolded = sTrackFolded;

    toggleFold |= IsFoldTabDoubleClicked(node);

    if (toggleFold)
    {
        if (!sTrackFolded)
            sTrackUnfoldedHeight = node->Size.y;

        sTrackFolded = !sTrackFolded;
        ImGui::MarkIniSettingsDirty();
    }

    if (sTrackFolded)
    {
        SetDockNodeHeight(node, tabRowHeight);
        node->LocalFlags |= ImGuiDockNodeFlags_NoResize;

        return drawFolded;
    }

    if (toggleFold)
        SetDockNodeHeight(node, sTrackUnfoldedHeight > tabRowHeight ? sTrackUnfoldedHeight : ImFloor(ImGui::GetFontSize() * cTrackUnfoldHeightEm));

    node->LocalFlags &= ~ImGuiDockNodeFlags_NoResize;

    return drawFolded;
}
