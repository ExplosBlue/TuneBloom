#include <ui/file/LoadProgress.h>

#include <ui/Messages.h>
#include <core/TestHooks.h>
#include <ui/widgets/WindowAnimation.h>

#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl3.h>
#include <imgui/imgui_internal.h>
#include <libs/IconsLucide.h>

#include <framework/glfw/seadGameFrameworkGlfwGL.h>
#include <gfx/seadFrameBuffer.h>
#include <heap/seadHeapMgr.h>
#include <prim/seadSafeString.h>

#include <app/Utilll.h>

namespace loadprogress
{

struct StageInfo
{
    const char* label;
    f32 stepCost;
};

static constexpr s32 cStageNum = static_cast<s32>(Stage::Num);

static constexpr StageInfo cStageInfos[cStageNum] = {
    { messages::archive::cOpenStageFile,          60.0f  }, // ReadFile
    { messages::project::cRowGroups,        0.01f  }, // Groups
    { messages::project::cRowSequenceFiles, 0.15f  }, // SequenceFiles
    { messages::project::cRowWaveFiles,     1.5f   }, // WaveFiles
    { messages::project::cRowBankFiles,     0.01f  }, // BankFiles
    { messages::project::cRowSounds,        3.5f   }, // Sounds
    { messages::project::cRowSoundSets,     0.01f  }, // SoundSets
    { messages::archive::cOpenStageGroupFiles,    0.5f   }, // GroupFiles
    { messages::archive::cOpenStageNames,         5.0f   }, // Names
};

static constexpr f64 cShowDelay     = 0.15;
static constexpr f64 cFrameInterval = 1.0 / 30.0;
static constexpr f32 cDimFadeTime   = 0.12f;
static constexpr f32 cWidthEm       = 24.0f;
static constexpr f32 cPadEm         = 0.8f;
static constexpr f32 cBarHeightEm   = 0.35f;
static constexpr f32 cRowGapEm      = 0.45f;

static bool sActive = false;
static bool sPolling = false;
static sead::FixedSafeString<512> sFileName;

static Stage sStage = Stage::ReadFile;
static u32 sStageCounts[cStageNum] = {};
static u32 sStageSteps[cStageNum] = {};
static f64 sStageTimes[cStageNum] = {};
static f64 sStageBeginTime = 0.0;
static f32 sFraction = 0.0f;

static f64 sBeginTime = 0.0;
static f64 sShowTime = -1.0;
static f64 sLastFrameTime = 0.0;
static s32 sFrameCount = 0;
static f64 sFrameSeconds = 0.0;
static f64 sFrameSecondsMax = 0.0;

static GLuint sBackdropTexture = 0;

static f32 CalcFraction()
{
    f32 total = 0.0f;
    f32 done = 0.0f;

    for (s32 i = 0; i < cStageNum; i++)
    {
        const f32 cost = cStageInfos[i].stepCost;

        total += cost * sStageCounts[i];

        if (i < static_cast<s32>(sStage))
            done += cost * sStageCounts[i];
        else if (i == static_cast<s32>(sStage))
            done += cost * ImMin(sStageSteps[i], sStageCounts[i]);
    }

    if (total <= 0.0f)
        return 0.0f;

    return ImSaturate(done / total);
}

void DrawRows(const char* icon, const char* title, const char* stageText, f32 fraction)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 em = ImGui::GetFontSize();
    const f32 width = ImGui::GetContentRegionAvail().x;
    const sead::FormatFixedSafeString<32> percent(messages::archive::cOpenPercentFormat, static_cast<s32>(fraction * 100.0f));

    // Title
    {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 iconSize = ImGui::CalcTextSize(icon);
        const ImVec2 textSize = ImGui::CalcTextSize(title);
        const f32 textX = pos.x + iconSize.x + style.ItemInnerSpacing.x;
        const ImVec2 textMax(pos.x + width, pos.y + textSize.y);

        ImGui::GetWindowDrawList()->AddText(pos, ImGui::GetColorU32(ImGuiCol_Text), icon);
        ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), ImVec2(textX, pos.y), textMax, textMax.x, textMax.x, title, nullptr, &textSize);
        ImGui::Dummy(ImVec2(width, textSize.y));
    }

    ImGui::Dummy(ImVec2(0.0f, ImFloor(em * cRowGapEm) - style.ItemSpacing.y));

    // Bar
    {
        const f32 height = ImMax(ImFloor(em * cBarHeightEm), 4.0f);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 max(pos.x + width, pos.y + height);
        const f32 fillX = pos.x + ImFloor(width * ImSaturate(fraction));
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        drawList->AddRectFilled(pos, max, ImGui::GetColorU32(ImGuiCol_FrameBg), height * 0.5f);

        if (fillX > pos.x + height)
            drawList->AddRectFilled(pos, ImVec2(fillX, max.y), ImGui::GetColorU32(ImGuiCol_PlotHistogram), height * 0.5f);

        ImGui::Dummy(ImVec2(width, height));
    }

    ImGui::Dummy(ImVec2(0.0f, ImFloor(em * cRowGapEm) - style.ItemSpacing.y));

    // Stage and percent
    {
        const f32 percentWidth = ImGui::CalcTextSize(percent.cstr()).x;

        ImGui::TextDisabled("%s", stageText);
        ImGui::SameLine(ImGui::GetCursorPosX() + width - percentWidth);
        ImGui::TextDisabled("%s", percent.cstr());
    }
}

static void DrawDialog(const char* fileName, Stage stage, f32 fraction, f32 dimAlpha)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 em = ImGui::GetFontSize();
    const f32 pad = ImFloor(em * cPadEm);
    const ImVec2 viewportMax(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y);

    ImDrawList* background = ImGui::GetBackgroundDrawList(ImGui::GetMainViewport());

    if (sBackdropTexture != 0)
        background->AddImage(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(sBackdropTexture)), viewport->Pos, viewportMax, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));

    ImVec4 dim = ImGui::GetStyleColorVec4(ImGuiCol_ModalWindowDimBg);
    dim.w *= dimAlpha;
    background->AddRectFilled(viewport->Pos, viewportMax, ImGui::ColorConvertFloat4ToU32(dim));

    ImVec4 windowColor = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    windowColor.w = 1.0f;

    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(ImFloor(em * cWidthEm), 0.0f));

    ImGui::PushStyleColor(ImGuiCol_WindowBg, windowColor);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, style.PopupRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, style.PopupBorderSize);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;

    if (ImGui::Begin("###LoadProgress", nullptr, flags))
    {
        AnimateWindow(WindowAnimation::Dialog);

        DrawRows(ICON_LC_FOLDER_OPEN, sead::FormatFixedSafeString<576>(messages::archive::cOpenProgressFormat, fileName).cstr(), cStageInfos[static_cast<s32>(stage)].label, fraction);
    }
    ImGui::End();

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
}

static void CaptureBackdrop(sead::GameFrameworkGlfwGL* fw)
{
    const sead::FrameBuffer* frameBuffer = fw->getDefaultFrameBuffer();

    if (!frameBuffer)
        return;

    s32 width = 0;
    s32 height = 0;
    glfwGetFramebufferSize(fw->getWindowHandle(), &width, &height);

    if (width <= 0 || height <= 0)
        return;

    GLint prevFrameBuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFrameBuffer);

    frameBuffer->bind(nullptr);

    glGenTextures(1, &sBackdropTexture);
    glBindTexture(GL_TEXTURE_2D, sBackdropTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 0, 0, width, height, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    glFinish();

    glBindFramebuffer(GL_FRAMEBUFFER, prevFrameBuffer);
}

static void PresentFrame(sead::GameFrameworkGlfwGL* fw)
{
    GLint prevFrameBuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFrameBuffer);

    GLint frameBuffer = 0;

    if (fw->getDefaultFrameBuffer())
    {
        fw->getDefaultFrameBuffer()->bind(nullptr);
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &frameBuffer);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // Copy to the window
    if (frameBuffer != 0)
    {
        s32 width = 0;
        s32 height = 0;
        glfwGetFramebufferSize(fw->getWindowHandle(), &width, &height);

        const GLboolean isScissor = glIsEnabled(GL_SCISSOR_TEST);
        glDisable(GL_SCISSOR_TEST);

        glBindFramebuffer(GL_READ_FRAMEBUFFER, frameBuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);

        if (isScissor)
            glEnable(GL_SCISSOR_TEST);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, prevFrameBuffer);

    glfwSwapBuffers(fw->getWindowHandle());
}

static void DrawFrame(f64 now)
{
    sead::GameFrameworkGlfwGL* fw = sead::DynamicCast<sead::GameFrameworkGlfwGL>(util::getFramework());

    if (!fw)
        return;

    sead::CurrentHeapSetter chs(fw->getGLHeap());

    if (sShowTime < 0.0)
    {
        sShowTime = now;
        CaptureBackdrop(fw);
    }

    sFraction = ImMax(sFraction, CalcFraction());
    sFrameCount++;

    DrawDialog(sFileName.cstr(), sStage, sFraction, ImSaturate(static_cast<f32>(now - sShowTime) / cDimFadeTime));

    ImGui::Render();
    testhooks::CaptureLoadFrame(ImGui::GetDrawData(), sFrameCount);

    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        GLFWwindow* prevContext = glfwGetCurrentContext();

        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();

        glfwMakeContextCurrent(prevContext);
    }

    PresentFrame(fw);

    sPolling = true;
    glfwPollEvents();
    sPolling = false;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void Begin(const char* fileName)
{
    sActive = true;
    sFileName = fileName;

    sStage = Stage::ReadFile;
    sFraction = 0.0f;

    for (s32 i = 0; i < cStageNum; i++)
    {
        sStageCounts[i] = 0;
        sStageSteps[i] = 0;
        sStageTimes[i] = 0.0;
    }

    sStageCounts[static_cast<s32>(Stage::ReadFile)] = 1;
    sStageCounts[static_cast<s32>(Stage::Names)] = 1;

    sBeginTime = glfwGetTime();
    sStageBeginTime = sBeginTime;
    sShowTime = -1.0;
    sLastFrameTime = 0.0;
    sFrameCount = 0;
    sFrameSeconds = 0.0;
    sFrameSecondsMax = 0.0;
}

void End()
{
    if (!sActive)
        return;

    const f64 now = glfwGetTime();

    sStageTimes[static_cast<s32>(sStage)] += now - sStageBeginTime;

    if (testhooks::IsEnabled())
    {
        for (s32 i = 0; i < cStageNum; i++)
            testhooks::ReportLoadStage(cStageInfos[i].label, sStageSteps[i], sStageCounts[i], sStageTimes[i]);

        testhooks::ReportLoadTotal(now - sBeginTime, sFrameCount, sFrameSeconds, sFrameSecondsMax);
    }

    if (sBackdropTexture != 0)
    {
        glDeleteTextures(1, &sBackdropTexture);
        sBackdropTexture = 0;
    }

    sActive = false;
}

void Repaint()
{
    if (!sPolling)
        return;

    sead::GameFrameworkGlfwGL* fw = sead::DynamicCast<sead::GameFrameworkGlfwGL>(util::getFramework());

    if (!fw)
        return;

    sead::CurrentHeapSetter chs(fw->getGLHeap());

    PresentFrame(fw);
}

void SetStageCount(Stage stage, u32 count)
{
    sStageCounts[static_cast<s32>(stage)] = count;
}

void BeginStage(Stage stage)
{
    if (!sActive)
        return;

    const f64 now = glfwGetTime();

    sStageTimes[static_cast<s32>(sStage)] += now - sStageBeginTime;
    sStageBeginTime = now;
    sStage = stage;

    Update();
}

void Step()
{
    if (!sActive)
        return;

    sStageSteps[static_cast<s32>(sStage)]++;

    Update();
}

void Update()
{
    if (!sActive)
        return;

    const f64 now = glfwGetTime();

    if (now - sBeginTime < cShowDelay || now - sLastFrameTime < cFrameInterval)
        return;

    sLastFrameTime = now;

    DrawFrame(now);

    const f64 frameSeconds = glfwGetTime() - now;

    sFrameSeconds += frameSeconds;
    sFrameSecondsMax = ImMax(sFrameSecondsMax, frameSeconds);
}

void DrawPreview(const char* fileName, Stage stage, f32 fraction)
{
    DrawDialog(fileName, stage, fraction, 1.0f);
}

}
