#pragma once

#include <snd-ply/snd/FinalMixCallback.h>

#include <atomic>

class OutputMeter : public snd::FinalMixCallback
{
public:
    static OutputMeter &instance();

    void install();
    void uninstall();

    void update(float dt);

    struct ChannelState
    {
        float level = 0.0f;
        float peakHold = 0.0f;
        float peakHoldAge = 0.0f;
        bool clip = false;
    };

    enum class Channel
    {
        Left,
        Right
    };

    const ChannelState &getLeft() const { return mLeft; }
    const ChannelState &getRight() const { return mRight; }
    bool isMono() const { return mMono; }

    void resetClip(Channel ch) { (ch == Channel::Left ? mLeft : mRight).clip = false; }

    virtual void onFinalMix(const snd::FinalMixData *data) override;

private:
    OutputMeter() = default;

    static void updateChannel_(ChannelState &c, float linPeak, float dt);

    std::atomic<float> mPeakL{0.0f};
    std::atomic<float> mPeakR{0.0f};
    std::atomic<bool> mMonoFlag{false};

    ChannelState mLeft;
    ChannelState mRight;
    bool mMono = false;
    bool mInstalled = false;
};

void DrawOutputMeterWindow();
