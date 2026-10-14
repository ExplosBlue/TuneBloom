#include <bfsar/files/WaveFile.h>

#include <ui/widgets/Dialogs.h>
#include <ui/file/File.h>
#include <ui/items/ItemColumns.h>
#include <ui/items/ItemList.h>
#include <ui/widgets/Knob.h>
#include <ui/wave/LoopWaveformEditor.h>
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
};

}

static constexpr f32 cWaveViewHeightEm    = 7.0f;
static constexpr f32 cWaveTitleGapEm      = 0.3f;
static constexpr f32 cWaveLoopFieldMinEm  = 3.5f;
static constexpr f32 cWaveLoopFieldMaxEm  = 7.0f;
static constexpr f32 cWaveLoopGroupGapEm  = 1.5f;
static constexpr f32 cWaveLoopDragRange   = 2000.0f;
static constexpr u32 cWaveViewMaxSamples  = 12000000;

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

static bool DrawWaveLoopField(const char* label, const char* id, f32 labelWidth, f32 fieldWidth, u32* frame, u32 min, u32 max, u32 sampleRate, bool* outCommit)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(0.0f, labelWidth - ImGui::CalcTextSize(label).x + ImGui::GetStyle().ItemSpacing.x);
    ImGui::SetNextItemWidth(fieldWidth);

    const bool changed = DragField(id, ImGuiDataType_U32, frame, sead::Mathf::max(1.0f, static_cast<f32>(max) / cWaveLoopDragRange), &min, &max);

    u32 minutes = 0;
    f64 seconds = 0.0;
    SplitSampleTime(*frame, sampleRate, &minutes, &seconds);
    KnobTooltip(messages::wavefile::cTimeFormat, minutes, seconds);

    *outCommit = ImGui::IsItemDeactivatedAfterEdit() || (changed && !ImGui::IsItemActive());

    return changed;
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
        if (!canPlay)
            SetDisabledTooltip(playbackError.cstr());
        else if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("%s", text);
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
    sead::FixedSafeString<96> readout;

    if (edit.isLoop && view.loopEndFrame > view.loopStartFrame)
    {
        u32 minutes = 0;
        f64 seconds = 0.0;
        SplitSampleTime(view.loopEndFrame - view.loopStartFrame, sampleRate, &minutes, &seconds);

        readout.format(wave.getIsLoopDirty() ? messages::wavefile::cLoopDirtyReadoutFormat : messages::wavefile::cLoopReadoutFormat, minutes, seconds);
    }

    f32 playheadSample = -1.0f;

    if (playing)
        playheadSample = static_cast<f32>(sSoundPlayer.getPlaySamplePosition(true));

    const ImGui::LoopWaveformState::Dragging dragBefore = view.state.dragging;
    f64 seekSample = -1.0;

    if (!wave.getStreamFileLayout().regions.empty())
        DrawStreamRegionLane(&wave, view.state);

    ImGui::LoopWaveformEditor("wave", view.state, view.mono, sampleRate, view.loopStartFrame, view.loopEndFrame, playheadSample, edit.isLoop, true,
                              readout.cstr(), ImVec2(0.0f, ImFloor(ImGui::GetFontSize() * cWaveViewHeightEm)), true, &seekSample);

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

static void DrawWaveViewBox(WaveFile& wave, WaveFileView& view, WaveLoopEdit& edit)
{
    if (!BeginPropertyBox("WaveView"))
        return;

    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 buttonSize = ImGui::GetFrameHeight();
    const u32 channelNum = wave.getChannels().size();
    const u32 buttonNum = channelNum > 1 ? 1 + channelNum : 1;
    const f32 buttonsWidth = buttonSize * buttonNum + style.ItemSpacing.x * (buttonNum - 1);
    const ImVec2 headerPos = ImGui::GetCursorScreenPos();
    const f32 buttonsX = headerPos.x + ImGui::GetContentRegionAvail().x - buttonsWidth;
    const bool playing = sSoundPlayer.isActive() && sSoundPlayer.getPlayingWaveFile() == &wave;

    RegionHeader(messages::wavefile::cWaveform, buttonsWidth + style.ItemSpacing.x);

    ImGui::SameLine();
    ImGui::SetCursorScreenPos(ImVec2(buttonsX, headerPos.y));

    DrawWavePlayButtons(wave, playing, buttonSize);

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImFloor(ImGui::GetFontSize() * cWaveTitleGapEm));

    if (view.mono.empty())
        ImGui::TextDisabled("%s", view.tooLong ? messages::wavefile::cTooLong : messages::wavefile::cNoData);
    else
        DrawWaveCanvas(wave, view, edit, playing);

    EndPropertyBox();
}

static void DrawWaveLoopFields(const WaveFile& wave, WaveFileView& view, WaveLoopEdit& edit)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 em = ImGui::GetFontSize();
    const u32 sampleCount = edit.sampleCount;
    const u32 sampleRate = wave.getSampleRate();

    const f32 labelWidth = sead::Mathf::max(ImGui::CalcTextSize(messages::common::cStart).x, ImGui::CalcTextSize(messages::common::cEnd).x);
    const f32 unitWidth = style.ItemSpacing.x + ImGui::CalcTextSize(messages::common::cSamples).x;
    const f32 groupGap = ImFloor(em * cWaveLoopGroupGapEm);
    const f32 avail = ImGui::GetContentRegionAvail().x;
    const f32 fieldMin = ImFloor(em * cWaveLoopFieldMinEm);
    const f32 oneLineWidth = (avail - (labelWidth + style.ItemSpacing.x) * 2.0f - groupGap - unitWidth) * 0.5f;
    const bool oneLine = oneLineWidth >= fieldMin;
    const f32 fieldWidth = oneLine ? sead::Mathf::min(ImFloor(oneLineWidth), ImFloor(em * cWaveLoopFieldMaxEm)) : sead::Mathf::max(fieldMin, avail - labelWidth - style.ItemSpacing.x - unitWidth);

    bool commit = false;

    if (DrawWaveLoopField(messages::common::cStart, "##LoopStart", oneLine ? ImGui::CalcTextSize(messages::common::cStart).x : labelWidth, fieldWidth, &view.loopStartFrame, 0, sampleCount > 0 ? sampleCount - 1 : 0, sampleRate, &commit))
        view.fieldPending = true;

    if (commit)
    {
        SetWaveLoopStart(edit, view.loopStartFrame, true);
        view.fieldPending = false;
    }

    if (oneLine)
        ImGui::SameLine(0.0f, groupGap);

    if (DrawWaveLoopField(messages::common::cEnd, "##LoopEnd", oneLine ? ImGui::CalcTextSize(messages::common::cEnd).x : labelWidth, fieldWidth, &view.loopEndFrame, 1, sead::Mathu::max(sampleCount, 1u), sampleRate, &commit))
        view.fieldPending = true;

    if (commit)
    {
        SetWaveLoopEnd(edit, view.loopEndFrame, true);
        view.fieldPending = false;
    }

    ImGui::SameLine();
    ImGui::TextDisabled("%s", messages::common::cSamples);
}

static void DrawWaveLoopSection(const WaveFile& wave, WaveFileView& view, WaveLoopEdit& edit)
{
    bool isLoop = edit.isLoop;
    bool toggled = false;
    const bool open = BeginPropertySection(sead::FormatFixedSafeString<64>("%s###Loop", messages::common::cLoop).cstr(), &isLoop, &toggled);

    if (toggled)
        SetWaveLoopFlag(edit, isLoop);

    if (!open)
        return;

    DrawWaveLoopFields(wave, view, edit);
    EndPropertySection();
}

void WaveFile::drawUI()
{
    mVersion = sBfsar.getVersionForBfwav();
    mEndian = sBfsar.getEndian();

    static Encoding sEncoding = Encoding::DspAdpcm;

    WaveFileView& view = GetWaveFileView(*this);
    WaveLoopEdit edit{ mIsLoop, mLoopStartFrame, mLoopEndFrame, mSampleCount, &mIsLoopDirty };

    if (!view.fieldPending && !IsWaveLoopDragging(view.state))
    {
        view.loopStartFrame = mLoopStartFrame;
        view.loopEndFrame = mLoopEndFrame;
    }

    DrawItemNameRow(this);

    const bool openEncodingPopup = DrawWaveFormatBox(*this, &sEncoding);

    DrawWaveViewBox(*this, view, edit);
    DrawWaveLoopSection(*this, view, edit);

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
