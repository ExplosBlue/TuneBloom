#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>

#include <devenv/seadAssertConfig.h>
#include <filedevice/seadFileDevice.h>
#include <filedevice/seadFileDeviceMgr.h>
#include <heap/seadExpHeap.h>
#include <heap/seadHeapMgr.h>

#if defined(SEAD_PLATFORM_WINDOWS)
#include <filedevice/win/seadWinNativeFileDeviceWin.h>
#elif defined(SEAD_PLATFORM_POSIX)
#include <filedevice/posix/seadPosixNativeFileDevicePosix.h>
#endif

#include "AppFramework.h"
#include "tasks/RootTask.h"

#include <snd/ut/ut_BinaryFileFormat.h>

#include "ui/PopupMgr.h"
#include "Utilll.h"
#include "bfsar/Bfsar.h"

#include <portable-file-dialogs.h>

extern Bfsar sBfsar;
static bool sCliMode = false;

void AssertException(const char* msg)
{
    if (sCliMode)
    {
        fprintf(stderr, "ASSERTION ERROR: %s\n", msg);
        return;
    }

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

static bool isBfsarBcsar(const void* file)
{
    return memcmp(file, "FSAR", 4) == 0 || memcmp(file, "CSAR", 4) == 0;
}

static void ensureDir(const char* path)
{
    std::string dir(path);
    size_t pos = dir.find_last_of('/');
    if (pos == std::string::npos)
        return;
    dir.resize(pos);
    if (dir.empty() || dir == ".")
        return;
    for (size_t i = 1; i < dir.size(); i++)
    {
        if (dir[i] == '/')
        {
            dir[i] = '\0';
            mkdir(dir.c_str(), 0755);
            dir[i] = '/';
        }
    }
    mkdir(dir.c_str(), 0755);
}

int main(int argc, char* argv[])
{
    sead::Delegate1 deleg = sead::FunctionDelegateCreator<const char*>(&AssertException);
    sead::AssertConfig::registerFinalCallback(&deleg);

    // CLI mode: convert input to output without UI
    if (argc >= 3)
    {
        sCliMode = true;
        sBfsar.setCliMode(true);

        sead::Framework::InitializeArg initArg;
        initArg.heap_size = 150 * 1024 * 1024;
        AppFramework::initialize(initArg);

        sead::HeapMgr::createUnboundHeap();
        sead::HeapMgr::instance()->setAllocFromNotSeadThreadHeap(sead::HeapMgr::getUnboundHeap());
        sead::HeapMgr::getUnboundHeap()->setEnableLock(true);

        sead::CurrentHeapSetter heapSetter(sead::HeapMgr::getUnboundHeap());

        PopupMgr::createInstance(nullptr);

#if defined(SEAD_PLATFORM_WINDOWS)
        sead::FileDeviceMgr::instance()->mount(new sead::WinNativeFileDevice());
#elif defined(SEAD_PLATFORM_POSIX)
        sead::FileDeviceMgr::instance()->mount(new sead::PosixNativeFileDevice());
#endif

        const char* inPath = argv[1];
        const char* outPath = argv[2];

        sead::FileDevice* device = sead::FileDeviceMgr::instance()->findDevice("native");
        if (!device)
        {
            fprintf(stderr, "Failed to find native file device\n");
            return 1;
        }

        sead::FileDevice::LoadArg loadArg;
        sead::FormatFixedSafeString<1024> inPathStr(inPath);
        loadArg.path = inPathStr;
        u8* bfsarFile = device->tryLoad(loadArg);
        if (!bfsarFile)
        {
            fprintf(stderr, "Failed to load '%s'\n", inPath);
            return 1;
        }

        if (!isBfsarBcsar(bfsarFile))
        {
            fprintf(stderr, "'%s' is not a valid BFSAR/BCSAR file\n", inPath);
            delete bfsarFile;
            return 1;
        }

        sFileEndian = nw::ut::GetFileEndian(*reinterpret_cast<const nw::ut::BinaryFileHeader*>(bfsarFile));

        if (!sBfsar.open(bfsarFile, static_cast<u32>(loadArg.read_size), inPathStr, nullptr))
        {
            fprintf(stderr, "Failed to parse '%s'\n", inPath);
            return 1;
        }

        sead::FormatFixedSafeString<1024> outPathStr(outPath);
        ensureDir(outPath);
        if (!sBfsar.saveAs(outPathStr))
        {
            fprintf(stderr, "Failed to save to '%s'\n", outPath);
            return 1;
        }

        fprintf(stdout, "Done: '%s' -> '%s'\n", inPath, outPath);
        fflush(stdout);
        _Exit(0);
        return 0;
    }

    // Normal GUI mode
    {
        sead::Framework::InitializeArg initArg;
        initArg.heap_size = 150 * 1024 * 1024;
        AppFramework::initialize(initArg);

        sead::HeapMgr::createUnboundHeap();
        sead::HeapMgr::instance()->setAllocFromNotSeadThreadHeap(sead::HeapMgr::getUnboundHeap());
        sead::HeapMgr::getUnboundHeap()->setEnableLock(true);
    }

    AppFramework::CreateArg createArg;
    createArg.wait_vblank = 0;
    createArg.window_name = util::cAppName;
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
}
