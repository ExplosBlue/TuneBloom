#include <ui/resource/ResourceViews.h>

#include <ui/file/File.h>
#include <ui/items/ItemColumns.h>
#include <ui/items/ItemList.h>
#include <ui/widgets/Knob.h>
#include <ui/wave/LoopWaveformEditor.h>
#include <ui/Messages.h>
#include <ui/player/PlayerViews.h>
#include <ui/app/PopupMgr.h>
#include <ui/widgets/Properties.h>
#include <ui/file/UnsavedChanges.h>
#include <ui/wave/WaveFormatUI.h>
#include <ui/widgets/Widgets.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_internal.h>

#include <bfsar/audio/AudioProcessing.h>
#include <bfsar/audio/DecodedPcm.h>

#include <vector>

static constexpr f32 cWaveViewHeightEm   = 7.0f;
static constexpr f32 cWaveTitleGapEm     = 0.3f;
static constexpr f32 cLoopFieldWidthEm   = 7.0f;
static constexpr f32 cLoopDragRange      = 2000.0f;
static constexpr u32 cWaveViewMaxSamples = 12000000;

class AudioAssetPanel
{
public:
    void draw(AudioAsset* asset);
    void release();

private:
    struct FrameContext
    {
        AudioAsset* asset = nullptr;
        const AudioAssetInfo* info = nullptr;
        const char* loopReason = nullptr;
        bool canSetLoop = false;
        bool playing = false;
    };

    FrameContext beginFrame(AudioAsset* asset);
    void refreshView(AudioAsset* asset);
    void commitLoop(FrameContext& ctx, bool isLoop, u32 loopStartFrame);

    void drawName(const FrameContext& ctx);
    void drawWaveformBox(FrameContext& ctx);
    void drawWaveCanvas(FrameContext& ctx);
    void drawLoopSection(FrameContext& ctx);
    void drawAudioBox(const FrameContext& ctx);
    void drawStreamBox(const FrameContext& ctx);
    void drawMetadataBox(const FrameContext& ctx);

    const AudioAsset* mAsset = nullptr;
    AudioAssetFile mSource;
    std::vector<float> mMono;
    bool mTooLong = false;
    ImGui::LoopWaveformState mState;
    u32 mLoopStartFrame = 0;
    u32 mLoopEndFrame = 0;
};

static AudioAssetPanel sPanel;

static void DrawSampleTimeRow(const char* label, u32 samples, u32 sampleRate)
{
    sead::FixedSafeString<32> time;
    FormatSampleTime(&time, samples, sampleRate);

    PropertyRow(label);
    ImGui::TextUnformatted(time.cstr());
    ImGui::SameLine(0.0f, ImGui::GetFontSize());
    ImGui::TextDisabled(messages::wavefile::cSamplesFormat, samples);
}

static const char* GetStreamStateText(const AudioAsset& asset)
{
    if (asset.needsStreamFileWrite())
        return messages::resource::cStreamPending;

    switch (asset.getStreamFileState())
    {
        case StreamFileState::Loaded:
            return messages::resource::cStreamLoaded;

        case StreamFileState::Damaged:
            return messages::resource::cStreamDamaged;

        case StreamFileState::Mismatched:
            return messages::resource::cStreamStateMismatched;

        default:
            break;
    }

    return messages::resource::cStreamMissing;
}

void AudioAssetPanel::release()
{
    mAsset = nullptr;
    mSource.reset();
    mMono.clear();
    mState = ImGui::LoopWaveformState{};
}

void AudioAssetPanel::refreshView(AudioAsset* asset)
{
    const AudioAssetFile& source = asset->getCompleteFile();

    if (asset == mAsset && source == mSource)
        return;

    if (asset != mAsset)
    {
        mState = ImGui::LoopWaveformState{};
        mState.grabMarkers = true;
        mState.lockEnd = true;
    }

    mAsset = asset;
    mSource = source;
    mMono.clear();
    mState.dataRevision++;

    const AudioAssetInfo& info = asset->getInfo();

    mLoopStartFrame = info.loopStartFrame;
    mLoopEndFrame = info.isLoop ? info.loopEndFrame : info.sampleCount;
    mTooLong = info.sampleCount > cWaveViewMaxSamples;

    DecodedPcm pcm;

    if (!mTooLong && asset->decode(&pcm))
        AudioProcessing::mixToMono(pcm.channels, mMono);
}

AudioAssetPanel::FrameContext AudioAssetPanel::beginFrame(AudioAsset* asset)
{
    AudioResource& resource = ActiveAudioResource();

    resource.loadStreamFile(asset);
    refreshView(asset);

    FrameContext ctx;
    ctx.asset = asset;
    ctx.info = &asset->getInfo();
    ctx.canSetLoop = resource.canSetLoop(*asset, &ctx.loopReason);
    ctx.playing = asset->isPlaying();

    return ctx;
}

void AudioAssetPanel::commitLoop(FrameContext& ctx, bool isLoop, u32 loopStartFrame)
{
    const char* error = nullptr;

    if (!ActiveAudioResource().setLoop(ctx.asset, isLoop, loopStartFrame, &error))
    {
        mLoopStartFrame = ctx.info->loopStartFrame;
        PopupMgr::instance()->addPopup({ error, ctx.asset });
        return;
    }

    ctx.info = &ctx.asset->getInfo();
    SetUnsavedChanges(true);
}

void AudioAssetPanel::draw(AudioAsset* asset)
{
    FrameContext ctx = beginFrame(asset);

    drawName(ctx);
    drawWaveformBox(ctx);
    drawLoopSection(ctx);
    drawAudioBox(ctx);

    if (asset->isStream())
        drawStreamBox(ctx);

    if (!asset->getMeta().empty())
        drawMetadataBox(ctx);
}

void AudioAssetPanel::drawName(const FrameContext& ctx)
{
    if (ActiveAudioResource().canRenameAsset(*ctx.asset))
    {
        DrawItemPropertiesUI(true);
        return;
    }

    PropertyRow(messages::common::cName);
    ImGui::TextUnformatted(ctx.asset->getName().cstr());
}

void AudioAssetPanel::drawWaveCanvas(FrameContext& ctx)
{
    sead::FixedSafeString<96> readout;

    if (ctx.info->isLoop && mLoopEndFrame > mLoopStartFrame)
    {
        u32 minutes = 0;
        f64 seconds = 0.0;
        SplitSampleTime(mLoopEndFrame - mLoopStartFrame, ctx.info->sampleRate, &minutes, &seconds);

        readout.format(messages::wavefile::cLoopReadoutFormat, minutes, seconds);
    }

    const f32 playheadSample = ctx.playing ? static_cast<f32>(sSoundPlayer.getPlaySamplePosition(true)) : -1.0f;
    const ImGui::LoopWaveformState::Dragging dragBefore = mState.dragging;
    f64 seekSample = -1.0;

    ImGui::LoopWaveformEditor("asset", mState, mMono, ctx.info->sampleRate, mLoopStartFrame, mLoopEndFrame, playheadSample, ctx.info->isLoop, ctx.canSetLoop,
                              readout.cstr(), ImVec2(0.0f, ImFloor(ImGui::GetFontSize() * cWaveViewHeightEm)), true, &seekSample);

    if (dragBefore == ImGui::LoopWaveformState::Dragging::Start && mState.dragging != dragBefore && mLoopStartFrame != ctx.info->loopStartFrame)
        commitLoop(ctx, true, mLoopStartFrame);

    if (seekSample >= 0.0 && ctx.playing && sSoundPlayer.getSampleCount() > 0)
        sSoundPlayer.seek(sead::Mathf::clamp2(0.0f, static_cast<f32>(seekSample / static_cast<f64>(sSoundPlayer.getSampleCount())), 1.0f));
}

void AudioAssetPanel::drawWaveformBox(FrameContext& ctx)
{
    if (!BeginPropertyBox("AssetWave"))
        return;

    const f32 buttonSize = ImGui::GetFrameHeight();
    const ImVec2 headerPos = ImGui::GetCursorScreenPos();

    RegionHeader(messages::wavefile::cWaveform, buttonSize + ImGui::GetStyle().ItemSpacing.x);

    ImGui::SameLine();
    ImGui::SetCursorScreenPos(ImVec2(headerPos.x + ImGui::GetContentRegionAvail().x - buttonSize, headerPos.y));

    sead::FixedSafeString<SoundPlayer::cPlaybackErrorLength> playbackError;
    const bool canPlay = !ctx.asset->getPlaybackError(&playbackError);

    ImGui::BeginDisabled(!canPlay);

    if (PlayToggleButton(ctx.playing ? TransportGlyph::Stop : TransportGlyph::Play, "###Play", ImVec2(buttonSize, buttonSize), ctx.playing))
    {
        const WaveFile* wave = ctx.playing ? nullptr : ctx.asset->getPreviewWave();

        if (ctx.playing)
            sSoundPlayer.fadeOutPlayers();
        else if (wave)
            sSoundPlayer.playWaveFile(*wave, -1, nullptr, 0, false);
    }

    ImGui::EndDisabled();
    ImGui::SetItemTooltip("%s", canPlay ? (ctx.playing ? messages::common::cStop : messages::common::cPlay) : playbackError.cstr());

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImFloor(ImGui::GetFontSize() * cWaveTitleGapEm));

    if (mMono.empty())
        ImGui::TextDisabled("%s", mTooLong ? messages::wavefile::cTooLong : (canPlay ? messages::wavefile::cNoData : playbackError.cstr()));
    else
        drawWaveCanvas(ctx);

    EndPropertyBox();
}

void AudioAssetPanel::drawLoopSection(FrameContext& ctx)
{
    bool isLoop = ctx.info->isLoop;
    bool toggled = false;

    const sead::FormatFixedSafeString<64> title("%s###AssetLoop", messages::common::cLoop);
    const char* state = ctx.canSetLoop ? nullptr : (isLoop ? messages::common::cOn : messages::common::cOff);
    const bool open = BeginPropertySection(title.cstr(), ctx.canSetLoop ? &isLoop : nullptr, &toggled, true, 1, nullptr, state);

    if (toggled)
        commitLoop(ctx, isLoop, ctx.info->loopStartFrame);

    if (!open)
        return;

    if (!ctx.canSetLoop)
    {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", ctx.loopReason);
        ImGui::PopTextWrapPos();
    }

    if (!ctx.canSetLoop && !ctx.info->isLoop)
    {
        EndPropertySection();
        return;
    }

    ImGui::BeginDisabled(!ctx.canSetLoop || !ctx.info->isLoop);

    const u32 minFrame = 0;
    const u32 maxFrame = ctx.info->sampleCount > 0 ? ctx.info->sampleCount - 1 : 0;
    u32 loopStartFrame = mLoopStartFrame;

    PropertyRow(messages::common::cStart);
    ImGui::SetNextItemWidth(ImFloor(ImGui::GetFontSize() * cLoopFieldWidthEm));
    DragField("##LoopStart", ImGuiDataType_U32, &loopStartFrame, sead::Mathf::max(1.0f, static_cast<f32>(maxFrame) / cLoopDragRange), &minFrame, &maxFrame);

    const bool commit = ImGui::IsItemDeactivatedAfterEdit();
    mLoopStartFrame = loopStartFrame;

    ImGui::SameLine();
    ImGui::TextDisabled("%s", messages::common::cSamples);

    ImGui::EndDisabled();

    if (commit && loopStartFrame != ctx.info->loopStartFrame)
        commitLoop(ctx, true, loopStartFrame);

    PropertyRow(messages::common::cEnd, ctx.info->kind == AudioAssetKind::Bwav ? messages::resource::cLoopEndFixed : nullptr);
    ImGui::TextDisabled(messages::wavefile::cSamplesFormat, ctx.info->sampleCount);

    EndPropertySection();
}

void AudioAssetPanel::drawAudioBox(const FrameContext& ctx)
{
    if (!BeginPropertyBox("AssetAudio"))
        return;

    const AudioAssetInfo& info = *ctx.info;

    RegionHeader(messages::common::cFormat);

    PropertyRow(messages::common::cFormat);
    ImGui::TextUnformatted(GetAudioAssetKindName(info.kind));

    if (info.channelCount > 0)
    {
        PropertyRow(messages::common::cSampleRate);
        ImGui::Text(messages::unit::cHertzFormat, info.sampleRate);

        DrawSampleTimeRow(messages::common::cLength, info.sampleCount, info.sampleRate);

        if (info.isPrefetch)
            DrawSampleTimeRow(messages::resource::cStoredInArchive, info.prefetchSampleCount, info.sampleRate);

        sead::FixedSafeString<24> channels;
        FormatChannelCount(&channels, info.channelCount);

        PropertyRow(messages::common::cChannels);
        ImGui::TextUnformatted(channels.cstr());
    }

    sead::FixedSafeString<32> size;
    FormatItemSize(&size, info.fileSize);

    PropertyRow(messages::common::cSize);
    ImGui::TextUnformatted(size.cstr());

    EndPropertyBox();
}

void AudioAssetPanel::drawStreamBox(const FrameContext& ctx)
{
    if (!BeginPropertyBox("AssetStream"))
        return;

    const std::string path = ActiveAudioResource().getStreamFilePath(*ctx.asset);

    RegionHeader(messages::sound::cStreamFileLabel);

    PropertyRow(messages::resource::cStreamState);
    ImGui::TextUnformatted(GetStreamStateText(*ctx.asset));
    ImGui::SetItemTooltip("%s", path.c_str());

    EndPropertyBox();
}

void AudioAssetPanel::drawMetadataBox(const FrameContext& ctx)
{
    if (!BeginPropertyBox("AssetMeta"))
        return;

    sead::FixedSafeString<16> version;
    FormatResourceVersion(&version, ctx.asset->getMetaVersion());

    sead::FixedSafeString<32> size;
    FormatItemSize(&size, static_cast<u32>(ctx.asset->getMeta().size()));

    RegionHeader(messages::resource::cMetadataTitle);

    PropertyRow(messages::common::cVersion);
    ImGui::Text(messages::resource::cMetadataVersionFormat, version.cstr());

    PropertyRow(messages::common::cSize);
    ImGui::TextUnformatted(size.cstr());

    EndPropertyBox();
}

void DrawAudioAssetPropertiesUI(AudioAsset* asset)
{
    sPanel.draw(asset);
}

void ReleaseAudioAssetView()
{
    sPanel.release();
}
