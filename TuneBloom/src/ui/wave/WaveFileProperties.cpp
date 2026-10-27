#include <bfsar/files/WaveFile.h>

#include <ui/widgets/Dialogs.h>
#include <ui/file/File.h>
#include <ui/items/ItemColumns.h>
#include <ui/items/ItemList.h>
#include <ui/widgets/Knob.h>
#include <ui/wave/LoopWaveformEditor.h>
#include <ui/wave/WaveformBox.h>
#include <ui/Messages.h>
#include <ui/player/PlayerViews.h>
#include <ui/widgets/Properties.h>
#include <ui/stream/StreamRegions.h>
#include <ui/file/UnsavedChanges.h>
#include <ui/wave/WaveFormatUI.h>
#include <ui/widgets/Widgets.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_internal.h>

#include <bfsar/audio/AudioProcessing.h>
#include <bfsar/audio/WaveFileEditDecode.h>

namespace
{

struct WaveLoopEdit
{
    bool& isLoop;
    u32& loopStartFrame;
    u32& loopEndFrame;
    u32 sampleCount;
    bool* isLoopDirty;
    bool changed = false;
};

struct WaveFileView
{
    const WaveFile* wave = nullptr;
    const void* data = nullptr;
    const void* fullData = nullptr;
    u32 sampleCount = 0;
    u32 channelNum = 0;
    WaveFile::Encoding encoding = WaveFile::Encoding::DspAdpcm;

    std::vector<float> mono;
    bool tooLong = false;

    ImGui::LoopWaveformState state;
    u32 loopStartFrame = 0;
    u32 loopEndFrame = 0;
    bool fieldPending = false;

    bool fillHeight = false;
    f32 canvasBottomY = 0.0f;
    f32 canvasExtraHeight = 0.0f;
    f32 belowCanvasHeight = 0.0f;
};

}

static constexpr f32 cWaveViewHeightEm   = 7.0f;
static constexpr u32 cWaveViewMaxSamples = 12000000;

static WaveFileView* sWaveFileView = nullptr;

static void CapWaveLoopStart(WaveLoopEdit& edit)
{
    if (edit.loopStartFrame >= edit.loopEndFrame)
    {
        edit.loopStartFrame = edit.loopEndFrame - 1;
        edit.changed = true;
    }
}

static void CapWaveLoopEnd(WaveLoopEdit& edit)
{
    if (edit.loopEndFrame > edit.sampleCount)
    {
        edit.loopEndFrame = edit.sampleCount;
        edit.changed = true;
    }
    else if (edit.loopEndFrame < 1)
    {
        edit.loopEndFrame = 1;
        edit.changed = true;
    }
}

static void SetWaveLoopFlag(WaveLoopEdit& edit, bool isLoop)
{
    edit.changed = true;
    edit.isLoop = isLoop;

    if (edit.isLoopDirty && (edit.loopStartFrame != 0 || edit.loopEndFrame < WaveFile::cStreamMinimumLoopFrames))
        *edit.isLoopDirty = true;
}

static void SetWaveLoopStart(WaveLoopEdit& edit, u32 loopStartFrame, bool commit)
{
    if (loopStartFrame >= edit.sampleCount)
        loopStartFrame = edit.sampleCount - 1;

    if (edit.isLoop && loopStartFrame != edit.loopStartFrame)
    {
        edit.changed = true;
        edit.loopStartFrame = loopStartFrame;
    }

    if (!commit)
        return;

    if (edit.isLoopDirty)
        *edit.isLoopDirty = true;

    if (edit.loopEndFrame <= edit.loopStartFrame)
    {
        edit.loopEndFrame = edit.loopStartFrame + 1;
        edit.changed = true;
    }

    CapWaveLoopEnd(edit);
    CapWaveLoopStart(edit);
}

static void SetWaveLoopEnd(WaveLoopEdit& edit, u32 loopEndFrame, bool commit)
{
    if (loopEndFrame > edit.sampleCount)
        loopEndFrame = edit.sampleCount;
    else if (loopEndFrame < 1)
        loopEndFrame = 1;

    if (edit.isLoop && loopEndFrame != edit.loopEndFrame)
    {
        edit.changed = true;
        edit.loopEndFrame = loopEndFrame;
    }

    if (!edit.isLoop || !commit)
        return;

    if (edit.isLoopDirty)
        *edit.isLoopDirty = true;

    CapWaveLoopStart(edit);
}

void ReleaseWaveFileView()
{
    delete sWaveFileView;
    sWaveFileView = nullptr;
}

void ForgetWaveFileView(const WaveFile* wave)
{
    if (sWaveFileView && sWaveFileView->wave == wave)
        ReleaseWaveFileView();
}

static WaveFileView& GetWaveFileView(const WaveFile& wave)
{
    if (!sWaveFileView)
        sWaveFileView = new WaveFileView();

    WaveFileView& view = *sWaveFileView;

    const WaveFile::Channel* channel = wave.getChannels().isEmpty() ? nullptr : wave.getChannels().nth(0);
    const void* data = channel ? channel->getData() : nullptr;
    const void* fullData = channel ? channel->getFullData_() : nullptr;
    const u32 channelNum = static_cast<u32>(wave.getChannels().size());

    if (view.wave == &wave && view.data == data && view.fullData == fullData && view.sampleCount == wave.getSampleCount() && view.channelNum == channelNum && view.encoding == wave.getEncoding())
        return view;

    if (view.wave != &wave)
    {
        view.state = ImGui::LoopWaveformState{};
        view.state.grabMarkers = true;
        view.fieldPending = false;
    }

    view.wave = &wave;
    view.data = data;
    view.fullData = fullData;
    view.sampleCount = wave.getSampleCount();
    view.channelNum = channelNum;
    view.encoding = wave.getEncoding();
    view.tooLong = view.sampleCount > cWaveViewMaxSamples;
    view.state.dataRevision++;
    view.mono.clear();

    if (view.tooLong)
        return view;

    DecodedPcm pcm = decodeWaveFileForEditing(wave);

    if (!pcm.isValid())
        return view;

    AudioProcessing::mixToMono(pcm.channels, view.mono);

    return view;
}

static bool IsWaveLoopDragging(const ImGui::LoopWaveformState& state)
{
    return state.dragging == ImGui::LoopWaveformState::Dragging::Start || state.dragging == ImGui::LoopWaveformState::Dragging::End;
}

static bool DrawWaveFormatBox(const WaveFile& wave, WaveFile::Encoding* outEncoding)
{
    bool encodingChanged = false;

    if (!BeginPropertyBox("WaveFormat"))
        return false;

    RegionHeader(messages::common::cFormat);

    PropertyRow(messages::common::cEncoding);
    ImGui::SetNextItemWidth(-FLT_MIN);

    s32 encoding = static_cast<s32>(wave.getEncoding());

    if (ComboScroll("##Encoding", &encoding, WaveFile::sEncodingTypes, cWaveEncodingDetails, IM_ARRAYSIZE(WaveFile::sEncodingTypes)) && encoding != static_cast<s32>(wave.getEncoding()))
    {
        *outEncoding = static_cast<WaveFile::Encoding>(encoding);
        encodingChanged = true;
    }

    PropertyRow(messages::common::cSampleRate);
    ImGui::Text(messages::unit::cHertzFormat, wave.getSampleRate());

    u32 minutes = 0;
    f64 seconds = 0.0;
    SplitSampleTime(wave.getSampleCount(), wave.getSampleRate(), &minutes, &seconds);

    PropertyRow(messages::common::cLength);
    ImGui::Text(messages::wavefile::cTimeFormat, minutes, seconds);
    ImGui::SameLine(0.0f, ImGui::GetFontSize());
    ImGui::TextDisabled(messages::wavefile::cSamplesFormat, wave.getSampleCount());

    sead::FixedSafeString<24> channels;
    FormatWaveFileChannels(&wave, &channels);

    PropertyRow(messages::common::cChannels);
    ImGui::TextUnformatted(channels.cstr());

    EndPropertyBox();

    return encodingChanged;
}

static void DrawWavePlayButtons(const WaveFile& wave, bool playing, f32 buttonSize)
{
    const u32 channelNum = wave.getChannels().size();

    sead::FixedSafeString<SoundPlayer::cPlaybackErrorLength> playbackError;
    const bool canPlay = !SoundPlayer::GetWaveFilePlaybackError(wave, &playbackError);

    const auto buttonTooltip = [&](const char* text)
    {
        ImGui::SetItemTooltip("%s", canPlay ? text : playbackError.cstr());
    };

    ImGui::BeginDisabled(!canPlay);

    if (PlayToggleButton(playing ? TransportGlyph::Stop : TransportGlyph::Play, "###Play", ImVec2(buttonSize, buttonSize), playing))
    {
        if (playing)
            sSoundPlayer.fadeOutPlayers();
        else
            sSoundPlayer.playWaveFile(wave);
    }

    buttonTooltip(playing ? messages::common::cStop : messages::common::cPlay);

    for (u32 i = 0; channelNum > 1 && i < channelNum; i++)
    {
        static const char* const sChannelLabels[snd::cWaveChannelMax] = { messages::common::cLeft, messages::common::cRight };
        static const char* const sChannelTips[snd::cWaveChannelMax] = { messages::wavefile::cPlayLeft, messages::wavefile::cPlayRight };

        ImGui::SameLine();
        ImGui::PushID(static_cast<s32>(i));

        if (ImGui::Button(sChannelLabels[i], ImVec2(buttonSize, buttonSize)))
            sSoundPlayer.playWaveFile(wave, static_cast<s32>(i));

        buttonTooltip(sChannelTips[i]);

        ImGui::PopID();
    }

    ImGui::EndDisabled();
}

static void DrawWaveCanvas(WaveFile& wave, WaveFileView& view, WaveLoopEdit& edit, bool playing)
{
    const u32 sampleRate = wave.getSampleRate();
    f32 playheadSample = -1.0f;

    if (playing)
        playheadSample = static_cast<f32>(sSoundPlayer.getPlaySamplePosition(true));

    const ImGui::LoopWaveformState::Dragging dragBefore = view.state.dragging;
    f64 seekSample = -1.0;

    if (!wave.getStreamFileLayout().regions.empty())
        DrawStreamRegionLane(&wave, view.state);

    const f32 canvasTop = ImGui::GetCursorScreenPos().y;
    const f32 contentBottom = ImGui::GetWindowPos().y + ImGui::GetWindowContentRegionMax().y;
    const f32 minHeight = ImFloor(ImGui::GetFontSize() * cWaveViewHeightEm);
    const f32 fillHeight = ImFloor(contentBottom - canvasTop - view.canvasExtraHeight - view.belowCanvasHeight);
    const f32 height = view.fillHeight ? sead::Mathf::max(minHeight, fillHeight) : minHeight;

    ImGui::LoopWaveformEditor("wave", view.state, view.mono, sampleRate, view.loopStartFrame, view.loopEndFrame, playheadSample, edit.isLoop, true,
                              nullptr, ImVec2(0.0f, height), true, &seekSample);

    view.canvasBottomY = ImGui::GetCursorScreenPos().y;
    view.canvasExtraHeight = view.canvasBottomY - canvasTop - height;

    if (!IsWaveLoopDragging(view.state))
    {
        if (dragBefore == ImGui::LoopWaveformState::Dragging::Start && view.loopStartFrame != edit.loopStartFrame)
            SetWaveLoopStart(edit, view.loopStartFrame, true);
        else if (dragBefore == ImGui::LoopWaveformState::Dragging::End && view.loopEndFrame != edit.loopEndFrame)
            SetWaveLoopEnd(edit, view.loopEndFrame, true);
    }

    if (seekSample >= 0.0 && playing && sSoundPlayer.getSampleCount() > 0)
        sSoundPlayer.seek(sead::Mathf::clamp2(0.0f, static_cast<f32>(seekSample / static_cast<f64>(sSoundPlayer.getSampleCount())), 1.0f));
}

static void DrawWaveLoopFields(WaveFileView& view, WaveLoopEdit& edit, u32 sampleRate)
{
    const LoopFieldsResult fields = DrawLoopFields(&view.loopStartFrame, &view.loopEndFrame, edit.sampleCount, sampleRate, edit.isLoop);

    if (fields.changed)
        view.fieldPending = true;

    if (fields.startCommit)
    {
        SetWaveLoopStart(edit, view.loopStartFrame, true);
        view.fieldPending = false;
    }

    if (fields.endCommit)
    {
        SetWaveLoopEnd(edit, view.loopEndFrame, true);
        view.fieldPending = false;
    }
}

static void DrawWaveViewBox(WaveFile& wave, WaveFileView& view, WaveLoopEdit& edit)
{
    if (!BeginPropertyBox("WaveView"))
        return;

    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 buttonSize = ImGui::GetFrameHeight();
    const u32 channelNum = wave.getChannels().size();
    const u32 buttonNum = channelNum > 1 ? 1 + channelNum : 1;
    const f32 buttonsWidth = buttonSize * buttonNum + style.ItemSpacing.x * (buttonNum - 1);
    const bool playing = sSoundPlayer.isActive() && sSoundPlayer.getPlayingWaveFile() == &wave;

    if (BeginWaveformHeader(buttonsWidth, edit.isLoop))
        SetWaveLoopFlag(edit, !edit.isLoop);

    DrawWavePlayButtons(wave, playing, buttonSize);

    EndWaveformHeader();

    if (view.mono.empty())
        ImGui::TextDisabled("%s", view.tooLong ? messages::wavefile::cTooLong : messages::wavefile::cNoData);
    else
        DrawWaveCanvas(wave, view, edit, playing);

    DrawWaveLoopFields(view, edit, wave.getSampleRate());

    EndPropertyBox();
}

void WaveFile::drawUI()
{
    DrawItemNameRow(this);
    drawEditorUI(false);
}

void WaveFile::drawEditorUI(bool fillHeight)
{
    const InnerFileTarget target = getTarget();

    mVersion = target.version;
    mEndian = target.endian;

    static Encoding sEncoding = Encoding::DspAdpcm;

    WaveFileView& view = GetWaveFileView(*this);
    WaveLoopEdit edit{ mIsLoop, mLoopStartFrame, mLoopEndFrame, mSampleCount, &mIsLoopDirty };

    if (!view.fieldPending && !IsWaveLoopDragging(view.state))
    {
        view.loopStartFrame = mLoopStartFrame;
        view.loopEndFrame = mLoopEndFrame;
    }

    const bool openEncodingPopup = DrawWaveFormatBox(*this, &sEncoding);

    view.fillHeight = fillHeight;

    DrawWaveViewBox(*this, view, edit);

    view.belowCanvasHeight = ImGui::GetCursorScreenPos().y - view.canvasBottomY;

    if (edit.changed)
    {
        disposeChannels_();
        SetUnsavedChanges(true);
    }

    if (openEncodingPopup)
        ImGui::OpenPopup("###ENCODING");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (BeginDialog(sead::FormatFixedSafeString<64>(ICON_LC_ALERT_TRIANGLE " %s###ENCODING", messages::common::cWarning).cstr(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("%s", messages::wavefile::cEncodingWarningDetail);

        BeginDialogFooter({messages::dialog::cConvert, messages::common::cCancel});

        if (DialogButton(messages::dialog::cConvert))
        {
            disposeChannels_();

            Encoding prev = mEncoding;
            mEncoding = sEncoding;
            SetUnsavedChanges(true);

            for (u32 i = 0; i < mChannels.size(); i++)
                mChannels.nth(i)->mSeekData.free();

            reencodeChannels_(prev);

            invalidateOriginalData_();
            refreshSerializedHash();

            ImGui::CloseCurrentPopup();
        }

        if (DialogButton(messages::common::cCancel))
        {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}
