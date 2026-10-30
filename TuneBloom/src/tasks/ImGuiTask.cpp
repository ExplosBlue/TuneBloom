#include "tasks/ImGuiTask.h"

#include "imgui/imgui.h"
#include "imgui/imgui_custom.h"
#include "imgui/imgui_impl_opengl3.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_internal.h"

#include <filedevice/seadFileDeviceMgr.h>
#include <framework/glfw/seadGameFrameworkGlfwGL.h>
#include <framework/seadProcessMeter.h>
#include <heap/seadExpHeap.h>

#include <core/TestHooks.h>
#include <platform/OwnedWindow.h>

#include <ui/widgets/Dialogs.h>
#include <ui/browser/FilesPanel.h>
#include <ui/items/ItemColumns.h>
#include <ui/player/OutputMeter.h>
#include <ui/player/PlayerFrame.h>
#include <ui/sequence/SeqManual.h>
#include <ui/settings/Theme.h>
#include <ui/app/UI.h>
#include <ui/widgets/WindowAnimation.h>

#include "libs/IconsLucide.h"

SEAD_TASK_SINGLETON_DISPOSER_IMPL(ImGuiTask);

static void SetupImGuiStyle();

static constexpr f32 cIconFontSizes[] = { 20.0f, 16.0f, 22.0f };
static constexpr f32 cIconSizeEm      = 0.9f;
static constexpr f32 cIconOffsetEm    = 0.2f;

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

static void ShowPlatformWindow(ImGuiViewport* viewport)
{
    const bool isModal = IsModalViewport(viewport);

    if (isModal)
        ownedwindow::prepare(viewport->PlatformHandleRaw);

    sShowPlatformWindow(viewport);

    if (isModal)
        ownedwindow::attach(viewport->PlatformHandleRaw, ImGui::GetMainViewport()->PlatformHandleRaw);
}

static void DestroyPlatformWindow(ImGuiViewport* viewport)
{
    ownedwindow::detach(viewport->PlatformHandleRaw);
    sDestroyPlatformWindow(viewport);
}

static void HookPlatformWindows()
{
    ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();

    sShowPlatformWindow = platformIo.Platform_ShowWindow;
    sDestroyPlatformWindow = platformIo.Platform_DestroyWindow;
    platformIo.Platform_ShowWindow = &ShowPlatformWindow;
    platformIo.Platform_DestroyWindow = &DestroyPlatformWindow;
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

    {
        sead::FileDevice::LoadArg arg;
        arg.path = "main://fonts/SpaceMono-Regular.ttf";

        u8* font = sead::FileDeviceMgr::instance()->load(arg);

        ImFontConfig config;
        config.FontDataOwnedByAtlas = false;

        io.Fonts->AddFontFromMemoryTTF(font, arg.read_size, ScaleSize(32.0f), &config);

        sead::FileDevice::LoadArg iconsArg;
        iconsArg.path = "main://fonts/" FONT_ICON_FILE_NAME_LC;

        u8* iconsFont = sead::FileDeviceMgr::instance()->load(iconsArg);

        static const ImWchar iconsRanges[] = { ICON_MIN_LC, ICON_MAX_LC, 0 };

        for (f32 baseSize : cIconFontSizes)
        {
            const f32 fontSize = ScaleSize(baseSize);

            ImFont* sizedFont = io.Fonts->AddFontFromMemoryTTF(font, arg.read_size, fontSize, &config);

            if (!io.FontDefault)
                io.FontDefault = sizedFont;

            ImFontConfig iconsConfig;
            iconsConfig.FontDataOwnedByAtlas = false;
            iconsConfig.MergeMode = true;
            iconsConfig.PixelSnapH = true;
            iconsConfig.OversampleH = 1;
            iconsConfig.GlyphOffset.y = static_cast<f32>(static_cast<s32>(fontSize * cIconOffsetEm + 0.5f));

            io.Fonts->AddFontFromMemoryTTF(iconsFont, iconsArg.read_size, fontSize * cIconSizeEm, &iconsConfig, iconsRanges);
        }

        ImFontConfig dialogIconConfig;
        dialogIconConfig.FontDataOwnedByAtlas = false;
        dialogIconConfig.PixelSnapH = true;
        dialogIconConfig.OversampleH = 1;

        io.Fonts->AddFontFromMemoryTTF(iconsFont, iconsArg.read_size, ScaleSize(cDialogIconFontSize), &dialogIconConfig, GetDialogIconRanges());
    }

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

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    testhooks::ApplyInputOverrides();
    ImGui::NewFrame();

    ApplyPlayerFrameStart();
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
