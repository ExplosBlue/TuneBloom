#include <ui/widgets/WindowAnimation.h>

#include <core/TestHooks.h>

#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl3.h>
#include <imgui/imgui_internal.h>

#include <platform/OwnedWindow.h>

#include <GLFW/glfw3.h>

#include <cmath>

struct WindowAnimationInfo
{
    f32 openSeconds;
    f32 closeSeconds;
    f32 openScale;
    f32 closeScale;
    bool isAnchorAtMouse;
    bool isAnchorMoving;
    bool isOpenFromGhost;
    f32 easePower;
};

static constexpr WindowAnimationInfo cWindowAnimationInfos[] = {
    { 0.08f, 0.08f, 0.98f, 0.98f, true,  false, false, 3.0f }, // Menu
    { 0.12f, 0.10f, 0.98f, 0.98f, false, false, false, 3.0f }, // Dialog
    { 0.11f, 0.09f, 0.98f, 0.98f, true,  true,  true,  1.5f }, // Tooltip
};

static constexpr s32 cWindowSlotCount    = 8;
static constexpr s32 cGhostChunkVertices = 3 * 4096;

struct GhostCommand
{
    ImVec4 clipRect;
    ImTextureID textureId;
    s32 vertexCount;
};

struct WindowSlot
{
    ImGuiID windowId = 0;
    WindowAnimation kind = WindowAnimation::Menu;
    f64 openTime = 0.0;
    ImVec2 anchor;
    bool isFirstVisibleFrame = false;
    bool isPlatformWindow = false;

    ImVector<ImDrawVert> ghostVertices;
    ImVector<GhostCommand> ghostCommands;
    ImGuiID ghostViewportId = 0;
    s32 ghostFrame = -1;
    f64 ghostCloseTime = -1.0;
};

struct PendingWindow
{
    ImGuiWindow* window;
    WindowAnimation kind;
};

struct PlatformWindowFade
{
    GLFWwindow* window = nullptr;
    void* nativeWindow = nullptr;
    ImDrawList* drawList = nullptr;
    ImVec2 pos;
    ImVec2 size;
    ImVec2 framebufferScale;
    WindowAnimation kind = WindowAnimation::Menu;
    f64 closeTime = 0.0;
    f32 startAlpha = 1.0f;
};

static WindowSlot sWindowSlots[cWindowSlotCount];
static PlatformWindowFade sPlatformWindowFades[cWindowSlotCount];
static PendingWindow sPendingWindows[cWindowSlotCount];
static s32 sPendingWindowCount = 0;
static bool sAnimationEnabled = true;

static f32 EaseOut(f32 t, f32 power)
{
    return 1.0f - powf(1.0f - ImSaturate(t), power);
}

static void TransformVertex(ImDrawVert& vertex, const ImVec2& anchor, f32 scale, f32 alpha)
{
    vertex.pos.x = anchor.x + (vertex.pos.x - anchor.x) * scale;
    vertex.pos.y = anchor.y + (vertex.pos.y - anchor.y) * scale;

    const u32 vertexAlpha = (vertex.col & IM_COL32_A_MASK) >> IM_COL32_A_SHIFT;
    vertex.col = (vertex.col & ~IM_COL32_A_MASK) | (static_cast<u32>(vertexAlpha * alpha) << IM_COL32_A_SHIFT);
}

static ImVec4 TransformClipRect(const ImVec4& clipRect, const ImVec2& anchor, f32 scale)
{
    return ImVec4(
        anchor.x + (clipRect.x - anchor.x) * scale,
        anchor.y + (clipRect.y - anchor.y) * scale,
        anchor.x + (clipRect.z - anchor.x) * scale,
        anchor.y + (clipRect.w - anchor.y) * scale
    );
}

static bool IsWindowDrawn(const ImGuiWindow* window)
{
    return window->Active && !window->Hidden;
}

static bool HasOwnPlatformWindow(const ImGuiWindow* window)
{
    return window->ViewportOwned && window->Viewport != ImGui::GetMainViewport();
}

static void TransformWindow(ImGuiWindow* window, const ImVec2& anchor, f32 scale, f32 alpha)
{
    ImDrawList* drawList = window->DrawList;

    for (ImDrawVert& vertex : drawList->VtxBuffer)
        TransformVertex(vertex, anchor, scale, alpha);

    for (ImDrawCmd& command : drawList->CmdBuffer)
        command.ClipRect = TransformClipRect(command.ClipRect, anchor, scale);

    for (ImGuiWindow* child : window->DC.ChildWindows)
    {
        if (IsWindowDrawn(child))
            TransformWindow(child, anchor, scale, alpha);
    }
}

static void CaptureWindow(WindowSlot& slot, const ImGuiWindow* window)
{
    const ImDrawList* drawList = window->DrawList;

    for (const ImDrawCmd& command : drawList->CmdBuffer)
    {
        if (command.UserCallback || command.ElemCount == 0)
            continue;

        const s32 first = slot.ghostVertices.Size;
        slot.ghostVertices.resize(first + static_cast<s32>(command.ElemCount));

        for (u32 i = 0; i < command.ElemCount; i++)
            slot.ghostVertices[first + static_cast<s32>(i)] = drawList->VtxBuffer[command.VtxOffset + drawList->IdxBuffer[command.IdxOffset + i]];

        slot.ghostCommands.push_back({ command.ClipRect, command.TextureId, static_cast<s32>(command.ElemCount) });
    }

    for (const ImGuiWindow* child : window->DC.ChildWindows)
    {
        if (IsWindowDrawn(child))
            CaptureWindow(slot, child);
    }
}

static WindowSlot& GetWindowSlot(ImGuiID windowId)
{
    WindowSlot* oldest = &sWindowSlots[0];

    for (WindowSlot& slot : sWindowSlots)
    {
        if (slot.windowId == windowId)
            return slot;

        if (slot.ghostFrame < oldest->ghostFrame)
            oldest = &slot;
    }

    oldest->windowId = windowId;
    oldest->openTime = 0.0;
    oldest->isFirstVisibleFrame = false;
    oldest->ghostFrame = ImGui::GetFrameCount();
    oldest->ghostVertices.resize(0);
    oldest->ghostCommands.resize(0);

    return *oldest;
}

static void ApplyWindowAnimation(ImGuiWindow* window, WindowAnimation kind)
{
    const WindowAnimationInfo& info = cWindowAnimationInfos[static_cast<s32>(kind)];
    WindowSlot& slot = GetWindowSlot(window->ID);

    slot.kind = kind;

    if (window->Appearing)
        slot.isFirstVisibleFrame = true;

    if (window->Hidden)
        return;

    slot.isPlatformWindow = HasOwnPlatformWindow(window);

    const ImVec2 max(window->Pos.x + window->Size.x, window->Pos.y + window->Size.y);

    if (slot.isFirstVisibleFrame || info.isAnchorMoving)
        slot.anchor = ImClamp(ImGui::GetIO().MousePos, window->Pos, max);

    if (slot.isFirstVisibleFrame)
    {
        slot.isFirstVisibleFrame = false;
        slot.openTime = ImGui::GetTime();

        if (info.isOpenFromGhost && !slot.ghostCommands.empty() && slot.ghostCloseTime >= 0.0)
        {
            const f32 ghostAlpha = 1.0f - EaseOut(static_cast<f32>(ImGui::GetTime() - slot.ghostCloseTime) / info.closeSeconds, info.easePower);
            slot.openTime -= info.openSeconds * (1.0f - powf(1.0f - ImSaturate(ghostAlpha), 1.0f / info.easePower));
        }
    }

    if (!info.isAnchorAtMouse)
        slot.anchor = ImVec2((window->Pos.x + max.x) * 0.5f, (window->Pos.y + max.y) * 0.5f);

    const f32 t = EaseOut(static_cast<f32>(ImGui::GetTime() - slot.openTime) / info.openSeconds, info.easePower);

    if (slot.isPlatformWindow)
        window->Viewport->Alpha = t;
    else if (t < 1.0f)
        TransformWindow(window, slot.anchor, ImLerp(info.openScale, 1.0f, t), t);

    slot.ghostVertices.resize(0);
    slot.ghostCommands.resize(0);
    CaptureWindow(slot, window);

    slot.ghostViewportId = window->Viewport->ID;
    slot.ghostFrame = ImGui::GetFrameCount();
    slot.ghostCloseTime = -1.0;
}

static void AddGhostVertices(ImDrawList* drawList, const WindowSlot& slot, f32 scale, f32 alpha)
{
    const ImDrawVert* source = slot.ghostVertices.Data;

    for (const GhostCommand& command : slot.ghostCommands)
    {
        const ImVec4 clipRect = TransformClipRect(command.clipRect, slot.anchor, scale);

        drawList->PushClipRect(ImVec2(clipRect.x, clipRect.y), ImVec2(clipRect.z, clipRect.w));
        drawList->PushTextureID(command.textureId);

        for (s32 remaining = command.vertexCount; remaining > 0;)
        {
            const s32 count = ImMin(remaining, cGhostChunkVertices);

            drawList->PrimReserve(count, count);

            for (s32 i = 0; i < count; i++, source++)
            {
                ImDrawVert vertex = *source;
                TransformVertex(vertex, slot.anchor, scale, alpha);
                drawList->PrimVtx(vertex.pos, vertex.uv, vertex.col);
            }

            remaining -= count;
        }

        drawList->PopTextureID();
        drawList->PopClipRect();
    }
}

static void DrawWindowGhost(WindowSlot& slot)
{
    const WindowAnimationInfo& info = cWindowAnimationInfos[static_cast<s32>(slot.kind)];

    if (slot.ghostCloseTime < 0.0)
        slot.ghostCloseTime = ImGui::GetTime();

    const f32 t = EaseOut(static_cast<f32>(ImGui::GetTime() - slot.ghostCloseTime) / info.closeSeconds, info.easePower);
    ImGuiViewport* viewport = ImGui::FindViewportByID(slot.ghostViewportId);

    if (t >= 1.0f || !viewport)
    {
        slot.ghostVertices.clear();
        slot.ghostCommands.clear();
        return;
    }

    AddGhostVertices(ImGui::GetForegroundDrawList(viewport), slot, ImLerp(1.0f, info.closeScale, t), 1.0f - t);
}

static WindowSlot* FindWindowSlot(ImGuiID windowId)
{
    for (WindowSlot& slot : sWindowSlots)
    {
        if (slot.windowId == windowId)
            return &slot;
    }

    return nullptr;
}

static PlatformWindowFade* FindFreePlatformWindowFade()
{
    for (PlatformWindowFade& fade : sPlatformWindowFades)
    {
        if (!fade.window)
            return &fade;
    }

    return nullptr;
}

static ImDrawList* CreateGhostDrawList(const WindowSlot& slot)
{
    ImDrawList* drawList = IM_NEW(ImDrawList)(ImGui::GetDrawListSharedData());

    drawList->_ResetForNewFrame();
    AddGhostVertices(drawList, slot, 1.0f, 1.0f);

    return drawList;
}

static ImVec2 CalcFramebufferScale(GLFWwindow* window, const ImVec2& size)
{
    s32 width = 0;
    s32 height = 0;
    glfwGetFramebufferSize(window, &width, &height);

    if (size.x <= 0.0f || size.y <= 0.0f)
        return ImVec2(1.0f, 1.0f);

    return ImVec2(static_cast<f32>(width) / size.x, static_cast<f32>(height) / size.y);
}

static void ClearWindowCallbacks(GLFWwindow* window)
{
    glfwSetWindowFocusCallback(window, nullptr);
    glfwSetCursorEnterCallback(window, nullptr);
    glfwSetCursorPosCallback(window, nullptr);
    glfwSetMouseButtonCallback(window, nullptr);
    glfwSetScrollCallback(window, nullptr);
    glfwSetKeyCallback(window, nullptr);
    glfwSetCharCallback(window, nullptr);
    glfwSetWindowCloseCallback(window, nullptr);
    glfwSetWindowPosCallback(window, nullptr);
    glfwSetWindowSizeCallback(window, nullptr);
}

static void ClosePlatformWindowFade(PlatformWindowFade& fade)
{
    ownedwindow::detach(fade.nativeWindow);
    glfwDestroyWindow(fade.window);
    IM_DELETE(fade.drawList);

    fade = PlatformWindowFade();
}

static void RenderPlatformWindowFade(const PlatformWindowFade& fade)
{
    ImDrawData drawData;
    drawData.Valid = true;
    drawData.DisplayPos = fade.pos;
    drawData.DisplaySize = fade.size;
    drawData.FramebufferScale = fade.framebufferScale;
    drawData.AddDrawList(fade.drawList);

    glfwMakeContextCurrent(fade.window);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(&drawData);
    glfwSwapBuffers(fade.window);
}

static void AddPendingWindow(ImGuiWindow* window, WindowAnimation kind)
{
    for (s32 i = 0; i < sPendingWindowCount; i++)
    {
        if (sPendingWindows[i].window == window)
            return;
    }

    if (sPendingWindowCount == cWindowSlotCount)
        return;

    sPendingWindows[sPendingWindowCount++] = { window, kind };
}

static void ClearWindowAnimations(ImGuiContext* context, ImGuiContextHook* hook)
{
    sPendingWindowCount = 0;

    for (WindowSlot& slot : sWindowSlots)
    {
        slot.ghostVertices.clear();
        slot.ghostCommands.clear();
    }

    for (PlatformWindowFade& fade : sPlatformWindowFades)
    {
        if (fade.window)
            ClosePlatformWindowFade(fade);
    }
}

static void UpdateWindowAnimations(ImGuiContext* context, ImGuiContextHook* hook)
{
    if (!IsAnimationEnabled())
    {
        ClearWindowAnimations(context, hook);
        return;
    }

    for (ImGuiWindow* window : context->Windows)
    {
        if (window->Active && (window->Flags & ImGuiWindowFlags_Tooltip) && !(window->Flags & ImGuiWindowFlags_ChildWindow))
            AddPendingWindow(window, WindowAnimation::Tooltip);
    }

    for (s32 i = 0; i < sPendingWindowCount; i++)
        ApplyWindowAnimation(sPendingWindows[i].window, sPendingWindows[i].kind);

    sPendingWindowCount = 0;

    const s32 frame = ImGui::GetFrameCount();

    for (WindowSlot& slot : sWindowSlots)
    {
        if (!slot.isPlatformWindow && !slot.ghostCommands.empty() && slot.ghostFrame != frame)
            DrawWindowGhost(slot);
    }
}

static void AddWindowAnimationHooks()
{
    static bool sHooksAdded = false;

    if (sHooksAdded)
        return;

    ImGuiContextHook updateHook;
    updateHook.Type = ImGuiContextHookType_EndFramePre;
    updateHook.Callback = &UpdateWindowAnimations;
    ImGui::AddContextHook(GImGui, &updateHook);

    ImGuiContextHook clearHook;
    clearHook.Type = ImGuiContextHookType_Shutdown;
    clearHook.Callback = &ClearWindowAnimations;
    ImGui::AddContextHook(GImGui, &clearHook);

    sHooksAdded = true;
}

bool IsMotionAllowed()
{
    static const bool sAllowed = !testhooks::IsEnabled() || testhooks::IsMotionOn();

    return sAllowed;
}

void SetAnimationEnabled(bool enabled)
{
    sAnimationEnabled = enabled;
}

bool IsAnimationEnabled()
{
    return sAnimationEnabled && IsMotionAllowed();
}

void InitWindowAnimation()
{
    if (IsMotionAllowed())
        AddWindowAnimationHooks();
}

void AnimateWindow(WindowAnimation kind)
{
    if (!IsAnimationEnabled())
        return;

    AddWindowAnimationHooks();
    AddPendingWindow(ImGui::GetCurrentWindow(), kind);
}

bool FadeOutPlatformWindow(ImGuiViewport* viewport)
{
    if (!IsAnimationEnabled())
        return false;

    const ImGuiWindow* window = static_cast<ImGuiViewportP*>(viewport)->Window;
    WindowSlot* slot = window ? FindWindowSlot(window->ID) : nullptr;
    PlatformWindowFade* fade = FindFreePlatformWindowFade();

    if (!slot || !fade || !slot->isPlatformWindow || slot->ghostCommands.empty() || window->Viewport != viewport)
        return false;

    if (slot->ghostFrame < ImGui::GetFrameCount() - 1)
        return false;

    GLFWwindow* platformWindow = ImGui_ImplGlfw_KeepWindowOnDestroy(viewport);

    if (!platformWindow)
        return false;

    fade->window = platformWindow;
    fade->nativeWindow = viewport->PlatformHandleRaw;
    fade->drawList = CreateGhostDrawList(*slot);
    fade->pos = viewport->Pos;
    fade->size = viewport->Size;
    fade->framebufferScale = CalcFramebufferScale(platformWindow, viewport->Size);
    fade->kind = slot->kind;
    fade->closeTime = ImGui::GetTime();
    fade->startAlpha = static_cast<ImGuiViewportP*>(viewport)->LastAlpha;

    slot->ghostVertices.resize(0);
    slot->ghostCommands.resize(0);

    ClearWindowCallbacks(platformWindow);
    glfwSetWindowAttrib(platformWindow, GLFW_MOUSE_PASSTHROUGH, GLFW_TRUE);

    if (glfwGetWindowAttrib(platformWindow, GLFW_FOCUSED))
        glfwFocusWindow(static_cast<GLFWwindow*>(ImGui::GetMainViewport()->PlatformHandle));

    return true;
}

void DrawPlatformWindowFades()
{
    for (PlatformWindowFade& fade : sPlatformWindowFades)
    {
        if (!fade.window)
            continue;

        const WindowAnimationInfo& info = cWindowAnimationInfos[static_cast<s32>(fade.kind)];
        const f32 t = EaseOut(static_cast<f32>(ImGui::GetTime() - fade.closeTime) / info.closeSeconds, info.easePower);

        if (t >= 1.0f)
        {
            ClosePlatformWindowFade(fade);
            continue;
        }

        glfwSetWindowOpacity(fade.window, fade.startAlpha * (1.0f - t));
        RenderPlatformWindowFade(fade);
    }
}
