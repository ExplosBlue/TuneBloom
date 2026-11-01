#include <ui/wave/WaveMonoJob.h>

#include <ui/wave/LoopWaveformEditor.h>

#include <aal/Amta.h>
#include <aal/AudioAsset.h>
#include <aal/Bwav.h>
#include <bfsar/audio/WaveFileEditDecode.h>
#include <bfsar/files/WaveFile.h>

#include <imgui/imgui.h>

#include <basis/seadAssert.h>

static constexpr u64 cInlineDecodeWork = 2000000;
static constexpr u64 cOpusDecodeCost   = 8;
static constexpr f64 cProgressDelay    = 0.15;

struct WaveChannelCopy
{
    WaveChannelSource source;
    std::vector<u8> bytes;
};

using WaveChannelCopies = std::vector<WaveChannelCopy>;

static std::shared_ptr<WaveChannelCopies> CopyWaveChannels(const WaveFile& wave, bool copyBytes)
{
    const u32 channelNum = static_cast<u32>(wave.getChannels().size());
    std::shared_ptr<WaveChannelCopies> copies = std::make_shared<WaveChannelCopies>(channelNum);

    for (u32 c = 0; c < channelNum; c++)
    {
        WaveChannelCopy& copy = (*copies)[c];

        if (!GetWaveChannelSource(wave, c, &copy.source) || !copyBytes)
            continue;

        const u8* data = static_cast<const u8*>(copy.source.data);
        copy.bytes.assign(data, data + GetWaveChannelSourceSize(copy.source));
        copy.source.data = copy.bytes.data();
    }

    return copies;
}

static bool DecodeWaveChannelsMono(WaveChannelCopies& copies, u32 sampleCount, std::vector<f32>* mono, const WaveMonoStop& isStopped)
{
    if (copies.empty() || sampleCount == 0)
        return false;

    mono->assign(sampleCount, 0.0f);

    const f32 gain = 1.0f / static_cast<f32>(copies.size());

    for (WaveChannelCopy& copy : copies)
    {
        if (copy.source.data && !AddWaveChannelSource(copy.source, mono->data(), gain, isStopped))
            return false;

        std::vector<u8>().swap(copy.bytes);
    }

    return !isStopped();
}

static u64 CalcAssetDecodeWork(const AudioAsset& asset)
{
    const AudioAssetInfo& info = asset.getInfo();
    const u64 work = static_cast<u64>(info.sampleCount) * info.channelCount;
    aal::BwavCodec codec = aal::BwavCodec::DspAdpcm;

    if (GetAudioAssetBwavCodec(asset, &codec) && codec == aal::BwavCodec::Opus)
        return work * cOpusDecodeCost;

    return work;
}

static void RunDecode(const WaveMonoDecode& decode, const WaveMonoStop& isStopped, bool keepsSamples, WaveMono* out)
{
    if (!decode(&out->samples, isStopped) || isStopped())
    {
        *out = WaveMono();
        return;
    }

    ImGui::LoopWaveformBuildPeaks(out->samples, &out->peakMin, &out->peakMax);

    if (!keepsSamples)
        std::vector<f32>().swap(out->samples);

    if (isStopped())
        *out = WaveMono();
}

WaveMonoJob::~WaveMonoJob()
{
    cancel();
    mJob.wait();
}

bool WaveMonoJob::requestView(const WaveFile& wave)
{
    const bool tooLong = wave.getSampleCount() > cWaveViewMaxSamples;

    if (tooLong)
        cancel();
    else
        request(wave, true);

    return tooLong;
}

bool WaveMonoJob::requestView(AudioAsset& asset)
{
    const bool tooLong = asset.getInfo().sampleCount > cWaveViewMaxSamples;

    if (tooLong)
        cancel();
    else
        request(asset);

    return tooLong;
}

void WaveMonoJob::requestPeaks(const WaveFile& wave)
{
    request(wave, false);
}

void WaveMonoJob::request(const WaveFile& wave, bool keepsSamples)
{
    const u32 sampleCount = wave.getSampleCount();
    const u64 work = static_cast<u64>(sampleCount) * wave.getChannels().size();
    std::shared_ptr<WaveChannelCopies> copies = CopyWaveChannels(wave, work > cInlineDecodeWork);

    request([copies, sampleCount](std::vector<f32>* mono, const WaveMonoStop& isStopped) { return DecodeWaveChannelsMono(*copies, sampleCount, mono, isStopped); }, work, keepsSamples);
}

void WaveMonoJob::request(AudioAsset& asset)
{
    const AudioAssetInfo& info = asset.getInfo();

    if (info.kind == AudioAssetKind::Fwav)
    {
        const WaveFile* wave = asset.getPreviewWave();

        if (wave)
            request(*wave, true);
        else
            request([](std::vector<f32>*, const WaveMonoStop&) { return false; }, 0, true);

        return;
    }

    const AudioAssetFile file = asset.getCompleteFile();
    const AudioAssetKind kind = info.kind;
    const bool isMidSide = aal::IsAmtaMidSide(asset.getMeta());

    request([file, kind, isMidSide](std::vector<f32>* mono, const WaveMonoStop& isStopped) { return DecodeAudioAssetMono(kind, file, isMidSide, mono, isStopped); },
            CalcAssetDecodeWork(asset), true);
}

void WaveMonoJob::request(WaveMonoDecode decode, u64 work, bool keepsSamples)
{
    mRequestId++;
    mReady.reset();

    if (work <= cInlineDecodeWork)
    {
        mPending = nullptr;
        mLoading = false;
        mReady = std::make_unique<WaveMono>();
        RunDecode(decode, [] { return false; }, keepsSamples, mReady.get());
        return;
    }

    if (!mLoading)
        mLoadStartTime = ImGui::GetTime();

    mPending = std::move(decode);
    mPendingKeepsSamples = keepsSamples;
    mLoading = true;
    update();
}

void WaveMonoJob::cancel()
{
    mRequestId++;
    mPending = nullptr;
    mReady.reset();
    mLoading = false;
}

bool WaveMonoJob::showsProgress() const
{
    return mLoading && ImGui::GetTime() - mLoadStartTime >= cProgressDelay;
}

void WaveMonoJob::update()
{
    if (mJob.finish())
    {
        if (mRunningId == mRequestId)
        {
            mReady = std::move(mRunning);
            mLoading = false;
        }

        mRunning.reset();
    }

    if (mPending && !mJob.isBusy())
        startPending();
}

void WaveMonoJob::startPending()
{
    mRunning = std::make_unique<WaveMono>();
    mRunningId = mRequestId;

    WaveMono* result = mRunning.get();
    WaveMonoDecode decode = std::move(mPending);
    const bool keepsSamples = mPendingKeepsSamples;
    const std::atomic<u32>* requestId = &mRequestId;
    const u32 id = mRunningId;

    mPending = nullptr;
    mJob.start([decode, keepsSamples, result, requestId, id]() { RunDecode(decode, [requestId, id] { return requestId->load() != id; }, keepsSamples, result); });
}

bool WaveMonoJob::poll(WaveMono* out)
{
    update();

    if (!mReady)
        return false;

    *out = std::move(*mReady);
    mReady.reset();

    return true;
}

WaveMonoJob& GetWaveMonoJob(WaveMonoJob** job)
{
    if (!*job)
        *job = new WaveMonoJob();

    return **job;
}

bool TakeWaveMono(WaveMonoJob& job, std::vector<f32>* mono, ImGui::LoopWaveformState& state)
{
    WaveMono result;

    if (!job.poll(&result))
        return false;

    SEAD_ASSERT(!result.samples.empty() || result.peakMin.empty());

    const bool appears = mono->empty();

    *mono = std::move(result.samples);
    state.dataRevision++;

    if (appears && !mono->empty())
        state.appearTime = ImGui::GetTime();

    if (!mono->empty())
        ImGui::LoopWaveformSetPeaks(state, *mono, result.peakMin, result.peakMax);

    return true;
}
