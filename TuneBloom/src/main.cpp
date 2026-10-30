#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

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

#include <messages/Messages.h>
#include <bfsar/model/MessageLog.h>
#include <ui/app/ModelEventHandlers.h>
#include "core/TestHooks.h"
#include "ui/file/SystemShell.h"
#include "app/Utilll.h"
#include "aal/Bars.h"
#include "aal/BarsListFile.h"
#include "bfsar/archive/Bea.h"
#include "bfsar/archive/BfsarLayoutCheck.h"
#include "bfsar/archive/Cmpbin.h"
#include "bfsar/files/LooseStreamFile.h"
#include "bfsar/files/LooseWaveFile.h"
#include "ui/file/File.h"
#include "bfsar/io/FileIo.h"
#include "bfsar/io/PackFile.h"
#include "platform/SingleInstance.h"

#include <libs/portable-file-dialogs.h>

#if defined(SEAD_PLATFORM_MACOSX)
#include "platform/macos/bundle.h"
#include "platform/macos/openFiles.h"
#endif

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

static u8* NewArchiveBuffer(const std::vector<u8>& bytes, sead::FileDevice::LoadArg* loadArg)
{
    u8* buffer = new u8[bytes.size()];
    std::memcpy(buffer, bytes.data(), bytes.size());
    loadArg->read_size = bytes.size();

    return buffer;
}

static bool ensureDir(const char* path)
{
    if (packfile::IsPackPath(path))
        return true;

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

static void dumpItemIds(Bfsar& archive)
{
    u32 total = 0;

    total += dumpItemList("sound", archive.getSoundList());
    total += dumpItemList("soundgroup", archive.getSoundSetList());
    total += dumpItemList("bank", archive.getBankList());
    total += dumpItemList("player", archive.getPlayerList());
    total += dumpItemList("warc", archive.getWaveArchiveList());
    total += dumpItemList("group", archive.getGroupList());

    fprintf(stderr, "%u named items\n", total);
}

[[noreturn]] static void convertAudioResource(const char* inPath, const char* outPath, std::vector<u8> bytes, bool dumpIds)
{
    AudioResource resource;
    const char* error = nullptr;

    if (!resource.open(std::move(bytes), sead::FixedSafeString<512>(inPath), &error))
    {
        fprintf(stderr, "Failed to parse '%s': %s\n", inPath, error);
        exitConvert(1);
    }

    if (dumpIds)
    {
        for (const Item* asset : resource.getAssetList())
            fprintf(stdout, "asset      0x%08x  %s\n", aal::HashBarsName(asset->getName().cstr()), asset->getName().cstr());

        exitConvert(0);
    }

    if (!testhooks::ApplyResourceCommands(resource))
        exitConvert(1);

    sead::FixedSafeString<256> validateError;
    const Item* invalid = nullptr;

    if (!resource.validate(&validateError, &invalid))
    {
        fprintf(stderr, "'%s': %s\n", invalid ? invalid->getName().cstr() : inPath, validateError.cstr());
        exitConvert(1);
    }

    if (!ensureDir(outPath) || resource.saveAs(sead::FixedSafeString<512>(outPath), &error) != SaveResult::Saved)
    {
        fprintf(stderr, "Failed to save to '%s': %s\n", outPath, error ? error : "");
        exitConvert(1);
    }

    fprintf(stdout, "Done: '%s' -> '%s'\n", inPath, outPath);
    exitConvert(0);
}

[[noreturn]] static void convertBarsList(const char* inPath, const char* outPath, const u8* data, u64 size, bool dumpIds)
{
    BarsListFile list;
    const char* error = nullptr;

    if (!list.open(data, size, sead::FixedSafeString<512>(inPath)))
    {
        fprintf(stderr, "Failed to parse '%s'\n", inPath);
        exitConvert(1);
    }

    if (dumpIds)
    {
        for (const Item* entry : list.getEntryList())
        {
            const FoundBars* found = list.findBars(entry);
            fprintf(stdout, "entry      %s  %s\n", entry->getName().cstr(), found ? found->location.c_str() : "-");
        }

        exitConvert(0);
    }

    if (!testhooks::ApplyBarsListCommands(list))
        exitConvert(1);

    if (!ensureDir(outPath) || list.saveAs(sead::FixedSafeString<512>(outPath), &error) != SaveResult::Saved)
    {
        fprintf(stderr, "Failed to save to '%s': %s\n", outPath, error ? error : "");
        exitConvert(1);
    }

    fprintf(stdout, "Done: '%s' -> '%s'\n", inPath, outPath);
    exitConvert(0);
}

template <typename LooseFile>
[[noreturn]] static void convertLooseFile(const char* inPath, const char* outPath, const u8* data, u32 size)
{
    LooseFile looseFile;
    sead::FixedSafeString<256> error;

    if (!looseFile.open(data, size, sead::FixedSafeString<512>(inPath), &error))
    {
        fprintf(stderr, "Failed to parse '%s': %s\n", inPath, error.cstr());
        exitConvert(1);
    }

    if (!testhooks::ApplyLooseFileCommands(looseFile))
        exitConvert(1);

    if (!ensureDir(outPath) || looseFile.saveAs(sead::FixedSafeString<512>(outPath), &error) != SaveResult::Saved)
    {
        fprintf(stderr, "Failed to save to '%s': %s\n", outPath, error.cstr());
        exitConvert(1);
    }

    fprintf(stdout, "Done: '%s' -> '%s'\n", inPath, outPath);
    exitConvert(0);
}

[[noreturn]] static void convertArchive(const char* inPath, const char* outPath)
{
    const bool dumpIds = strcmp(outPath, "--dump-ids") == 0;

    if (strlen(inPath) > fileio::cPathLengthMax || (!dumpIds && strlen(outPath) > fileio::cPathLengthMax))
    {
        fprintf(stderr, "A path is longer than %d characters\n", fileio::cPathLengthMax);
        exitConvert(1);
    }

    std::vector<u8> packBytes;

    if (packfile::IsPackPath(inPath))
    {
        if (!packfile::ReadFile(inPath, &packBytes))
        {
            fprintf(stderr, "Failed to load '%s'\n", inPath);
            exitConvert(1);
        }

        if (aal::IsBarsList(packBytes.data(), packBytes.size()))
            convertBarsList(inPath, outPath, packBytes.data(), packBytes.size(), dumpIds);

        if (packBytes.size() < sizeof(nw::ut::BinaryFileHeader) || !isBfsarBcsar(packBytes.data()))
            convertAudioResource(inPath, outPath, std::move(packBytes), dumpIds);
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
    u8* bfsarFile = packBytes.empty() ? device->tryLoad(loadArg) : NewArchiveBuffer(packBytes, &loadArg);
    if (!bfsarFile)
    {
        fprintf(stderr, "Failed to load '%s'\n", inPath);
        exitConvert(1);
    }

    if (cmpbin::IsZstdFrame(bfsarFile, loadArg.read_size) || AudioResource::IsResourceFile(bfsarFile, loadArg.read_size))
    {
        std::vector<u8> bytes(bfsarFile, bfsarFile + loadArg.read_size);
        device->unload(bfsarFile);

        convertAudioResource(inPath, outPath, std::move(bytes), dumpIds);
    }

    if (aal::IsBarsList(bfsarFile, loadArg.read_size))
        convertBarsList(inPath, outPath, bfsarFile, loadArg.read_size, dumpIds);

    if (!dumpIds && LooseStreamFile::IsStreamFile(bfsarFile, static_cast<u32>(loadArg.read_size)))
        convertLooseFile<LooseStreamFile>(inPath, outPath, bfsarFile, static_cast<u32>(loadArg.read_size));

    if (!dumpIds && LooseWaveFile::IsWaveFile(bfsarFile, static_cast<u32>(loadArg.read_size)))
        convertLooseFile<LooseWaveFile>(inPath, outPath, bfsarFile, static_cast<u32>(loadArg.read_size));

    bea::Source beaSource;

    if (bea::IsBea(bfsarFile, loadArg.read_size))
    {
        std::vector<u8> archiveBytes;
        const bea::ExtractResult extracted = bea::ExtractSoundArchive(std::vector<u8>(bfsarFile, bfsarFile + loadArg.read_size), &archiveBytes, &beaSource);

        device->unload(bfsarFile);

        if (extracted != bea::ExtractResult::Ok)
        {
            fprintf(stderr, "'%s': %s\n", inPath, extracted == bea::ExtractResult::NoSoundArchive ? messages::archive::cBeaNoSoundArchive : messages::archive::cBeaDamaged);
            exitConvert(1);
        }

        bfsarFile = NewArchiveBuffer(archiveBytes, &loadArg);
    }

    const bool wasCmpbin = cmpbin::IsCompressed(bfsarFile, loadArg.read_size);
    const bool prefersZstd = wasCmpbin && cmpbin::DetectCodec(bfsarFile, loadArg.read_size) == cmpbin::Codec::Zstd;

    if (wasCmpbin)
    {
        std::vector<u8> archiveBytes;
        const bool decompressed = cmpbin::Decompress(bfsarFile, loadArg.read_size, archiveBytes);

        device->unload(bfsarFile);

        if (!decompressed)
        {
            fprintf(stderr, "'%s': %s\n", inPath, messages::archive::cCmpbinDecompressFailed);
            exitConvert(1);
        }

        bfsarFile = NewArchiveBuffer(archiveBytes, &loadArg);
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

    Bfsar archive;
    archive.setCliMode(true);

    if (!archive.open(bfsarFile, static_cast<u32>(loadArg.read_size), archivePath, nullptr, openAsPath ? &inPathStr : nullptr))
    {
        fprintf(stderr, "Failed to parse '%s'\n", inPath);
        exitConvert(1);
    }

    archive.setBeaSource(std::move(beaSource));

    if (wasCmpbin)
        archive.setCmpbinPreferZstd(prefersZstd);

    if (dumpIds)
    {
        dumpItemIds(archive);
        exitConvert(0);
    }

    if (!ensureDir(outPath))
    {
        fprintf(stderr, "Failed to create the folder of '%s'\n", outPath);
        exitConvert(1);
    }

    if (!testhooks::ApplyGroupCommands(archive))
        exitConvert(1);

    if (archive.saveAs(sead::FixedSafeString<512>(outPath)) != SaveResult::Saved)
    {
        if (archive.hasPendingSaveFinalize())
        {
            fprintf(stderr, "error: the output file cannot be replaced\n");
            archive.cancelPendingSaveFinalize();
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

    SetStartupFile(ec ? path : fullPath.c_str());
}

int main(int argc, char* argv[])
{
    if (argc == 2 && argv[1][0] != '-' && !testhooks::IsEnabled() && singleinstance::ForwardToRunningApp(argv[1]))
        return 0;

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

        sead::Framework::InitializeArg initArg;
        initArg.heap_size = 150 * 1024 * 1024;
        AppFramework::initialize(initArg);

        sead::HeapMgr::createUnboundHeap();
        sead::HeapMgr::instance()->setAllocFromNotSeadThreadHeap(sead::HeapMgr::getUnboundHeap());
        sead::HeapMgr::getUnboundHeap()->setEnableLock(true);

        sead::CurrentHeapSetter heapSetter(sead::HeapMgr::getUnboundHeap());

        MessageLog::createInstance(nullptr);
        InstallModelEventHandlers();

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
    else
        singleinstance::StartListening();

    sead::TaskBase::CreateArg rootArg(&sead::TTaskFactory<RootTask>);
    sead::Framework::RunArg runArg;
    runArg.prepare_stack_size = 8 * 1024 * 1024;

    framework->run(sead::HeapMgr::getRootHeap(0), rootArg, runArg);

    delete framework;

    fflush(stdout);
    _Exit(0);
}
