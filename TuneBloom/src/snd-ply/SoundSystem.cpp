#include "snd/SoundSystem.h"

#include "snd/SoundThread.h"
#include "snd/VoiceImpl.h"
#include "snd/DriverCommand.h"
#include "snd/TaskMgr.h"
#include "snd/MultiVoiceMgr.h"
#include "snd/ChannelMgr.h"
#include "snd/DisposeCallbackMgr.h"
#include "snd/TaskThread.h"
#include "snd/CurveLfo.h"
#include "snd/FFT.h"

#include "snd/SequenceSoundPlayer.h"

#include <heap/seadHeapMgr.h>
#include <thread/seadThreadUtil.h>

#include <cstring>

#define MINIAUDIO_IMPLEMENTATION
#include "snd/miniaudio.h"

namespace snd {

const u32 SoundSystem::cMaxVoiceCount = internal::driver::HardwareMgr::cMaxVoiceCount;
const u32 SoundSystem::cSamplePerFrame = internal::driver::HardwareMgr::cSamplePerFrame;

u32 SoundSystem::sMaxVoiceCount = SoundSystem::cMaxVoiceCount;

u32 SoundSystem::sLoadThreadStackSize = 0;
u32 SoundSystem::sSoundThreadStackSize = 0;

bool SoundSystem::sIsInitialized = false;
bool SoundSystem::sIsStreamLoadWait = false;
bool SoundSystem::sIsEnterSleep = false;
bool SoundSystem::sIsInitializedDriverCommandMgr = false;
bool SoundSystem::sIsInitializedVoiceRenderer = false;
bool SoundSystem::sIsStreamOpenFailureHalt = true;
bool SoundSystem::sIsEnableVisualization = false;

internal::VoiceRenderer* SoundSystem::sVoiceRenderer = nullptr;
u32 SoundSystem::sSoundThreadCommandBufferSize = 0;
u32 SoundSystem::sTaskThreadCommandBufferSize = 0;
u32 SoundSystem::sVoiceCommandBufferSize = 0;
u32 SoundSystem::sSynthesizeBufferSize = 0;

//static void OboeInit(sead::Heap* heap);
//static void OboeQuit();

static void InitSDK(sead::Heap* heap);
static void QuitSDK();

void SoundSystem::initialize(const SoundSystemParam& param, sead::Heap* heap)
{
    // Multiple initialization check
    if (sIsInitialized)
        return;

    internal::driver::HardwareMgr::createInstance(heap);
    internal::driver::SoundThread::createInstance(heap);
    internal::TaskMgr::createInstance(heap);
    internal::driver::MultiVoiceMgr::createInstance(heap);
    internal::driver::ChannelMgr::createInstance(heap);
    internal::driver::DisposeCallbackMgr::createInstance(heap);
    internal::TaskThread::createInstance(heap);

    sIsEnableVisualization = param.enable_visualization;

    // Initializes VoiceRenderer.
    if (!sIsInitializedVoiceRenderer)
    {
        sVoiceRenderer = new(heap) internal::VoiceRenderer();
        //sVoiceRenderer = (internal::VoiceRenderer*)malloc(sizeof(internal::VoiceRenderer));
        //new(sVoiceRenderer) internal::VoiceRenderer();

        sVoiceRenderer->initialize(param.voice_synthesize_buffer_count, heap);

        internal::driver::SoundThread::instance()->detail_setVoiceRenderer(sVoiceRenderer);

        sSynthesizeBufferSize = internal::VoiceSynthesizeBuffer::getSynthesizeBufferSize() * param.voice_synthesize_buffer_count;

        sIsInitializedVoiceRenderer = true;
    }

    // Initializes the command buffer.
    SoundSystem::detail_initializeDriverCommandMgr(param, heap);

    // Initializes SDK-related data.
    internal::driver::HardwareMgr::instance()->initialize();

    // Prepares the snd sound thread.
    {
        sSoundThreadStackSize = param.sound_thread_stack_size;
        internal::driver::SoundThread::instance()->initialize();
    }

    // Prepares the snd sound data load thread.
    {
        internal::TaskMgr::instance()->initialize(heap);
        sLoadThreadStackSize = param.task_thread_stack_size;
    }

    // Initializes MultiVoiceManager.
    {
        internal::driver::MultiVoiceMgr::instance()->initialize(sMaxVoiceCount, heap);
    }

    // ChannelManager initialization
    {
        internal::driver::ChannelMgr::instance()->initialize(sMaxVoiceCount, heap);
    }

    // Initializes SequenceSoundPlayer.
    SequenceSoundPlayer::initSequenceSoundPlayer();

    u16 attribute = 0;

    // Launches the data load thread.
    bool result = internal::TaskThread::instance()->create(sead::ThreadUtil::ConvertPrioritySeadToPlatform(param.task_thread_priority), sLoadThreadStackSize, attribute, heap);
    SEAD_ASSERT(result);

    // Sound thread start
    result = internal::driver::SoundThread::instance()->createSoundThread(sead::ThreadUtil::ConvertPrioritySeadToPlatform(param.sound_thread_priority), sSoundThreadStackSize, attribute, param.enable_get_sound_thread_tick, heap);
    SEAD_ASSERT(result);

    // Initializes the SEQ modulation curve table.
    internal::CurveLfo::initializeCurveTable();

    //internal::AxVoice::setSamplesPerFrame(AX_IN_SAMPLES_PER_FRAME);
    //internal::AxVoice::setSamplesPerSec(AX_IN_SAMPLES_PER_SEC);

    //OboeInit(heap);
    InitSDK(heap);

    sIsInitialized = true;
}

void SoundSystem::finalize()
{
    if (!sIsInitialized)
        return;

    //OboeQuit();
    QuitSDK();

    // Ends the data load thread.
    internal::TaskMgr::instance()->cancelAllTask();
    internal::TaskThread::instance()->destroy();
    internal::TaskMgr::instance()->finalize();

    // Ends the sound thread.
    internal::driver::SoundThread::instance()->destroy();

    // Destroys the channel manager.
    internal::driver::ChannelMgr::instance()->finalize();
    internal::driver::MultiVoiceMgr::instance()->finalize();
    internal::driver::HardwareMgr::instance()->finalize();

    if (sIsInitializedVoiceRenderer)
    {
        internal::driver::SoundThread::instance()->detail_setVoiceRenderer(nullptr);

        sVoiceRenderer->finalize();
        delete sVoiceRenderer;
        sVoiceRenderer = nullptr;

        sSynthesizeBufferSize = 0;

        sIsInitializedVoiceRenderer = false;
    }

    // Finalizes the sound thread.
    internal::driver::SoundThread::instance()->finalize();

    SoundSystem::detail_finalizeDriverCommandMgr();

    sLoadThreadStackSize = 0;
    sSoundThreadStackSize = 0;

    sIsEnableVisualization = false;

    internal::TaskThread::deleteInstance();
    internal::driver::DisposeCallbackMgr::deleteInstance();
    internal::driver::ChannelMgr::deleteInstance();
    internal::driver::MultiVoiceMgr::deleteInstance();
    internal::TaskMgr::deleteInstance();
    internal::driver::SoundThread::deleteInstance();
    internal::driver::HardwareMgr::deleteInstance();

    sIsInitialized = false;
}

void SoundSystem::detail_initializeDriverCommandMgr(const SoundSystemParam& param, sead::Heap* heap)
{
    if (sIsInitializedDriverCommandMgr)
        return;

    internal::DriverCommand::createInstance(heap);
    internal::DriverCommandForTaskThread::createInstance(heap);

    internal::DriverCommand::instance()->initialize(param.sound_thread_command_buffer_size, heap);
    internal::DriverCommandForTaskThread::instance()->initialize(param.task_thread_command_buffer_size, heap);

    sSoundThreadCommandBufferSize = param.sound_thread_command_buffer_size;
    sTaskThreadCommandBufferSize = param.task_thread_command_buffer_size;

    sIsInitializedDriverCommandMgr = true;
}

void SoundSystem::detail_finalizeDriverCommandMgr()
{
    if (!sIsInitializedDriverCommandMgr)
        return;

    internal::DriverCommandForTaskThread::instance()->finalize();
    internal::DriverCommand::instance()->finalize();

    internal::DriverCommandForTaskThread::deleteInstance();
    internal::DriverCommand::deleteInstance();

    sSoundThreadCommandBufferSize = 0;
    sTaskThreadCommandBufferSize = 0;

    sIsInitializedDriverCommandMgr = false;
}

void SoundSystem::setSoundFrameUserCallback(SoundFrameUserCallback callback, void* arg)
{
    internal::driver::SoundThread::instance()->registerSoundFrameUserCallback(callback, arg);
}

void SoundSystem::clearSoundFrameUserCallback()
{
    internal::driver::SoundThread::instance()->clearSoundFrameUserCallback();
}

void SoundSystem::lockSoundThread()
{
    internal::driver::SoundThread::instance()->lock();
}

void SoundSystem::unlockSoundThread()
{
    internal::driver::SoundThread::instance()->unlock();
}

void SoundSystem::enterSleep()
{
    if (sIsEnterSleep)
        return;

    sIsEnterSleep = true;
    sIsStreamLoadWait = true;
    internal::TaskThread::instance()->lock();

    // The sound thread can be stopped on the SDK side.
}

void SoundSystem::leaveSleep()
{
    if (!sIsEnterSleep)
        return;

    // The sound thread recovers on the SDK side.
    internal::TaskThread::instance()->unlock();
    sIsStreamLoadWait = false;
    sIsEnterSleep = false;
}

f32 sVisualizationWaveData[SoundSystem::cSamplePerFrame] = { 0 };
f32 sWaveData[SoundSystem::cSamplePerFrame] = { 0 };
f32 sFFTData[SoundSystem::cSamplePerFrame] = { 0 };

f32* SoundSystem::getWave()
{
    {
        internal::driver::SoundThreadLock lock;

        for (u32 i = 0; i < cSamplePerFrame; i++)
        {
            sWaveData[i] = sVisualizationWaveData[i];
        }
    }

    return sWaveData;
}

f32* SoundSystem::calcFFT()
{
    const u32 bufSize = cSamplePerFrame + (2 - 1llu) & ~(2 - 1llu);

    f32 temp[bufSize * 4];

    {
        internal::driver::SoundThreadLock lock;

        for (u32 i = 0; i < bufSize; i++)
        {
            if (i < cSamplePerFrame)
                temp[i*2] = sVisualizationWaveData[i];
            else
                temp[i*2] = 0.0f;

            temp[i*2+1] = 0.0f;

            temp[i+bufSize*2] = 0.0f;
            temp[i+bufSize*3] = 0.0f;
        }
    }

    FFT::fft(temp, bufSize);

    for (u32 i = 0; i < cSamplePerFrame; i++)
    {
        f32 real = temp[i * 2];
        f32 imag = temp[i * 2 + 1];

        sFFTData[i] = sqrt(real*real+imag*imag);
    }

    return sFFTData;
}

static u32 sOutputSampleRate = 48000;
static ma_device device;

static ma_context sContext;
static bool sContextReady = false;
static ma_device_info* sPlaybackInfos = nullptr;
static ma_uint32 sPlaybackInfoCount = 0;
static s32 sSelectedPlaybackDevice = 0; // 0 = system default

static void DataCallback(ma_device* pDevice, void* pOutput, const void*, ma_uint32 frameCount);

static void InitDevice()
{
    ma_device_config config    = ma_device_config_init(ma_device_type_playback);
    config.playback.format     = ma_format_f32;
    config.playback.channels   = internal::driver::HardwareMgr::cChannelCount;
    config.sampleRate          = sOutputSampleRate;
    config.dataCallback        = DataCallback;
    config.pUserData           = nullptr;
    config.periodSizeInFrames  = sOutputSampleRate * internal::driver::HardwareMgr::cSoundFrameIntervalMSEC / 1000;

    config.noPreSilencedOutputBuffer = false;
    config.noClip = false;

    ma_context* ctx = sContextReady ? &sContext : nullptr;
    if (sContextReady && sSelectedPlaybackDevice > 0 && (u32)(sSelectedPlaybackDevice - 1) < sPlaybackInfoCount)
        config.playback.pDeviceID = &sPlaybackInfos[sSelectedPlaybackDevice - 1].id;

    if (ma_device_init(ctx, &config, &device) != MA_SUCCESS)
    {
        if (config.playback.pDeviceID)
        {
            config.playback.pDeviceID = nullptr;
            sSelectedPlaybackDevice = 0;
            if (ma_device_init(ctx, &config, &device) != MA_SUCCESS)
            {
                SEAD_ASSERT_MSG(false, "Failed to initialize the device.");
                return;
            }
        }
        else
        {
            SEAD_ASSERT_MSG(false, "Failed to initialize the device.");
            return;
        }
    }

    ma_device_start(&device);
}

static void DataCallback(ma_device* pDevice, void* pOutput, const void*, ma_uint32 frameCount)
{
    const u32 cChannelCount   = internal::driver::HardwareMgr::cChannelCount;
    const u32 cInternalFrames = internal::driver::HardwareMgr::cSamplePerFrame;

    internal::driver::HardwareMgr::callHwCallback();

    f32* dataBuffers[cChannelCount] = { internal::driver::HardwareMgr::sLeftDataBuffer, internal::driver::HardwareMgr::sRightDataBuffer };

    internal::driver::HardwareMgr::resetFinalMixCallbackData();
    internal::driver::HardwareMgr::processFinalMixCallback(dataBuffers, cInternalFrames, cChannelCount);

    f32* outData = (f32*)pOutput;

    if (sOutputSampleRate == 48000)
    {
        for (u32 i = 0; i < frameCount; i++)
        {
            for (u32 ch = 0; ch < cChannelCount; ch++)
            {
                f32 s = dataBuffers[ch][i];
                outData[i * cChannelCount + ch] = sead::Mathf::clamp2(-1.0f, s / 32767.0f, 1.0f);
            }
        }
    }
    else
    {
        for (u32 i = 0; i < frameCount; i++)
        {
            f32 pos = (static_cast<f32>(i) * cInternalFrames) / frameCount;
            u32 idx = static_cast<u32>(pos);
            f32 frac = pos - static_cast<f32>(idx);
            u32 idx0 = sead::Mathu::min(idx, cInternalFrames - 1);
            u32 idx1 = sead::Mathu::min(idx + 1, cInternalFrames - 1);

            for (u32 ch = 0; ch < cChannelCount; ch++)
            {
                f32 s = dataBuffers[ch][idx0] + (dataBuffers[ch][idx1] - dataBuffers[ch][idx0]) * frac;
                outData[i * cChannelCount + ch] = sead::Mathf::clamp2(-1.0f, s / 32767.0f, 1.0f);
            }
        }
    }
}

static void InitSDK(sead::Heap* heap)
{
    sead::CurrentHeapSetter chs(heap);
    sOutputSampleRate = internal::driver::HardwareMgr::cSampleRate;

    if (ma_context_init(nullptr, 0, nullptr, &sContext) == MA_SUCCESS)
    {
        sContextReady = true;
        ma_context_get_devices(&sContext, &sPlaybackInfos, &sPlaybackInfoCount, nullptr, nullptr);
    }

    InitDevice();
}

static void QuitSDK()
{
    ma_device_uninit(&device);

    if (sContextReady)
    {
        ma_context_uninit(&sContext);
        sContextReady = false;
    }
}

void SoundSystem::pauseAudio()
{
    ma_device_stop(&device);
}

void SoundSystem::resumeAudio()
{
    ma_device_start(&device);
}

void SoundSystem::setOutputSampleRate(u32 rate)
{
    if (rate == sOutputSampleRate) return;

    sOutputSampleRate = rate;

    ma_device_stop(&device);
    ma_device_uninit(&device);
    InitDevice();
}

u32 SoundSystem::getOutputSampleRate()
{
    return sOutputSampleRate;
}

void SoundSystem::setMasterVolume(f32 volume)
{
    internal::driver::HardwareMgr::instance()->setMasterVolume(volume, 0);
}

f32 SoundSystem::getMasterVolume()
{
    return internal::driver::HardwareMgr::instance()->getMasterVolume();
}

void SoundSystem::refreshPlaybackDevices()
{
    if (sContextReady)
        ma_context_get_devices(&sContext, &sPlaybackInfos, &sPlaybackInfoCount, nullptr, nullptr);
}

u32 SoundSystem::getPlaybackDeviceCount()
{
    return sPlaybackInfoCount + 1;
}

const char *SoundSystem::getPlaybackDeviceName(u32 index)
{
    if (index == 0)
        return "System Default";

    u32 i = index - 1;
    if (i < sPlaybackInfoCount)
        return sPlaybackInfos[i].name;

    return "";
}

u32 SoundSystem::getPlaybackDevice()
{
    return (u32)sSelectedPlaybackDevice;
}

void SoundSystem::setPlaybackDevice(u32 index)
{
    if ((s32)index == sSelectedPlaybackDevice)
        return;
    if (index > sPlaybackInfoCount)
        return;

    sSelectedPlaybackDevice = (s32)index;

    ma_device_stop(&device);
    ma_device_uninit(&device);
    InitDevice();
}

void SoundSystem::setPlaybackDeviceByName(const char *name)
{
    if (!name || !*name)
    {
        setPlaybackDevice(0);
        return;
    }

    for (ma_uint32 i = 0; i < sPlaybackInfoCount; i++)
    {
        if (strcmp(sPlaybackInfos[i].name, name) == 0)
        {
            setPlaybackDevice(i + 1);
            return;
        }
    }
}

/*
class AudioDataCallback : public oboe::AudioStreamDataCallback
{
public:
    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* stream, void* audioData, s32 numFrames) override
    {
        const u32 cChannelCount = 2;

        internal::driver::HardwareMgr::callHwCallback();
        internal::driver::HardwareMgr::callHwCallback(); // do twice ???
        internal::driver::HardwareMgr::callHwCallback(); // do thrice ???

        f32* dataBuffers[cChannelCount] = { internal::driver::HardwareMgr::sLeftDataBuffer, internal::driver::HardwareMgr::sRightDataBuffer };

        internal::driver::HardwareMgr::resetFinalMixCallbackData();
        internal::driver::HardwareMgr::processFinalMixCallback(dataBuffers, numFrames, cChannelCount);

        s16* outData = (s16*)audioData;
        u32 idx = 0;

        for (u32 i = 0; i < numFrames * cChannelCount; i += cChannelCount)
        {
            outData[i + 0] = internal::driver::HardwareMgr::sLeftDataBuffer[idx];
            outData[i + 1] = internal::driver::HardwareMgr::sRightDataBuffer[idx];

            idx++;
        }

        return oboe::DataCallbackResult::Continue;
    }
};

class AudioErrorCallback : public oboe::AudioStreamErrorCallback
{
public:
    void onErrorAfterClose(oboe::AudioStream* stream, oboe::Result error) override
    {
    }
};

AudioDataCallback sAudioDataCallback;
AudioErrorCallback sAudioErrorCallback;

oboe::AudioStream* sAudioStream = nullptr;

static void openStream(Heap* heap)
{
    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Output);
    builder.setAudioApi(oboe::AudioApi::AAudio);
    builder.setFormat(oboe::AudioFormat::I16);
    builder.setFormatConversionAllowed(true);
    builder.setPerformanceMode(oboe::PerformanceMode::LowLatency);
    builder.setSharingMode(oboe::SharingMode::Exclusive);
    builder.setUsage(oboe::Usage::Game);
    builder.setContentType(oboe::ContentType::Music);
    builder.setSampleRate(internal::driver::HardwareMgr::cSampleRate);
    builder.setSampleRateConversionQuality(oboe::SampleRateConversionQuality::Medium);
    builder.setChannelCount(internal::driver::HardwareMgr::cChannelCount);
    builder.setFramesPerCallback(internal::driver::HardwareMgr::cSamplePerFrame);

    builder.setDataCallback(&sAudioDataCallback);
    builder.setErrorCallback(&sAudioErrorCallback);

    oboe::Result result;

    {
        ScopedCurrentHeap currentHeap(heap);

        result = builder.openStream(&sAudioStream);
    }

    if (result != oboe::Result::OK)
    {
        warn("Failed to open stream. Error: %s", oboe::convertToText(result));
        return;
    }

    SEAD_ASSERT(sAudioStream->getDirection() == oboe::Direction::Output);
    SEAD_ASSERT(sAudioStream->getAudioApi() == oboe::AudioApi::AAudio);
    SEAD_ASSERT(sAudioStream->getFormat() == oboe::AudioFormat::I16);
    SEAD_ASSERT(sAudioStream->getSampleRate() == internal::driver::HardwareMgr::cSampleRate);
    SEAD_ASSERT(sAudioStream->getChannelCount() == internal::driver::HardwareMgr::cChannelCount);
    SEAD_ASSERT(sAudioStream->getFramesPerCallback() == internal::driver::HardwareMgr::cSamplePerFrame);
}

static void OboeInit(Heap* heap)
{
    openStream(heap);

    if (!sAudioStream)
        return;

    oboe::Result result = sAudioStream->requestStart();
    if (result != oboe::Result::OK)
    {
        warn("Failed to start AudioStream");
        return;
    }

    //info("started AudioStream");
}

static void OboeQuit()
{
    if (!sAudioStream)
        return;

    sAudioStream->stop();
    sAudioStream->close();
    sAudioStream = nullptr;

    //info("closed AudioStream");
}
*/

} // namespace snd
