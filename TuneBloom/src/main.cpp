#include <devenv/seadAssertConfig.h>
#include <heap/seadExpHeap.h>
#include <heap/seadHeapMgr.h>

#include "AppFramework.h"
#include "tasks/RootTask.h"

#include "Utilll.h"

#include <portable-file-dialogs.h>

void AssertException(const char* msg)
{
    auto msgBox = [msg]()
    {
        sead::FormatFixedSafeString<2048> info("%s\nAn unexpected error has ocurred, please report this issue on 'https://github.com/stupidestmodder/TuneBloom/issues' or ask here 'https://go.nsmbu.net/discord'", msg);
        pfd::message("ASSERTION ERROR", info.cstr(), pfd::choice::ok, pfd::icon::warning).result();
    };

    if (sead::ThreadMgr::instance() && sead::ThreadMgr::instance()->getCurrentThread())
    {
        sead::CurrentHeapSetter chs(sead::HeapMgr::getUnboundHeap());
        msgBox();
    }
    else
    {
        msgBox();
    }
}

int main()
{
    sead::Delegate1 deleg = sead::FunctionDelegateCreator<const char*>(&AssertException);
    sead::AssertConfig::registerFinalCallback(&deleg);

    sead::Framework::InitializeArg initArg;
    initArg.heap_size = 150 * 1024 * 1024; // 150 MiB
    AppFramework::initialize(initArg);

    sead::HeapMgr::createUnboundHeap();
    sead::HeapMgr::instance()->setAllocFromNotSeadThreadHeap(sead::HeapMgr::getUnboundHeap());
    sead::HeapMgr::getUnboundHeap()->setEnableLock(true);

    AppFramework::CreateArg createArg;
    createArg.wait_vblank = 0;
    createArg.window_name = util::cAppName;
    // createArg.window_ex_style = WS_EX_ACCEPTFILES; // TODO: win32 for drag-and-drop
    createArg.clear_color = sead::Color4f(0.0f, 0.0f, 0.3f, 1.0f);

    AppFramework* framework = nullptr;
    {
        sead::ExpHeap* heap = sead::ExpHeap::create(0, "AppFramework", sead::HeapMgr::getRootHeap(0));
        framework = new(heap) AppFramework(createArg);
        heap->adjust();
    }

    util::setFramework_(framework);
    util::updateTitle(nullptr);

    {
        sead::ExpHeap* heap = sead::ExpHeap::create(0, "GraphicSystem", sead::HeapMgr::getRootHeap(0));
        framework->initializeGraphicsSystem(heap, sead::Vector2f(1280.0f, 720.0f));
        heap->adjust();
    }

    sead::TaskBase::CreateArg rootArg(&sead::TTaskFactory<RootTask>);
    sead::Framework::RunArg runArg;
    runArg.prepare_stack_size = 8 * 1024 * 1024;

    framework->run(sead::HeapMgr::getRootHeap(0), rootArg, runArg);

    delete framework;

    // TODO: sead framework destroy
    sead::ThreadMgr::instance()->destroy();
    sead::HeapMgr::destroy();
}

// extern "C"
// {
// 	__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
// 	__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
// }
