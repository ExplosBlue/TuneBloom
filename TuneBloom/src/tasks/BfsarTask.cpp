#include "tasks/BfsarTask.h"

#include "tasks/ImGuiTask.h"

#include <framework/glfw/seadGameFrameworkBaseGlfw.h>
#include <framework/glfw/seadGameFrameworkGlfwGL.h>
#include <gfx/gl/seadTextureGL.h>
#include <gfx/seadGraphics.h>
#include <gfx/seadTextureUtil.h>
#include <heap/seadExpHeap.h>

#include "imgui/imgui.h"

#include "snd-ply/snd/SoundSystem.h"

#include "ui/app/UI.h"
#include "ui/browser/FilesPanel.h"
#include "ui/file/DocumentMgr.h"
#include "ui/file/File.h"
#include "ui/file/LoadProgress.h"
#include "ui/player/OutputMeter.h"
#include "ui/player/PlayerFrame.h"
#include <messages/Messages.h>
#include "ui/app/Notifications.h"
#include <ui/app/ModelEventHandlers.h>
#include <ui/app/PopupMgr.h>

#include <app/Utilll.h>
#include <core/TestHooks.h>
#include <libs/stb_image.h>
#include <platform/WindowFrame.h>
#include <platform/WindowMove.h>

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

static sead::TextureGL* CreateIconTexture(const u8* pixels, s32 width, s32 height)
{
    sead::GameFrameworkGlfwGL* fw = sead::DynamicCast<sead::GameFrameworkGlfwGL>(util::getFramework());
    SEAD_ASSERT(fw);

    sead::TextureGL* icon = new sead::TextureGL();

    sead::CurrentHeapSetter chs(fw->getGLHeap());

    bool r = sead::TextureUtil::createTextureImage2DGL(icon, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    SEAD_ASSERT(r);

    // Mipmaps

    sead::Graphics::instance()->lockDrawContext();
    glBindTexture(GL_TEXTURE_2D, icon->getID());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    sead::Graphics::instance()->unlockDrawContext();

    return icon;
}

void BfsarTask::prepare()
{
    sead::CurrentHeapSetter chs(sead::HeapMgr::getUnboundHeap());

    LoadAudioConfig();

    {
        const AudioSettings& audio = GetAudioSettings();

        snd::SoundSystem::SoundSystemParam param;
        param.voice_synthesize_buffer_count = 10;
        param.output_sample_rate = audio.outputSampleRate;
        param.playback_device_name = audio.playbackDeviceName.c_str();

        sead::ExpHeap* heap = sead::ExpHeap::create(0, "SoundHeap", getHeapArray().getPrimaryHeap());

        snd::SoundSystem::initialize(param, heap);

        heap->adjust();
    }

    OutputMeter::instance().install();

    ApplyAudioMixConfig();
    LoadMidiConfig();

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

        if (ImGuiTask::instance() && ImGuiTask::instance()->isDrawing())
            return;

        if (sIsCalc)
        {
            loadprogress::Repaint();
            return;
        }

        if (sRefreshCallback)
            sRefreshCallback(window);
    });

    windowmove::KeepDrawing(fw->getWindowHandle());
    windowframe::Install(fw->getWindowHandle());

    glfwSetDropCallback(fw->getWindowHandle(), [](GLFWwindow *window, s32 count, const char **paths)
    {
        if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
            return; // ignore dropped files while a popup is open

        f64 cursorX = 0.0;
        f64 cursorY = 0.0;

        if (!testhooks::GetDropCursor(&cursorX, &cursorY))
            glfwGetCursorPos(window, &cursorX, &cursorY);

        const bool isPinDrop = IsFilesPanelDropTarget(static_cast<f32>(cursorX), static_cast<f32>(cursorY));
        bool hasOpened = false;

        for (s32 i = 0; i < count; i++)
            hasOpened |= isPinDrop ? PinExternalPath(paths[i]) : OpenExternalPath(paths[i]);

        if (hasOpened)
        {
            glfwFocusWindow(window);
            return;
        }

        for (s32 i = 0; i < count; i++)
        {
            if (!HasFileExtension(paths[i], strlen(paths[i]), cWavExtension))
                continue;

            if (IsPlayerFrameShown())
                openqueue::PushExternal(paths[i]);
            else
                sDroppedWavPath = paths[i];

            return;
        }

        MessageLog::instance()->addPopup({ messages::app::cDropUnsupported });
    });

    MessageLog::createInstance(nullptr);
    InstallModelEventHandlers();
    PopupMgr::createInstance(nullptr);

    s32 width = 0;
    s32 height = 0;
    s32 channels = 0;
    u8* data = stbi_load("content/icon.png", &width, &height, &channels, 4);
    
    if (data && channels == 4)
    {
        util::setIcon_(CreateIconTexture(data, width, height));

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

    CloseAllDocuments();

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
    snd::SoundSystem::updateIdleOutput(ImGui::GetIO().DeltaTime);

    if (IsPlayerFrameShown())
    {
        DrawPlayerFrame();
    }
    else
    {
        AutoConnectMidiDevices();
        DrawMenuBar();
        DrawUI();
    }

    PollMidiInput();

    sIsCalc = false;
}

void BfsarTask::draw()
{
}
