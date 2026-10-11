#include "tasks/BfsarTask.h"

#include <framework/glfw/seadGameFrameworkBaseGlfw.h>
#include <gfx/gl/seadTextureGL.h>
#include <gfx/seadTextureUtil.h>
#include <heap/seadExpHeap.h>

#include "imgui/imgui.h"

#include "snd-ply/snd/SoundSystem.h"

#include "ui/app/UI.h"
#include "ui/player/OutputMeter.h"
#include "ui/file/LoadProgress.h"
#include "ui/Messages.h"
#include "ui/app/Notifications.h"
#include "ui/app/PopupMgr.h"

#include <app/Utilll.h>
#include <libs/stb_image.h>

#include <cctype>
#include <cstring>

#include <core/Debug.h>

#if defined(SEAD_PLATFORM_MACOSX)
#include "platform/macos/dockIcon.h"
#endif

static bool sIsCalc = false;
static GLFWwindowrefreshfun sRefreshCallback = nullptr;

BfsarTask::BfsarTask(const sead::TaskConstructArg& arg)
    : sead::Task(arg, "BfsarTask")
{
}

BfsarTask::~BfsarTask()
{
    exit();
}

GLFWimage sGlfwIcon;

void BfsarTask::prepare()
{
    {
        snd::SoundSystem::SoundSystemParam param;
        param.enable_visualization = true;
        param.voice_synthesize_buffer_count = 10;

        sead::ExpHeap* heap = sead::ExpHeap::create(0, "SoundHeap", nullptr);

        snd::SoundSystem::initialize(param, heap);

        heap->adjust();
    }

    OutputMeter::instance().install();

    LoadAudioConfig();
    ApplyAudioConfig();
    LoadMidiConfig();
    AutoConnectMidiDevices();

    sead::GameFrameworkBaseGlfw* fw = sead::DynamicCast<sead::GameFrameworkBaseGlfw>(util::getFramework());
    SEAD_ASSERT(fw);

    glfwSetWindowCloseCallback(fw->getWindowHandle(), [](GLFWwindow* window)
    {
        TryExit();
    });

    sRefreshCallback = glfwSetWindowRefreshCallback(fw->getWindowHandle(), [](GLFWwindow* window)
    {
        if (util::isAssertDialogOpen())
            return;

        if (sIsCalc)
        {
            loadprogress::Repaint();
            return;
        }

        if (sRefreshCallback)
            sRefreshCallback(window);
    });

    glfwSetDropCallback(fw->getWindowHandle(), [](GLFWwindow *window, s32 count, const char **paths)
    {
        if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
            return; // ignore dropped files while a popup is open

        auto hasExtensionCI = [](const char* path, const char* ext) -> bool
        {
            size_t pathLen = strlen(path);
            size_t extLen = strlen(ext);
            if (pathLen < extLen)
                return false;

            const char* suffix = path + (pathLen - extLen);
            for (size_t i = 0; i < extLen; i++)
            {
                if (std::tolower((unsigned char)suffix[i]) != std::tolower((unsigned char)ext[i]))
                    return false;
            }
            return true;
        };

        for (s32 i = 0; i < count; i++)
        {
            if (hasExtensionCI(paths[i], ".bfsar") || hasExtensionCI(paths[i], ".bcsar"))
            {
                sDroppedFilePath = paths[i];
                return;
            }
        }

        for (s32 i = 0; i < count; i++)
        {
            if (hasExtensionCI(paths[i], ".wav"))
            {
                sDroppedWavPath = paths[i];
                return;
            }
        }

        PopupMgr::instance()->addPopup({ messages::app::cDropUnsupported });
    });

    PopupMgr::createInstance(nullptr);

    s32 width = 0;
    s32 height = 0;
    s32 channels = 0;
    u8* data = stbi_load("content/icon.png", &width, &height, &channels, 4);
    if (data && channels == 4)
    {
        sead::TextureGL* icon = new sead::TextureGL();
        bool r = sead::TextureUtil::createTextureImage2DGL(icon, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        SEAD_ASSERT(r);

        util::setIcon_(icon);

        sGlfwIcon.width = width;
        sGlfwIcon.height = height;
        sGlfwIcon.pixels = data;
    }
    else
    {
        notify::Add(notify::Kind::Warning, messages::app::cIconLoadFailed);
    }

    adjustHeapAll();
    LOG_STR("BfsarTask prepared");
}

void BfsarTask::enter()
{
    sead::GameFrameworkBaseGlfw* fw = sead::DynamicCast<sead::GameFrameworkBaseGlfw>(util::getFramework());
    SEAD_ASSERT(fw);

#if defined(SEAD_PLATFORM_MACOSX)
    macos::setDockIcon("content/icon.png");
#else
    glfwSetWindowIcon(fw->getWindowHandle(), 1, &sGlfwIcon);
#endif
}

void BfsarTask::exit()
{
    sead::CurrentHeapSetter chs(sead::HeapMgr::getUnboundHeap());

    CloseFile();

    OutputMeter::instance().uninstall();
    snd::SoundSystem::finalize();
}

void BfsarTask::calc()
{
    sead::CurrentHeapSetter chs(sead::HeapMgr::getUnboundHeap());

    sIsCalc = true;

    UpdatePendingOpen();

    PopupMgr::instance()->update();

    OutputMeter::instance().update(ImGui::GetIO().DeltaTime);

    DrawMenuBar();
    DrawUI();

    PollMidiInput();

    sIsCalc = false;
}

void BfsarTask::draw()
{
}
