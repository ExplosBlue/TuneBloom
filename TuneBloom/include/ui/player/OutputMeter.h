#pragma once

#include <snd-ply/snd/FinalMixCallback.h>

#include <atomic>

struct ImVec2;

class OutputMeter : public snd::FinalMixCallback
{
public:
    struct ChannelState
    {
        f32 level = 0.0f;
        f32 peakHold = 0.0f;
        f32 peakHoldAge = 0.0f;
        bool clip = false;
    };

public:
    static OutputMeter& instance();

    static void updateChannel(ChannelState& channel, f32 peak, f32 dt);

    void install();
    void uninstall();

    void update(f32 dt);
    void resetPeaks();

    const ChannelState& getLeft() const { return mLeft; }
    const ChannelState& getRight() const { return mRight; }
    bool isMono() const { return mMono; }

    virtual void onFinalMix(const snd::FinalMixData* data) override;

private:
    OutputMeter() = default;

    std::atomic<f32> mPeakL{0.0f};
    std::atomic<f32> mPeakR{0.0f};
    std::atomic<bool> mMonoFlag{false};

    ChannelState mLeft;
    ChannelState mRight;
    bool mMono = false;
    bool mInstalled = false;
};

void RegisterOutputMeterSettings();
void DrawOutputMeterWindow();
bool IsOutputMeterWindowVisible();
void SetOutputMeterWindowVisible(bool visible);
void DrawOutputMeterBar(const OutputMeter::ChannelState& state, const ImVec2& min, const ImVec2& max, bool horizontal);
void DrawOutputMeterRows(const ImVec2& min, f32 width, f32 rowHeight, f32 gap);
f32 OutputMeterDbToPosition(f32 db);
f32 OutputMeterPositionToDb(f32 position);
