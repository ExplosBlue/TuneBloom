#include <cstdio>
#include <cstring>
#include <filesystem>

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

#include "app/AppFramework.h"
#include "tasks/RootTask.h"

#include <snd/DisposeCallbackMgr.h>
#include <snd/SoundThread.h>
#include <snd/ut/ut_BinaryFileFormat.h>

#include "ui/Messages.h"
#include "ui/app/PopupMgr.h"
#include "core/TestHooks.h"
#include "ui/file/SystemShell.h"
#include "app/Utilll.h"
#include "bfsar/archive/Bfsar.h"
#include "bfsar/archive/BfsarLayoutCheck.h"
#include "bfsar/io/FileIo.h"

#include <libs/portable-file-dialogs.h>

#if defined(SEAD_PLATFORM_MACOSX)
#include "platform/macos/bundle.h"
#include "platform/macos/openFiles.h"
#endif

extern Bfsar sBfsar;
extern sead::FixedSafeString<512> sDroppedFilePath;
static bool sCliMode = false;
static s32 sAssertDialogNum = 0;

bool util::isAssertDialogOpen()
{
    return sAssertDialogNum > 0;
}

void AssertException(const char* msg)
{
    if (sCliMode)
    {
        fprintf(stderr, "ASSERTION ERROR: %s\n", msg);
        return;
    }

    auto msgBox = [msg]()
    {
        sead::FormatFixedSafeString<2048> info(messages::app::cAssertReportFormat, msg);

        sAssertDialogNum++;
        pfd::message(messages::app::cAssertTitle, info.cstr(), pfd::choice::ok, pfd::icon::warning).result();
        sAssertDialogNum--;
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

static bool ensureDir(const char* path)
{
    const std::filesystem::path dir = std::filesystem::path(path).parent_path();

    if (dir.empty())
        return true;

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    return std::filesystem::is_directory(dir, ec);
}

[[noreturn]] static void exitConvert(int code)
{
    fflush(stdout);
    fflush(stderr);
    _Exit(code);
}

static u32 dumpItemList(const char* label, Item::List& list)
{
    u32 count = 0;

    for (const Item* item : list)
    {
        if (!item->isNameValid())
            continue;

        fprintf(stdout, "%-10s 0x%08x  %s\n", label, item->getIdWithType(), item->getName().cstr());
        count++;
    }

    return count;
}

static void dumpItemIds()
{
    u32 total = 0;

    total += dumpItemList("sound", sBfsar.getSoundList());
    total += dumpItemList("soundgroup", sBfsar.getSoundSetList());
    total += dumpItemList("bank", sBfsar.getBankList());
    total += dumpItemList("player", sBfsar.getPlayerList());
    total += dumpItemList("warc", sBfsar.getWaveArchiveList());
    total += dumpItemList("group", sBfsar.getGroupList());

    fprintf(stderr, "%u named items\n", total);
}

[[noreturn]] static void convertArchive(const char* inPath, const char* outPath)
{
    const bool dumpIds = strcmp(outPath, "--dump-ids") == 0;

    if (strlen(inPath) > fileio::cPathLengthMax || (!dumpIds && strlen(outPath) > fileio::cPathLengthMax))
    {
        fprintf(stderr, "A path is longer than %d characters\n", fileio::cPathLengthMax);
        exitConvert(1);
    }

    sead::FileDevice* device = sead::FileDeviceMgr::instance()->findDevice("native");
    if (!device)
    {
        fprintf(stderr, "Failed to find native file device\n");
        exitConvert(1);
    }

    sead::FileDevice::LoadArg loadArg;
    sead::FixedSafeString<512> inPathStr(inPath);
    loadArg.path = inPathStr;
    u8* bfsarFile = device->tryLoad(loadArg);
    if (!bfsarFile)
    {
        fprintf(stderr, "Failed to load '%s'\n", inPath);
        exitConvert(1);
    }

    if (loadArg.read_size < sizeof(nw::ut::BinaryFileHeader) || !isBfsarBcsar(bfsarFile) || !CheckSoundArchiveHeader(bfsarFile, static_cast<u32>(loadArg.read_size)))
    {
        fprintf(stderr, "'%s' is not a valid BFSAR/BCSAR file\n", inPath);
        exitConvert(1);
    }

    sFileEndian = nw::ut::GetFileEndian(*reinterpret_cast<const nw::ut::BinaryFileHeader*>(bfsarFile));

    const char* openAsPath = testhooks::GetOpenAsPath();
    const sead::FixedSafeString<512> archivePath(openAsPath ? openAsPath : inPath);

    testhooks::ApplyGroupFiles(archivePath.cstr());

    if (!sBfsar.open(bfsarFile, static_cast<u32>(loadArg.read_size), archivePath, nullptr, openAsPath ? &inPathStr : nullptr))
    {
        fprintf(stderr, "Failed to parse '%s'\n", inPath);
        exitConvert(1);
    }

    if (dumpIds)
    {
        dumpItemIds();
        exitConvert(0);
    }

    if (!ensureDir(outPath))
    {
        fprintf(stderr, "Failed to create the folder of '%s'\n", outPath);
        exitConvert(1);
    }

    if (!testhooks::ApplyGroupCommands())
        exitConvert(1);

    if (!sBfsar.saveAs(sead::FixedSafeString<512>(outPath)))
    {
        if (sBfsar.hasPendingSaveFinalize())
        {
            fprintf(stderr, "error: the output file cannot be replaced\n");
            sBfsar.cancelPendingSaveFinalize();
        }

        fprintf(stderr, "Failed to save to '%s'\n", outPath);
        exitConvert(1);
    }

    fprintf(stdout, "Done: '%s' -> '%s'\n", inPath, outPath);
    exitConvert(0);
}

static void setStartupFile(const char* path)
{
    std::error_code ec;
    const std::string fullPath = std::filesystem::absolute(path, ec).string();

    if (ec || fullPath.size() >= static_cast<size_t>(sDroppedFilePath.getBufferSize()))
        sDroppedFilePath = path;
    else
        sDroppedFilePath = fullPath.c_str();
}

int main(int argc, char* argv[])
{
    if (argc == 2 && argv[1][0] != '-')
        setStartupFile(argv[1]);

#if defined(SEAD_PLATFORM_MACOSX)
    macos::useBundleResources();
    macos::installOpenFileHandler();
#else
    if (argc < 3)
        UseExecutableFolder();
#endif

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

        snd::internal::driver::SoundThread::createInstance(nullptr);
        snd::internal::driver::DisposeCallbackMgr::createInstance(nullptr);

#if defined(SEAD_PLATFORM_WINDOWS)
        sead::FileDeviceMgr::instance()->mount(new sead::WinNativeFileDevice());
#elif defined(SEAD_PLATFORM_POSIX)
        sead::FileDeviceMgr::instance()->mount(new sead::PosixNativeFileDevice());
#endif

        convertArchive(argv[1], argv[2]);
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
    createArg.wait_vblank = testhooks::IsEnabled() ? 0 : 1;
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

    if (testhooks::IsEnabled())
        testhooks::HideWindow(framework->getWindowHandle());

    sead::TaskBase::CreateArg rootArg(&sead::TTaskFactory<RootTask>);
    sead::Framework::RunArg runArg;
    runArg.prepare_stack_size = 8 * 1024 * 1024;

    framework->run(sead::HeapMgr::getRootHeap(0), rootArg, runArg);

    delete framework;

    fflush(stdout);
    _Exit(0);
}
