#pragma once

#include <ui/wave/LoopWaveformEditor.h>
#include <ui/wave/WaveImportSettings.h>

#include <bfsar/player/SoundPlayer.h>

#include <core/BackgroundJob.h>

#include <initializer_list>
#include <string>

namespace waveimport
{

constexpr f32 cTitleGapEm = 0.3f;
constexpr f32 cRowGapEm   = 0.4f;

f32 CalcLabelWidth(std::initializer_list<const char*> labels);
void RowLabel(const char* label, f32 labelWidth, const char* detail = nullptr);
void BeginBoxTools(f32 width);
void EndBoxTools();
void FillBox(f32 height);
bool SampleField(const char* id, u32* value, u32 min, u32 max, u32 reset, u32 rate, bool hasValue = true);

class WaveImportPanel
{
public:
    void draw(const char* header, WaveFile::Encoding* encoding, WaveFile::RiffWaveInfo* info);
    void reset();
    void tick();
    bool confirmCancel(bool cancelClicked);

    std::string finalizeForCommit(WaveFile::RiffWaveInfo* info);

    f32 getBodyHeight() const
    {
        return mBodyHeight;
    }

    bool isModified() const
    {
        return mTouched;
    }

private:
    struct FrameContext
    {
        WaveFile::Encoding* encoding = nullptr;
        WaveFile::RiffWaveInfo* info = nullptr;

        u32 totalSamples = 0;

        bool manualLoop = false;
        bool loopActive = false;
        bool showLoop = false;
        bool editable = false;

        u32 loopStart = 0;
        u32 loopEnd = 0;

        bool cropEditable = false;
        u32 cropStart = 0;
        u32 cropEnd = 0;

        bool changed = false;
        bool rebuiltThisFrame = false;
    };

    struct PreviewInfo
    {
        u32 cropStart = 0;
        u32 totalSamples = 0;
        u32 sampleCount = 0;
        f32 gain = 1.0f;
        f32 speed = 1.0f;
    };

    enum class JobKind
    {
        None,
        Shape,
        Preview,
    };

    WaveImportSettings mSettings;
    ImGui::LoopWaveformState mWaveformState;

    WaveFile* mPreviewWave = nullptr;
    WaveFile* mSpareWave = nullptr;
    PreviewInfo mPreview;
    PreviewInfo mSparePreview;

    BackgroundJob mJob;
    JobKind mJobKind = JobKind::None;
    ShapedAudio mShapedResult;

    bool mTouched = false;
    bool mDrawnThisFrame = false;
    bool mPreviewBuilt = false;
    bool mPreviewStale = false;
    bool mPlayRequested = false;
    bool mLoopPlayback = true;
    bool mPeakStale = false;

    f32 mBodyTop = 0.0f;
    f32 mBodyHeight = 0.0f;
    bool mBodyScroll = false;
    f32 mColumnFill[2] = {};

    void ensureDecoded(const WaveFile::RiffWaveInfo& info);
    void rescaleWaveformView(u32 oldLength, u32 newLength);
    OutputParams makeOutputParams(bool isLoop, u32 loopStart, u32 loopEnd, u32 outputEnd, f32 gain) const;

    void applyShapedAudio(ShapedAudio& shaped);
    void rebuildShapedNow(bool passThrough);
    void startShapeJob(bool passThrough);
    void startPreviewJob(const FrameContext& ctx);
    void finishPreviewJob();
    bool pollJob();
    bool updateDerived(bool passThrough);
    void cancelJob();

    bool isPreviewActive() const;
    void stopPreview();
    WaveFileScale calcPreviewScale() const;
    bool playPreview(u32 offset, bool muted = false);
    f32 getPlayheadSample() const;
    void seekPreview(f64 sample);
    void refreshPreview(FrameContext& ctx);

    bool beginBody();
    void endBody();
    FrameContext beginFrame(WaveFile::Encoding* encoding, WaveFile::RiffWaveInfo* info, bool passThrough);
    void constrainRanges(FrameContext& ctx);
    void commitLoop(FrameContext& ctx);

    void drawSourceHeader(const char* header, u32 sampleRate, u32 channelCount, u32 sampleCount);
    void drawUndecodableSource(const char* header, WaveFile::Encoding* encoding, WaveFile::RiffWaveInfo* info);

    void drawWaveformBox(FrameContext& ctx);
    void drawWaveformTools(FrameContext& ctx, f32 groupGap);
    void drawWaveform(FrameContext& ctx);
    void drawPreviewTransport(FrameContext& ctx, f32 buttonSize);

    void drawColumns(FrameContext& ctx);
    void drawFormatBox(FrameContext& ctx);
    void drawProcessingPanels(f32 fill);
    void drawPitchPanel();
    void drawNormalizePanel();
    void drawEncodingField(WaveFile::Encoding* encoding, f32 labelWidth);
    void drawSampleRateField(f32 labelWidth);
    void drawChannelsField(f32 labelWidth);
    void drawTrimBox(FrameContext& ctx, f32 labelWidth);
    void drawLoopBox(FrameContext& ctx, f32 labelWidth, f32 fill);
    void drawLoopModeField(FrameContext& ctx, f32 width);
    void drawLoopRangeFields(FrameContext& ctx, f32 labelWidth);
    void drawCrossfadeKnob(FrameContext& ctx);
    void drawLoopTools(FrameContext& ctx);
};

}
