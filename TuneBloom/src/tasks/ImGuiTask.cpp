#include "tasks/ImGuiTask.h"

#include "imgui/imgui.h"
#include "imgui/imgui_custom.h"
#include "imgui/imgui_impl_opengl3.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_internal.h"

#include <framework/glfw/seadGameFrameworkGlfwGL.h>
#include <framework/seadProcessMeter.h>
#include <heap/seadExpHeap.h>

#include <core/TestHooks.h>
#include <platform/OwnedWindow.h>
#include <platform/SingleInstance.h>
#include <platform/WindowFrame.h>

#include <ui/browser/FilesPanel.h>
#include <ui/file/File.h>
#include <ui/items/ItemColumns.h>
#include <ui/player/OutputMeter.h>
#include <ui/player/PlayerFrame.h>
#include <ui/sequence/SeqManual.h>
#include <ui/settings/ColorUtil.h>
#include <ui/settings/Theme.h>
#include <ui/app/UI.h>
#include <ui/widgets/AppFonts.h>
#include <ui/widgets/WindowAnimation.h>
#include <ui/widgets/Widgets.h>

SEAD_TASK_SINGLETON_DISPOSER_IMPL(ImGuiTask);

static void SetupImGuiStyle();

static void* ImGuiAllocFunc(size_t size, void* userPtr)
{
    sead::Heap* heap = static_cast<sead::Heap*>(userPtr);

    return heap->alloc(size);
}

static void ImGuiFreeFunc(void* ptr, void* userPtr)
{
    sead::Heap* heap = static_cast<sead::Heap*>(userPtr);

    heap->free(ptr);
}

static void (*sShowPlatformWindow)(ImGuiViewport*) = nullptr;
static void (*sDestroyPlatformWindow)(ImGuiViewport*) = nullptr;

static bool IsModalViewport(ImGuiViewport* viewport)
{
    const ImGuiWindow* window = static_cast<ImGuiViewportP*>(viewport)->Window;

    return window && (window->Flags & ImGuiWindowFlags_Modal);
}

static ImU32 CalcBorderColor(ImGuiCol background)
{
    const ImVec4* colors = ImGui::GetStyle().Colors;
    const ImVec4& border = colors[ImGuiCol_Border];

    return ImGui::ColorConvertFloat4ToU32(ImLerp(uicolors::Opaque(colors[background]), uicolors::Opaque(border), border.w));
}

static ImGuiCol GetViewportBackground(ImGuiViewport* viewport)
{
    const ImGuiWindow* window = static_cast<ImGuiViewportP*>(viewport)->Window;

    return window && (window->Flags & (ImGuiWindowFlags_Popup | ImGuiWindowFlags_Tooltip)) ? ImGuiCol_PopupBg : ImGuiCol_WindowBg;
}

static void RoundPlatformWindow(ImGuiViewport* viewport)
{
    if (!(viewport->Flags & ImGuiViewportFlags_NoDecoration))
        return;

    windowframe::SetRoundCorners(static_cast<GLFWwindow*>(viewport->PlatformHandle), CalcBorderColor(GetViewportBackground(viewport)));
}

static void UpdatePlatformWindowBorders()
{
    static ImU32 sPopupBorder = 0;
    static ImU32 sWindowBorder = 0;

    const ImU32 popupBorder = CalcBorderColor(ImGuiCol_PopupBg);
    const ImU32 windowBorder = CalcBorderColor(ImGuiCol_WindowBg);

    if (popupBorder == sPopupBorder && windowBorder == sWindowBorder)
        return;

    sPopupBorder = popupBorder;
    sWindowBorder = windowBorder;

    for (ImGuiViewport* viewport : ImGui::GetPlatformIO().Viewports)
    {
        if (static_cast<ImGuiViewportP*>(viewport)->PlatformWindowCreated)
            RoundPlatformWindow(viewport);
    }
}

static bool IsPopupViewport(ImGuiViewport* viewport)
{
    const ImGuiWindow* window = static_cast<ImGuiViewportP*>(viewport)->Window;

    return window && (window->Flags & (ImGuiWindowFlags_Popup | ImGuiWindowFlags_ChildMenu | ImGuiWindowFlags_Tooltip));
}

static void ShowPlatformWindow(ImGuiViewport* viewport)
{
    RoundPlatformWindow(viewport);

    if (IsModalViewport(viewport))
        ownedwindow::prepare(viewport->PlatformHandleRaw);

    sShowPlatformWindow(viewport);

    if (IsPopupViewport(viewport))
        ownedwindow::attach(viewport->PlatformHandleRaw, ImGui::GetMainViewport()->PlatformHandleRaw);
}

static void DestroyPlatformWindow(ImGuiViewport* viewport)
{
    const bool isFading = ImGuiTask::instance()->isDrawing() && FadeOutPlatformWindow(viewport);

    if (!isFading)
        ownedwindow::detach(viewport->PlatformHandleRaw);

    sDestroyPlatformWindow(viewport);
    static_cast<ImGuiViewportP*>(viewport)->LastAlpha = 1.0f;
}

static void HookPlatformWindows()
{
    ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();

    sShowPlatformWindow = platformIo.Platform_ShowWindow;
    sDestroyPlatformWindow = platformIo.Platform_DestroyWindow;
    platformIo.Platform_ShowWindow = &ShowPlatformWindow;
    platformIo.Platform_DestroyWindow = &DestroyPlatformWindow;
}

static void UpdateWindowFrameTheme()
{
    static GLFWwindow* sWindow = nullptr;
    static bool sIsDark = false;

    GLFWwindow* window = static_cast<GLFWwindow*>(ImGui::GetMainViewport()->PlatformHandle);
    const bool isDark = IsDarkTheme();

    if (window == sWindow && isDark == sIsDark)
        return;

    sWindow = window;
    sIsDark = isDark;
    windowframe::SetDark(window, isDark);
}

static bool UpdateFontAtlas()
{
    ImGuiIO& io = ImGui::GetIO();
    const bool hasTexture = io.Fonts->TexID != nullptr;

    if (!BuildAppFonts(io.DisplayFramebufferScale.x))
        return false;

    if (hasTexture)
    {
        ImGui_ImplOpenGL3_DestroyFontsTexture();
        ImGui_ImplOpenGL3_CreateFontsTexture();
    }

    return true;
}

ImGuiTask::ImGuiTask(const sead::TaskConstructArg& arg)
    : sead::Task(arg, "ImGuiTask")
    , mCalcMeter("ImGuiTask::calc", sead::Color4f::cCyan)
    , mDrawMeter("ImGuiTask::draw", sead::Color4f::cBlue)
{
}

void ImGuiTask::prepare()
{
    {
        sead::ExpHeap* heap = sead::ExpHeap::create(90 * 1024 * 1024, "ImGui::Heap", nullptr);
        ImGui::SetAllocatorFunctions(&ImGuiAllocFunc, &ImGuiFreeFunc, heap);
    }

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    RegisterItemColumnTexts();
    RegisterItemColumnSettings();
    RegisterPlaybackSettings();
    RegisterPropertySettings();
    RegisterSeqEditorSettings();
    RegisterOutputMeterSettings();
    RegisterLayoutSettings();
    RegisterRecentFilesSettings();

    if (testhooks::IsEnabled())
        testhooks::LoadSettings();

    InitWindowAnimation();

    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;       // Enable Docking

#if !defined(SEAD_PLATFORM_LINUX)
    if (!testhooks::IsEnabled() || testhooks::IsViewportsOn())
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
#endif

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    SetupImGuiStyle();
    InitUIScale();

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowMenuButtonPosition = ImGuiDir_Right;

    LoadAppFontFiles();

    sead::GameFrameworkGlfwGL* fw = sead::DynamicCast<sead::GameFrameworkGlfwGL>(getFramework());
    SEAD_ASSERT(fw);

    sead::Graphics::instance()->lockDrawContext();
    {
        sead::CurrentHeapSetter chs(fw->getGLHeap());

        // Setup Platform/Renderer backends
        ImGui_ImplGlfw_InitForOpenGL(fw->getWindowHandle(), true);
        ImGui_ImplOpenGL3_Init();
        HookPlatformWindows();
    }
    sead::Graphics::instance()->unlockDrawContext();

    if (!testhooks::IsEnabled() || testhooks::IsSingleInstanceOn())
        singleinstance::StartListening(fw->getWindowHandle(), &TakeSystemFile);

    adjustHeapAll();
}

void ImGuiTask::enter()
{
    if (sead::ProcessMeter::instance())
    {
        sead::ProcessMeter::instance()->attachProcessMeterBar(&mCalcMeter);
        sead::ProcessMeter::instance()->attachProcessMeterBar(&mDrawMeter);
    }
}

void ImGuiTask::exit()
{
    sead::GameFrameworkGlfwGL* fw = sead::DynamicCast<sead::GameFrameworkGlfwGL>(getFramework());
    SEAD_ASSERT(fw);

    sead::CurrentHeapSetter chs(fw->getGLHeap());

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void ImGuiTask::calc()
{
    sead::GameFrameworkGlfwGL* fw = sead::DynamicCast<sead::GameFrameworkGlfwGL>(getFramework());
    SEAD_ASSERT(fw);

    sead::CurrentHeapSetter chs(fw->getGLHeap());

    mCalcMeter.measureBegin();

    ImGui_ImplGlfw_NewFrame();

    const bool isNewFontAtlas = UpdateFontAtlas();
    ImGui_ImplOpenGL3_NewFrame();

    if (isNewFontAtlas)
        ImGui::GetIO().Fonts->ClearTexData();

    testhooks::ApplyInputOverrides();
    ImGui::NewFrame();

    ApplyPlayerFrameStart();
    UpdateWindowFrameTheme();
    UpdatePlatformWindowBorders();
    testhooks::Update();

    mCalcMeter.measureEnd();
}

void ImGuiTask::draw()
{
    sead::GameFrameworkGlfwGL* fw = sead::DynamicCast<sead::GameFrameworkGlfwGL>(getFramework());
    SEAD_ASSERT(fw);

    sead::CurrentHeapSetter chs(fw->getGLHeap());

    mDrawMeter.measureBegin();
    mIsDrawing = true;

    ImGui::Render();
    testhooks::Capture(ImGui::GetDrawData());
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // Update and Render additional Platform Windows
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        GLFWwindow* prevContext = glfwGetCurrentContext();

        GLint prevFBO;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        DrawPlatformWindowFades();

        // Restore the OpenGL rendering context to the main window DC, since platform windows might have changed it.
        glfwMakeContextCurrent(prevContext);

        glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    }

    mIsDrawing = false;
    mDrawMeter.measureEnd();
}

static void SetupImGuiStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 10.0f;
    style.ChildRounding = 8.0f;
    style.PopupRounding = 8.0f;
    style.FrameRounding = 4.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 4.0f;
    style.DisabledAlpha = 0.45f;

    MigrateLegacyConfig();
    LoadThemeConfig();
    LoadStartupConfig();
    LoadRecentFiles();
    LoadFilesPanelConfig();
    LoadArchiveConfig();
    LoadBackupConfig();
    LoadInterfaceConfig();
    ApplyInterfaceConfig();
    ApplyTheme();
    RunStartupAction();
}
