#include <ui/app/TitleBar.h>

#include <ui/items/ItemColumns.h>
#include <ui/settings/UiColors.h>

#include <platform/WindowFrame.h>
#include <imgui/imgui_custom.h>
#include <imgui/imgui_internal.h>

#include <gfx/gl/seadTextureGL.h>

#include <app/Utilll.h>

#include <GLFW/glfw3.h>

static constexpr f32 cWindowButtonWidthEm = 2.3f;
static constexpr f32 cWindowGlyphEm       = 0.5f;

using windowframe::WindowButton;

struct WindowButtonsLayout
{
    f32 left = 0.0f;
    f32 width = 0.0f;
    bool canMaximize = false;
};

static WindowButtonsLayout sButtons;

static GLFWwindow* GetMainWindow()
{
    return static_cast<GLFWwindow*>(ImGui::GetMainViewport()->PlatformHandle);
}

static bool IsMainWindowFocused()
{
    GLFWwindow* window = GetMainWindow();

    return window && glfwGetWindowAttrib(window, GLFW_FOCUSED);
}

static ImVec4 GetTitleBarInkColor()
{
    return IsMainWindowFocused() ? GetItemValueColor() : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
}

bool HasCustomTitleBar()
{
    return windowframe::IsCustom();
}

f32 CalcTitleBarHeight(f32 minHeight)
{
    return ImMax(minHeight, windowframe::FitTitleBarHeight(minHeight));
}

static f32 DrawTitleBarIcon(f32 left, f32 top, f32 bottom)
{
    sead::TextureGL* texture = util::getIcon() ? sead::DynamicCast<sead::TextureGL>(util::getIcon()) : nullptr;
    const f32 slot = bottom - top;
    const f32 size = GImGui->FontBaseSize;

    if (!texture)
        return left + slot;

    const ImVec2 min = ImFloor(ImVec2(left, top) + ImVec2(slot - size, slot - size) * 0.5f);
    ImGui::GetWindowDrawList()->AddImage(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(texture->getID())), min, min + ImVec2(size, size));

    return left + slot;
}

static void DrawWindowGlyph(WindowButton button, bool isMaximized, ImVec2 center, ImU32 color)
{
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const f32 em = GImGui->FontBaseSize;
    const f32 thickness = ImMax(1.0f, ImFloor(em / 20.0f));
    const f32 half = ImFloor(em * cWindowGlyphEm * 0.5f);
    const ImVec2 c = ImFloor(center);

    switch (button)
    {
        case WindowButton::Minimize:
            drawList->AddLine(ImVec2(c.x - half, c.y), ImVec2(c.x + half, c.y), color, thickness);
            break;

        case WindowButton::Maximize:
            if (!isMaximized)
            {
                drawList->AddRect(c - ImVec2(half, half), c + ImVec2(half, half), color, thickness, 0, thickness);
                break;
            }

            {
                const f32 shift = ImFloor(half * 0.4f);
                const ImVec2 frontMin(c.x - half, c.y - half + shift);
                const ImVec2 frontMax(c.x + half - shift, c.y + half);

                drawList->AddRect(frontMin, frontMax, color, thickness, 0, thickness);
                drawList->AddLine(ImVec2(frontMin.x + shift, frontMin.y), ImVec2(frontMin.x + shift, c.y - half), color, thickness);
                drawList->AddLine(ImVec2(frontMin.x + shift, c.y - half), ImVec2(c.x + half, c.y - half), color, thickness);
                drawList->AddLine(ImVec2(c.x + half, c.y - half), ImVec2(c.x + half, frontMax.y - shift), color, thickness);
                drawList->AddLine(ImVec2(c.x + half, frontMax.y - shift), ImVec2(frontMax.x, frontMax.y - shift), color, thickness);
            }
            break;

        case WindowButton::Close:
        {
            const f32 reach = half + 0.5f;

            drawList->AddLine(c - ImVec2(reach, reach), c + ImVec2(reach, reach), color, thickness);
            drawList->AddLine(ImVec2(c.x - reach, c.y + reach), ImVec2(c.x + reach, c.y - reach), color, thickness);
            break;
        }
    }
}

static ImU32 CalcButtonFill(ImGuiID id, WindowButton button, bool hovered, bool held)
{
    const bool isClose = button == WindowButton::Close;
    const ImVec4 hoverFill = isClose ? uicolors::cWindowClose : ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered);
    ImVec4 fill = held && !isClose ? ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive) : hoverFill;

    if (held && isClose)
        fill.w *= 0.8f;

    ImVec4 idle = fill;
    idle.w = 0.0f;

    return ImGui::GetHoverColorU32(id, hovered || held, ImGui::GetColorU32(idle), ImGui::GetColorU32(fill));
}

static ImU32 CalcGlyphColor(ImGuiID id, WindowButton button, bool hovered, bool held, bool enabled)
{
    if (!enabled)
        return ImGui::GetColorU32(ImGuiCol_TextDisabled);

    const ImVec4 lit = button == WindowButton::Close ? uicolors::cInkOnDark : ImGui::GetStyleColorVec4(ImGuiCol_Text);

    return ImGui::GetColorU32(ImLerp(GetTitleBarInkColor(), lit, ImGui::GetHoverRatio(id + 1, hovered || held)));
}

static bool IsButtonEnabled(WindowButton button)
{
    return button != WindowButton::Maximize || sButtons.canMaximize;
}

static f32 GetButtonLeft(WindowButton button)
{
    return sButtons.left + sButtons.width * static_cast<f32>(button);
}

static void DrawWindowButton(WindowButton button, f32 top, f32 bottom, bool isMaximized)
{
    static constexpr const char* cIds[windowframe::cWindowButtonNum] = { "##WindowMinimize", "##WindowMaximize", "##WindowClose" };

    const ImGuiID id = ImGui::GetID(cIds[static_cast<s32>(button)]);
    const bool enabled = IsButtonEnabled(button);
    const windowframe::ButtonState state = enabled ? windowframe::GetButtonState(button) : windowframe::ButtonState::Idle;
    const bool hovered = state != windowframe::ButtonState::Idle;
    const bool held = state == windowframe::ButtonState::Held;
    const ImVec2 min(GetButtonLeft(button), top);
    const ImVec2 max(min.x + sButtons.width, bottom);

    if (enabled)
        ImGui::GetWindowDrawList()->AddRectFilled(min, max, CalcButtonFill(id, button, hovered, held));

    DrawWindowGlyph(button, isMaximized, (min + max) * 0.5f, CalcGlyphColor(id, button, hovered, held, enabled));
}

static f32 DrawWindowButtons(f32 right, f32 top, f32 bottom)
{
    GLFWwindow* window = GetMainWindow();
    const bool isMaximized = windowframe::IsWindowMaximized(window);

    sButtons.width = ImFloor(GImGui->FontBaseSize * cWindowButtonWidthEm);
    sButtons.left = right - sButtons.width * windowframe::cWindowButtonNum;
    sButtons.canMaximize = windowframe::CanMaximize(window);

    ImGui::PushClipRect(ImVec2(sButtons.left, top), ImVec2(right, bottom), false);

    for (s32 i = 0; i < windowframe::cWindowButtonNum; i++)
        DrawWindowButton(static_cast<WindowButton>(i), top, bottom, isMaximized);

    ImGui::PopClipRect();

    return sButtons.left;
}

f32 GetTitleBarContentLeft(f32 left)
{
    return ImMax(left + ImGui::GetStyle().WindowPadding.x, left + windowframe::GetTitleBarInsetLeft());
}

f32 DrawTitleBarLeft(f32 left, f32 top, f32 bottom)
{
    if (windowframe::HasDrawnButtons())
        return DrawTitleBarIcon(left, top, bottom);

    const f32 itemInset = ImFloor(ImGui::GetStyle().ItemSpacing.x * 0.5f);

    return ImMax(ImGui::GetCursorScreenPos().x, left + windowframe::GetTitleBarInsetLeft() + itemInset);
}

f32 DrawTitleBarRight(f32 right, f32 top, f32 bottom)
{
    if (windowframe::HasDrawnButtons())
        return DrawWindowButtons(right, top, bottom);

    sButtons = WindowButtonsLayout();
    sButtons.left = right;

    return right;
}

static windowframe::Span MakeSpan(f32 left, f32 right, f32 origin)
{
    windowframe::Span span;
    span.left = static_cast<s32>(ImFloor(left - origin));
    span.right = static_cast<s32>(ImCeil(right - origin));

    return span;
}

static void AddWindowButtonSpans(windowframe::CaptionArea* area, f32 origin)
{
    for (s32 i = 0; i < windowframe::cWindowButtonNum; i++)
    {
        const WindowButton button = static_cast<WindowButton>(i);
        const f32 left = GetButtonLeft(button);
        const windowframe::Span span = MakeSpan(left, left + sButtons.width, origin);

        if (IsButtonEnabled(button))
            area->buttons[i] = span;
        else
            area->clientSpans[area->clientSpanNum++] = span;
    }
}

void FinishTitleBar(f32 menuLeft, f32 menuRight, f32 toolsLeft, f32 bottom)
{
    const ImVec2 origin = ImGui::GetMainViewport()->Pos;

    windowframe::CaptionArea area;
    area.height = static_cast<s32>(ImCeil(bottom - origin.y));
    area.clientSpans[area.clientSpanNum++] = MakeSpan(menuLeft, menuRight, origin.x);
    area.clientSpans[area.clientSpanNum++] = MakeSpan(toolsLeft, sButtons.left, origin.x);

    if (windowframe::HasDrawnButtons())
        AddWindowButtonSpans(&area, origin.x);

    windowframe::SetCaptionArea(area);
}
