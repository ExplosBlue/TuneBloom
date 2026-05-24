#include "tasks/RootTask.h"

#include <filedevice/seadFileDeviceMgr.h>
#include <framework/seadGameFramework.h>
#include <framework/seadProcessMeter.h>
#include <gfx/seadPrimitiveRenderer.h>

#if defined(SEAD_PLATFORM_WINDOWS)
#include <filedevice/win/seadWinNativeFileDeviceWin.h>
#elif defined(SEAD_PLATFORM_POSIX)
#include <filedevice/posix/seadPosixNativeFileDevicePosix.h>
#endif // SEAD_PLATFORM

#include "tasks/BfsarTask.h"
#include "tasks/ImGuiTask.h"

RootTask::RootTask(const sead::TaskConstructArg& arg)
    : sead::Task(arg, "RootTask")
    , mNativeFileDevice(nullptr)
{
}

void RootTask::prepare()
{
    {
        // sead::Framework::CreateSystemTaskArg arg;
        // arg.infloop_detection_span = sead::TickSpan::makeFromSeconds(5);

        // getFramework()->createSystemTasks(this, arg);
        sead::GameFramework* fw = sead::DynamicCast<sead::GameFramework>(getFramework());
        fw->createProcessMeter(this);
    }

#if defined(SEAD_PLATFORM_WINDOWS)
    mNativeFileDevice = new sead::WinNativeFileDevice();
#elif defined(SEAD_PLATFORM_POSIX)
    mNativeFileDevice = new sead::PosixNativeFileDevice();
#else
#error "Unsupported platform"
#endif // SEAD_PLATFORM

    sead::FileDeviceMgr::instance()->mount(mNativeFileDevice);

    sead::PrimitiveRenderer::createInstance(nullptr);
    sead::PrimitiveRenderer::instance()->prepare(nullptr, sead::SafeString::cEmptyString);

    adjustHeapAll();

    {
        sead::TaskBase::CreateArg arg(&sead::TTaskFactory<ImGuiTask>);
        arg.tag = sead::TaskBase::Tag::eSystem;

        getTaskMgr()->createSingletonTaskSync<ImGuiTask>(arg);
    }

    {
        sead::TaskBase::CreateArg arg(&sead::TTaskFactory<BfsarTask>);

        createTaskSync(arg);
    }

    //adjustHeapAll();
}

void RootTask::calc()
{
}

void RootTask::draw()
{
}
