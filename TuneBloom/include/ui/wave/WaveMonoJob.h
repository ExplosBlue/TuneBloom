#pragma once

#include <basis/seadTypes.h>
#include <core/BackgroundJob.h>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

class AudioAsset;
class WaveFile;

namespace ImGui
{
    struct LoopWaveformState;
}

inline constexpr u32 cWaveViewMaxSamples = 60000000;

struct WaveMono
{
    std::vector<f32> samples;
    std::vector<f32> peakMin;
    std::vector<f32> peakMax;
};

using WaveMonoStop = std::function<bool()>;
using WaveMonoDecode = std::function<bool(std::vector<f32>*, const WaveMonoStop&)>;

class WaveMonoJob
{
public:
    ~WaveMonoJob();

    bool requestView(const WaveFile& wave);
    bool requestView(AudioAsset& asset);
    void requestPeaks(const WaveFile& wave);
    void cancel();
    bool poll(WaveMono* out);

    bool isLoading() const
    {
        return mLoading;
    }

    bool showsProgress() const;

private:
    void request(const WaveFile& wave, bool keepsSamples);
    void request(AudioAsset& asset);
    void request(WaveMonoDecode decode, u64 work, bool keepsSamples);
    void update();
    void startPending();

    std::unique_ptr<WaveMono> mReady;
    std::unique_ptr<WaveMono> mRunning;
    WaveMonoDecode mPending;
    bool mPendingKeepsSamples = true;
    std::atomic<u32> mRequestId { 0 };
    u32 mRunningId = 0;
    bool mLoading = false;
    f64 mLoadStartTime = 0.0;
    BackgroundJob mJob;
};

WaveMonoJob& GetWaveMonoJob(WaveMonoJob** job);
bool TakeWaveMono(WaveMonoJob& job, std::vector<f32>* mono, ImGui::LoopWaveformState& state);
