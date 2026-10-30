#include <ui/resource/ResourceViews.h>

#include <ui/resource/AudioAssetLabels.h>
#include <ui/file/File.h>
#include <ui/items/ItemColumns.h>
#include <ui/items/ItemList.h>
#include <ui/widgets/Knob.h>
#include <ui/wave/LoopWaveformEditor.h>
#include <ui/wave/WaveformBox.h>
#include <messages/Messages.h>
#include <ui/player/PlayerViews.h>
#include <bfsar/model/MessageLog.h>
#include <ui/widgets/Properties.h>
#include <ui/file/UnsavedChanges.h>
#include <ui/wave/WaveFormatUI.h>
#include <ui/widgets/Widgets.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_internal.h>

#include <aal/Bwav.h>
#include <bfsar/audio/AudioProcessing.h>
#include <bfsar/audio/DecodedPcm.h>
#include <bfsar/files/WaveFile.h>

#include <vector>

static constexpr f32 cWaveViewHeightEm   = 7.0f;
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
    void drawLoopFields(FrameContext& ctx);
    void drawAudioBox(const FrameContext& ctx);
    void drawStreamBox(const FrameContext& ctx);
    void drawMetadataBox(const FrameContext& ctx);

    const AudioAsset* mAsset = nullptr;
    AudioAssetFile mSource;
    std::vector<float> mMono;
    bool mTooLong = false;
    ImGui::LoopWaveformState mState;
    AssetLabelView mLabels;
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
    mLabels = AssetLabelView{};
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
    RefreshAssetLabels(&mLabels, asset);

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
        MessageLog::instance()->addPopup({ error, ctx.asset });
        return;
    }

    ctx.info = &ctx.asset->getInfo();
    SetUnsavedChanges(true);
}

void AudioAssetPanel::draw(AudioAsset* asset)
{
    FrameContext ctx = beginFrame(asset);

    const u32 addFrame = ctx.playing ? static_cast<u32>(sSoundPlayer.getPlaySamplePosition(true)) : 0;

    drawName(ctx);
    drawWaveformBox(ctx);
    DrawAssetMarkersBox(&mLabels, asset, ctx.info->sampleCount, ctx.info->sampleRate, addFrame);
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
    const f32 playheadSample = ctx.playing ? static_cast<f32>(sSoundPlayer.getPlaySamplePosition(true)) : -1.0f;
    const ImGui::LoopWaveformState::Dragging dragBefore = mState.dragging;
    f64 seekSample = -1.0;

    SetAssetLabelCues(mLabels, &mState);

    ImGui::LoopWaveformEditor("asset", mState, mMono, ctx.info->sampleRate, mLoopStartFrame, mLoopEndFrame, playheadSample, ctx.info->isLoop, ctx.canSetLoop,
                              nullptr, ImVec2(0.0f, ImFloor(ImGui::GetFontSize() * cWaveViewHeightEm)), true, &seekSample);

    ApplyAssetLabelCues(&mLabels, ctx.asset, mState);

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

    if (BeginWaveformHeader(buttonSize, ctx.info->isLoop, ctx.canSetLoop ? nullptr : ctx.loopReason))
        commitLoop(ctx, !ctx.info->isLoop, ctx.info->loopStartFrame);

    sead::FixedSafeString<SoundPlayer::cPlaybackErrorLength> playbackError;
    const bool canPlay = !ctx.asset->getPlaybackError(&playbackError);

    ImGui::BeginDisabled(!canPlay);

    if (PlayToggleButton(ctx.playing ? TransportGlyph::Stop : TransportGlyph::Play, "###Play", ImVec2(buttonSize, buttonSize), ctx.playing))
    {
        if (ctx.playing)
            sSoundPlayer.fadeOutPlayers();
        else
            PlayAudioAsset(ctx.asset);
    }

    ImGui::EndDisabled();
    ImGui::SetItemTooltip("%s", canPlay ? (ctx.playing ? messages::common::cStop : messages::common::cPlay) : playbackError.cstr());

    EndWaveformHeader();

    if (mMono.empty())
        ImGui::TextDisabled("%s", mTooLong ? messages::wavefile::cTooLong : (canPlay ? messages::wavefile::cNoData : playbackError.cstr()));
    else
        drawWaveCanvas(ctx);

    drawLoopFields(ctx);

    EndPropertyBox();
}

void AudioAssetPanel::drawLoopFields(FrameContext& ctx)
{
    u32 loopEndFrame = mLoopEndFrame;
    const LoopFieldsResult fields = DrawLoopFields(&mLoopStartFrame, &loopEndFrame, ctx.info->sampleCount, ctx.info->sampleRate, ctx.canSetLoop && ctx.info->isLoop,
                                                   messages::resource::cLoopEndFixed);

    if (fields.startCommit && mLoopStartFrame != ctx.info->loopStartFrame)
        commitLoop(ctx, true, mLoopStartFrame);
}

static const char* GetAssetEncodingName(AudioAsset* asset)
{
    aal::BwavCodec codec = aal::BwavCodec::DspAdpcm;

    if (GetAudioAssetBwavCodec(*asset, &codec))
        return cBwavCodecNames[static_cast<u32>(codec)];

    const WaveFile* wave = asset->getInfo().kind == AudioAssetKind::Fwav ? asset->getPreviewWave() : nullptr;

    return wave ? WaveFile::sEncodingTypes[static_cast<s32>(wave->getEncoding())] : nullptr;
}

void AudioAssetPanel::drawAudioBox(const FrameContext& ctx)
{
    if (!BeginPropertyBox("AssetAudio"))
        return;

    const AudioAssetInfo& info = *ctx.info;
    const char* encoding = GetAssetEncodingName(ctx.asset);

    RegionHeader(messages::common::cFormat);

    PropertyRow(messages::common::cFileType);
    ImGui::TextUnformatted(GetAudioAssetKindName(info.kind));

    if (encoding)
    {
        PropertyRow(messages::common::cEncoding);
        ImGui::TextUnformatted(encoding);
    }

    if (info.channelCount > 0)
    {
        PropertyRow(messages::common::cSampleRate);
        ImGui::Text(messages::unit::cHertzFormat, info.sampleRate);

        DrawSampleTimeRow(messages::common::cLength, info.sampleCount, info.sampleRate);

        if (ctx.asset->isStream())
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

    const AudioResource& resource = ActiveAudioResource();
    const bool isLoosePrefetch = resource.getContainer() == AudioResource::Container::Bwav;

    RegionHeader(messages::sound::cStreamFileLabel);

    PropertyRow(messages::resource::cStreamState);
    ImGui::TextUnformatted(isLoosePrefetch ? messages::resource::cStreamNeedsArchive : GetStreamStateText(*ctx.asset));

    if (!isLoosePrefetch)
        ImGui::SetItemTooltip("%s", resource.getStreamFilePath(*ctx.asset).c_str());

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

    DrawAssetMetadataTitle(&mLabels, ctx.asset);

    PropertyRow(messages::common::cVersion);
    ImGui::Text(messages::resource::cMetadataVersionFormat, version.cstr());

    PropertyRow(messages::common::cSize);
    ImGui::TextUnformatted(size.cstr());

    DrawAssetTagsRow(&mLabels, ctx.asset);
    DrawAssetKeyRows(&mLabels, ctx.asset);

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
