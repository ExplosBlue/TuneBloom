#pragma once

#include <ui/wave/LoopWaveformEditor.h>
#include <ui/wave/WaveImportPanel.h>
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
f32 CalcLeftLabelWidth();
void RowLabel(const char* label, f32 labelWidth, const char* detail = nullptr);
void BeginBoxTools(f32 width);
void EndBoxTools();
void FillBox(f32 height);
bool SampleField(const char* id, u32* value, u32 min, u32 max, u32 reset, u32 rate, bool hasValue = true);

enum class SourceLine
{
    Full,
    None
};

class WaveImportPanel
{
public:
    void draw(SourceLine source, const char* sourceName, const WaveImportEncoding& encoding, WaveFile::RiffWaveInfo* info);
    void reset();
    void tick();
    bool isDrawnThisFrame() const { return mDrawnThisFrame; }
    bool confirmCancel(bool cancelClicked);

    std::string finalizeForCommit(WaveFile::RiffWaveInfo* info);
    u32 calcOutputLength(const WaveFile::RiffWaveInfo& info) const;
    u32 calcOutputSampleRate(const WaveFile::RiffWaveInfo& info) const;

    f32 getBodyHeight() const
    {
        return mBodyHeight;
    }

    bool isModified() const
    {
        return mTouched;
    }

    void setOldData(const OldAudioData* data, OldAudioClear* cleared)
    {
        mOldData = data;
        mOldClear = cleared;
    }

    const char* formatOldDataWarning(const WaveFile::RiffWaveInfo& info, sead::BufferedSafeString* out) const;

private:
    struct FrameContext
    {
        WaveImportEncoding encoding;
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

    u32 mRateBeforeFit = 0;
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

    const OldAudioData* mOldData = nullptr;
    OldAudioClear* mOldClear = nullptr;

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
    FrameContext beginFrame(const WaveImportEncoding& encoding, WaveFile::RiffWaveInfo* info, bool passThrough);
    void constrainRanges(FrameContext& ctx);
    void commitLoop(FrameContext& ctx);

    void drawSourceHeader(SourceLine source, const char* sourceName, const WaveFile::RiffWaveInfo& info);
    void drawUndecodableSource(const WaveImportEncoding& encoding, WaveFile::RiffWaveInfo* info);

    void drawWaveformBox(FrameContext& ctx);
    void drawWaveformTools(FrameContext& ctx, f32 groupGap);
    void drawWaveform(FrameContext& ctx);
    void drawPreviewTransport(FrameContext& ctx, f32 buttonSize);

    void drawColumns(FrameContext& ctx);
    void drawFormatBox(FrameContext& ctx);
    void drawProcessingPanels(f32 fill);
    void drawPitchPanel();
    void drawNormalizePanel();
    void drawEncodingField(const WaveImportEncoding& encoding, f32 labelWidth);
    void drawBitrateField(const WaveImportEncoding& encoding, f32 labelWidth);
    void drawSampleRateField(const WaveImportEncoding& encoding, f32 labelWidth);
    void fitSampleRateToEncoding(const WaveImportEncoding& encoding);
    void selectSampleRate(u32 sampleRate);
    void drawChannelsField(f32 labelWidth);
    u32 calcOutputChannelCount(const WaveFile::RiffWaveInfo& info) const;
    bool isOutputSource(const WaveFile::RiffWaveInfo& info) const;
    void drawOldDataBox(const WaveFile::RiffWaveInfo& info);
    void drawTrimBox(FrameContext& ctx, f32 labelWidth);
    void drawLoopBox(FrameContext& ctx, f32 labelWidth, f32 fill);
    void drawLoopModeField(FrameContext& ctx, f32 width);
    void drawLoopRangeFields(FrameContext& ctx, f32 labelWidth);
    void drawCrossfadeKnob(FrameContext& ctx);
    void drawLoopTools(FrameContext& ctx);
};

}
