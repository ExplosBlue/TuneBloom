#define IMGUI_DEFINE_MATH_OPERATORS
#include <ui/export/Export.h>

#include <ui/widgets/Dialogs.h>
#include <ui/export/ExportInternal.h>
#include <ui/file/FileDialogs.h>
#include <ui/items/ItemColumns.h>
#include <ui/widgets/Knob.h>
#include <ui/Messages.h>
#include <ui/player/PlayerViews.h>
#include <ui/widgets/Properties.h>
#include <ui/project/References.h>
#include <ui/widgets/Widgets.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_internal.h>

#include <bfsar/model/Sound.h>
#include <bfsar/audio/WavWriter.h>
#include <bfsar/files/WaveFile.h>
#include <midi/SeqMidiExporter.h>

#include <snd/HardwareMgr.h>

#include <vector>

static constexpr s32 cExportLoopCountDefault = 2;
static constexpr f32 cExportFadeSecDefault   = 10.0f;
static constexpr s32 cExportNoEndSecsDefault = 120;

namespace
{

struct SoundExportSettings
{
    s32 loopCount = cExportLoopCountDefault;
    f32 fadeSec = cExportFadeSecDefault;
    s32 noEndSecs = cExportNoEndSecsDefault;
    s32 sampleRateIndex = 0;
    s32 filesIndex = 0;
    bool loopMarkers = true;
};

}

static std::vector<Sound*> sPendingSounds;
static SoundExportSettings sSettings;

static const char* sSampleRateItems[] = {
    messages::exporting::cSampleRateOriginal,
    messages::unit::cSampleRate8000,
    messages::unit::cSampleRate11025,
    messages::unit::cSampleRate16000,
    messages::unit::cSampleRate22050,
    messages::unit::cSampleRate32000,
    messages::unit::cSampleRate44100,
    messages::unit::cSampleRate48000,
    messages::unit::cSampleRate96000
};
static constexpr u32 cSampleRateValues[] = {
    0,
    8000,
    11025,
    16000,
    22050,
    32000,
    44100,
    48000,
    96000
};
static const char* sSampleRateDetails[IM_ARRAYSIZE(sSampleRateItems)] = {
    messages::exporting::cSampleRateOriginalDetail
};
static const char* sExportFilesItems[] = {
    messages::exporting::cFilesOne,
    messages::exporting::cFilesEachTrack
};
static const char* sExportFilesDetails[] = {
    messages::exporting::cFilesOneDetail,
    messages::exporting::cFilesEachTrackDetail
};
static const char* sExportSeqFilesItems[] = {
    messages::exporting::cFilesSeqOne,
    messages::exporting::cFilesEachTrack
};
static const char* sExportSeqFilesDetails[] = {
    messages::exporting::cFilesSeqOneDetail,
    messages::exporting::cFilesSeqEachTrackDetail
};

void RequestExportSounds(const std::vector<Sound*>& sounds)
{
    HideExportResult();
    sPendingSounds = sounds;
}

void CancelSoundExport()
{
    sPendingSounds.clear();
    CancelSoundExportRun();
}

static SoundExportOptions MakeSoundExportOptions()
{
    SoundExportOptions options;
    options.loopCount = static_cast<u32>(sSettings.loopCount);
    options.fadeSec = sSettings.fadeSec;
    options.noEndSecs = static_cast<u32>(sSettings.noEndSecs);
    options.sampleRate = cSampleRateValues[sSettings.sampleRateIndex];
    options.multiChannel = sSettings.filesIndex == 0;
    options.splitSeqTracks = sSettings.filesIndex != 0;
    options.loopMarkers = sSettings.loopMarkers;

    return options;
}

namespace
{

struct SoundExportSummary
{
    s32 loopNum = 0;
    s32 noEndNum = 0;

    // Values of the last sound
    f64 introSec = 0.0;
    f64 loopSec = 0.0;
    f64 lengthSec = 0.0;
    u32 sampleRate = 0;
    u32 channelNum = 0;
    u32 trackNum = 0;
    bool isSequence = false;
};

}

static constexpr f32 cExportFieldWidthEm  = 6.0f;
static constexpr f32 cExportListWidthEm   = 13.0f;

static constexpr s32 cExportLoopCountMin = 1;
static constexpr s32 cExportLoopCountMax = 99;
static constexpr f32 cExportFadeSecMin   = 0.0f;
static constexpr f32 cExportFadeSecMax   = 60.0f;
static constexpr s32 cExportNoEndSecsMin = 1;
static constexpr s32 cExportNoEndSecsMax = 3600;

static SoundExportSummary sExportSummary;

static SoundExportSummary CalcSoundExportSummary()
{
    SoundExportSummary summary;

    for (const Sound* sound : sPendingSounds)
    {
        if (sound->getSoundType() == Sound::SoundType::Seq)
        {
            const SeqTimeline& timeline = sSoundPlayer.getSeqTimeline(*sound);
            const f32 tempoRatio = sSoundPlayer.getSeqTempoRatio();
            const f64 timeScale = tempoRatio > 0.0f ? 1.0 / tempoRatio : 1.0;

            summary.sampleRate = snd::internal::driver::HardwareMgr::cSampleRate;
            summary.channelNum = snd::internal::driver::HardwareMgr::cChannelCount;
            summary.trackNum = 0;
            summary.isSequence = true;

            u16 trackFlags = 0;
            for (const SeqMidiPart& part : collectSeqMidiContents(*sound).parts)
                trackFlags |= 1 << part.trackNo;

            for (; trackFlags != 0; trackFlags &= trackFlags - 1)
                summary.trackNum++;

            if (!timeline.hasLength())
            {
                summary.noEndNum++;
                continue;
            }

            const f64 endSec = timeline.calcMsec(timeline.endTick) / 1000.0 * timeScale;

            if (timeline.kind == SeqTimeline::Kind::Loop)
            {
                summary.loopNum++;
                summary.introSec = timeline.calcMsec(timeline.loopStartTick) / 1000.0 * timeScale;
                summary.loopSec = endSec - summary.introSec;
            }
            else
                summary.lengthSec = endSec;
        }
        else if (sound->getSoundType() == Sound::SoundType::Strm)
        {
            const WaveFile* wave = FindStreamExportWave(sound);

            if (!wave)
                continue;

            const f64 sampleRate = wave->getSampleRate();
            const auto& trackList = sound->getStreamSoundInfo().getTrackList();

            summary.sampleRate = wave->getSampleRate();
            summary.channelNum = 0;
            summary.trackNum = 0;

            for (auto it = trackList.robustBegin(); it != trackList.robustEnd(); ++it)
            {
                const Item* trackWave = static_cast<const Sound::StreamSoundInfo::Track*>(it->val())->getWaveFileRef().getItem();

                if (!trackWave)
                    continue;

                summary.channelNum += static_cast<const WaveFile*>(trackWave)->getChannels().size();
                summary.trackNum++;
            }

            if (HasExportLoop(wave))
            {
                summary.loopNum++;
                summary.introSec = wave->getOriginalLoopStartFrame() / sampleRate;
                summary.loopSec = wave->getLoopFrames() / sampleRate;
            }
            else
                summary.lengthSec = wave->getSampleCount() / sampleRate;
        }
    }

    return summary;
}

static sead::FixedSafeString<16> FormatExportTime(f64 seconds, bool tenths = true)
{
    const u32 total = static_cast<u32>(seconds * 10.0 + 0.5);

    sead::FixedSafeString<16> out;

    if (tenths)
        out.format(messages::exporting::cTimeTenthsFormat, total / 600, total / 10 % 60, total % 10);
    else
        out.format(messages::exporting::cTimeFormat, total / 600, total / 10 % 60);

    return out;
}

static constexpr f32 cExportTimelineHeightEm = 1.5f;
static constexpr f32 cExportSegmentPadEm     = 0.6f;
static constexpr f32 cExportLoopAlpha        = 0.60f;
static constexpr f32 cExportLoopAlphaAlt     = 0.42f;

static f64 DrawExportTimeline(const SoundExportSummary& summary)
{
    const f32 em = ImGui::GetFontSize();
    const bool isLoop = summary.loopNum > 0;
    const bool noEnd = summary.noEndNum > 0;

    const f64 fadeSec = isLoop ? sSettings.fadeSec : noEnd ? ImMin<f64>(sSettings.fadeSec, sSettings.noEndSecs) : 0.0;
    const f64 introSec = isLoop ? summary.introSec : noEnd ? sSettings.noEndSecs - fadeSec : summary.lengthSec;
    const f64 loopSec = isLoop ? summary.loopSec : 0.0;
    const s32 loopCount = isLoop ? sSettings.loopCount : 0;
    const f64 totalSec = introSec + loopSec * loopCount + fadeSec;

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImFloor(em * 0.4f));

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, ImFloor(em * cExportTimelineHeightEm));
    const f32 textY = pos.y + ImFloor((size.y - em) * 0.5f);

    ImGui::Dummy(size);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 baseColor = ImGui::GetColorU32(ImGuiCol_Button);
    const ImU32 gapColor = ImGui::GetColorU32(ImGuiCol_WindowBg);

    drawList->AddRectFilled(pos, pos + size, baseColor);

    auto calcX = [&](f64 sec)
    {
        return totalSec > 0.0 ? ImFloor(pos.x + size.x * static_cast<f32>(sec / totalSec)) : pos.x;
    };

    auto drawLabel = [&](f32 minX, f32 maxX, const char* label)
    {
        const f32 textWidth = ImGui::CalcTextSize(label).x;

        if (maxX - minX >= textWidth + em * cExportSegmentPadEm)
            drawList->AddText(ImVec2(ImFloor((minX + maxX - textWidth) * 0.5f), textY), ImGui::GetColorU32(ImGuiCol_Text), label);
    };

    f64 sec = introSec;

    if (isLoop)
        drawLabel(pos.x, calcX(sec), messages::exporting::cSegmentIntro);

    // Loop passes
    for (s32 i = 0; i < loopCount; i++)
    {
        const f32 minX = calcX(sec);
        const f32 maxX = calcX(sec + loopSec);

        drawList->AddRectFilled(ImVec2(minX, pos.y), ImVec2(maxX, pos.y + size.y), gapColor);
        drawList->AddRectFilled(ImVec2(minX, pos.y), ImVec2(maxX, pos.y + size.y), ImGui::GetColorU32(ImGuiCol_CheckMark, i % 2 == 0 ? cExportLoopAlpha : cExportLoopAlphaAlt));
        drawLabel(minX, maxX, sead::FormatFixedSafeString<16>(messages::exporting::cSegmentLoopFormat, i + 1).cstr());

        sec += loopSec;
    }

    // Fade
    if (fadeSec > 0.0)
    {
        const f32 minX = calcX(sec);
        const f32 maxX = pos.x + size.x;
        const ImU32 fromColor = isLoop ? ImGui::GetColorU32(ImGuiCol_CheckMark, loopCount % 2 == 0 ? cExportLoopAlpha : cExportLoopAlphaAlt) : baseColor;

        drawList->AddRectFilled(ImVec2(minX, pos.y), ImVec2(maxX, pos.y + size.y), gapColor);
        drawList->AddRectFilledMultiColor(ImVec2(minX, pos.y), ImVec2(maxX, pos.y + size.y), fromColor, 0, 0, fromColor);
        drawLabel(minX, maxX, messages::exporting::cSegmentFade);
    }

    if (isLoop && introSec > 0.0)
        drawList->AddLine(ImVec2(calcX(introSec), pos.y), ImVec2(calcX(introSec), pos.y + size.y), gapColor);

    // Times
    const sead::FixedSafeString<16> startText = FormatExportTime(0.0);
    const sead::FixedSafeString<16> totalText = FormatExportTime(totalSec);

    const f32 timesY = ImGui::GetCursorScreenPos().y;

    ImGui::TextDisabled("%s", startText.cstr());
    ImGui::SameLine();
    ImGui::SetCursorScreenPos(ImVec2(pos.x + size.x - ImGui::CalcTextSize(totalText.cstr()).x, timesY));
    ImGui::TextUnformatted(totalText.cstr());

    return totalSec;
}

static sead::FixedSafeString<128> FormatExportHeadDetail(const SoundExportSummary& summary, s32 seqCount, s32 strmCount, s32 waveCount)
{
    sead::FixedSafeString<128> detail;

    if (seqCount + strmCount + waveCount > 1)
    {
        const s32 counts[] = { seqCount, strmCount, waveCount };
        const char* formats[] = { messages::exporting::cHeadSequenceNumFormat, messages::exporting::cHeadStreamNumFormat, messages::exporting::cHeadWaveNumFormat };

        for (s32 i = 0; i < IM_ARRAYSIZE(counts); i++)
        {
            if (counts[i] == 0)
                continue;

            if (!detail.isEmpty())
                detail.append(messages::exporting::cHeadJoin);

            detail.appendWithFormat(formats[i], counts[i]);
        }

        return detail;
    }

    detail.copy(seqCount > 0 ? messages::item::cTypeSequenceSound : messages::item::cTypeStreamSound);

    if (summary.sampleRate != 0)
        detail.appendWithFormat(messages::exporting::cHeadHertzFormat, summary.sampleRate);

    if (summary.channelNum == 1)
        detail.append(messages::exporting::cHeadMono);
    else if (summary.channelNum == 2)
        detail.append(messages::exporting::cHeadStereo);
    else if (summary.channelNum > 2)
        detail.appendWithFormat(messages::exporting::cHeadChannelsFormat, summary.channelNum);

    if (summary.trackNum > 1)
        detail.appendWithFormat(messages::exporting::cHeadTracksFormat, summary.trackNum);

    return detail;
}

static f64 DrawExportTimelineBox(const SoundExportSummary& summary, s32 count)
{
    f64 totalSec = 0.0;

    if (!BeginGroupBox(messages::exporting::cTimelineBox))
        return totalSec;

    const f32 em = ImGui::GetFontSize();
    const bool single = count == 1;
    const ImVec4 valueColor = GetItemValueColor();
    const f32 labelWidth = ImFloor(em * cExportLabelWidthEm);
    const f32 fieldWidth = ImFloor(em * cExportFieldWidthEm);

    if (single)
        totalSec = DrawExportTimeline(summary);

    // Loop
    if (!single || summary.loopNum > 0 || summary.noEndNum > 0)
        GroupBoxRowLabel(messages::exporting::cLoop, labelWidth);

    if (!single)
        ImGui::TextColored(valueColor, messages::exporting::cLoopNumFormat, summary.loopNum, count);
    else if (summary.loopNum > 0)
        ImGui::TextColored(valueColor, messages::exporting::cLoopRangeFormat, FormatExportTime(summary.introSec).cstr(), FormatExportTime(summary.introSec + summary.loopSec).cstr());
    else if (summary.noEndNum > 0)
        ImGui::TextDisabled("%s", messages::exporting::cLoopNotFound);

    if (summary.loopNum > 0)
    {
        GroupBoxRowLabel(messages::exporting::cLoopCount, labelWidth);
        ImGui::SetNextItemWidth(fieldWidth);
        SetNextKnobReset(cExportLoopCountDefault);
        DragField("##LoopCount", ImGuiDataType_S32, &sSettings.loopCount, cPropertyDragSpeed, &cExportLoopCountMin, &cExportLoopCountMax);
        DetailTooltip(messages::exporting::cLoopCountDetail);
    }

    if (summary.loopNum > 0 || summary.noEndNum > 0)
    {
        GroupBoxRowLabel(messages::exporting::cFadeOut, labelWidth);
        ImGui::SetNextItemWidth(fieldWidth);
        SetNextKnobReset(cExportFadeSecDefault);
        DragField("##FadeOut", ImGuiDataType_Float, &sSettings.fadeSec, cPropertyDragSpeed, &cExportFadeSecMin, &cExportFadeSecMax, messages::exporting::cFadeOutFormat);
        DetailTooltip(messages::exporting::cFadeOutDetail);
    }

    // Length
    if (summary.noEndNum > 0)
    {
        GroupBoxRowLabel(messages::exporting::cLength, labelWidth);
        ImGui::SetNextItemWidth(fieldWidth);
        SetNextKnobReset(cExportNoEndSecsDefault);
        SetNextKnobText(FormatExportTime(sSettings.noEndSecs, false).cstr());
        DragField("##Length", ImGuiDataType_S32, &sSettings.noEndSecs, 1.0f, &cExportNoEndSecsMin, &cExportNoEndSecsMax);
        DetailTooltip(messages::exporting::cNoEndLengthDetail);
    }

    EndGroupBox();

    return totalSec;
}

static u32 CalcExportFileSize(const SoundExportSummary& summary, f64 totalSec)
{
    const u32 targetRate = cSampleRateValues[sSettings.sampleRateIndex];
    const u64 sampleCount = static_cast<u64>(totalSec * (targetRate != 0 ? targetRate : summary.sampleRate));
    const bool hasLoop = sSettings.loopMarkers && summary.loopNum > 0;
    const u32 fileNum = summary.isSequence && sSettings.filesIndex != 0 && summary.trackNum > 1 ? summary.trackNum : 1;

    return CalcPcm16WavSize(sampleCount, summary.channelNum, hasLoop) * fileNum;
}

static void DrawExportFormatBox(const SoundExportSummary& summary, bool single, bool hasStream, bool hasSequence, f64 totalSec)
{
    if (!BeginGroupBox(messages::exporting::cFormatBox))
        return;

    const f32 em = ImGui::GetFontSize();
    const f32 labelWidth = ImFloor(em * cExportLabelWidthEm);

    const sead::FormatFixedSafeString<32> originalRate(messages::exporting::cSampleRateOriginalFormat, summary.sampleRate);
    const char* sampleRateItems[IM_ARRAYSIZE(sSampleRateItems)];

    for (s32 i = 0; i < IM_ARRAYSIZE(sSampleRateItems); i++)
        sampleRateItems[i] = sSampleRateItems[i];

    if (single && summary.sampleRate != 0)
        sampleRateItems[0] = originalRate.cstr();

    GroupBoxRowLabel(messages::exporting::cFileType, labelWidth);
    ImGui::TextColored(GetItemValueColor(), "%s", messages::exporting::cFileTypeWav);

    GroupBoxRowLabel(messages::exporting::cSampleRate, labelWidth);
    ImGui::SetNextItemWidth(ImFloor(em * cExportListWidthEm));
    ComboScroll("##SampleRate", &sSettings.sampleRateIndex, sampleRateItems, sSampleRateDetails, IM_ARRAYSIZE(sSampleRateItems));

    if (hasStream || (hasSequence && (!single || summary.trackNum > 1)))
    {
        GroupBoxRowLabel(messages::exporting::cFiles, labelWidth);

        if (hasStream)
            ComboScroll("##Files", &sSettings.filesIndex, sExportFilesItems, sExportFilesDetails, IM_ARRAYSIZE(sExportFilesItems));
        else
            ComboScroll("##Files", &sSettings.filesIndex, sExportSeqFilesItems, sExportSeqFilesDetails, IM_ARRAYSIZE(sExportSeqFilesItems));
    }

    if (summary.loopNum > 0)
    {
        GroupBoxRowLabel(messages::exporting::cLoopMarkers, labelWidth);
        ImGui::Checkbox("##LoopMarkers", &sSettings.loopMarkers);
        DetailTooltip(messages::exporting::cLoopMarkersDetail);
    }

    if (single && totalSec > 0.0)
    {
        sead::FixedSafeString<32> sizeText;
        FormatItemSize(&sizeText, CalcExportFileSize(summary, totalSec));

        GroupBoxRowLabel(messages::exporting::cDiskSpace, labelWidth);
        ImGui::TextColored(GetItemValueColor(), "%s", sizeText.cstr());
    }

    EndGroupBox();
}

static void DrawSoundExportOptions(s32 seqCount, s32 strmCount, s32 waveCount)
{
    const s32 count = static_cast<s32>(sPendingSounds.size());
    const bool single = count == 1;
    const bool mixed = seqCount != count && strmCount != count;
    const char* title = mixed ? messages::exporting::cSoundsTitle : seqCount > 0 ? messages::exporting::cSequenceTitle : messages::exporting::cStreamTitle;
    const sead::FormatFixedSafeString<64> popupName("%s###ExportSound", title);
    const f32 em = ImGui::GetFontSize();

    ImGui::OpenPopup(popupName.cstr());

    CenterNextDialog();
    ImGui::SetNextWindowContentSize(ImVec2(ImFloor(em * cExportDialogWidthEm), 0.0f));

    bool popupOpen = true;
    if (!BeginDialog(popupName.cstr(), &popupOpen, ImGuiWindowFlags_AlwaysAutoResize))
    {
        sPendingSounds.clear();
        return;
    }

    if (ImGui::IsWindowAppearing())
        sExportSummary = CalcSoundExportSummary();

    const SoundExportSummary& summary = sExportSummary;

    sead::FixedSafeString<256> headTitle;
    if (single)
        headTitle.format("%s", sPendingSounds[0]->getName().cstr());
    else
        headTitle.format(messages::exporting::cSoundsFormat, count);

    if (headTitle.isEmpty())
        headTitle.format("%s", title);

    DrawPageHeader(single ? GetItemIcon(sPendingSounds[0]) : ICON_LC_FILE_AUDIO, headTitle.cstr(), FormatExportHeadDetail(summary, seqCount, strmCount, waveCount).cstr());

    const f64 totalSec = DrawExportTimelineBox(summary, count);

    sSettings.loopCount = ImClamp(sSettings.loopCount, cExportLoopCountMin, cExportLoopCountMax);
    sSettings.fadeSec = ImClamp(sSettings.fadeSec, cExportFadeSecMin, cExportFadeSecMax);
    sSettings.noEndSecs = ImClamp(sSettings.noEndSecs, cExportNoEndSecsMin, cExportNoEndSecsMax);

    DrawExportFormatBox(summary, single, strmCount > 0, seqCount > 0, totalSec);

    DrawShowExportedFilesCheck();
    ImGui::SameLine();

    BeginDialogFooter({messages::dialog::cExport, messages::dialog::cCancel}, false);

    const bool doExport = DialogButton(messages::dialog::cExport);
    ImGui::SetItemDefaultFocus();

    const bool doCancel = DialogButton(messages::dialog::cCancel);

    if (doExport && StartSoundExport(sPendingSounds, MakeSoundExportOptions()))
    {
        sPendingSounds.clear();
        ImGui::CloseCurrentPopup();
    }

    if (doCancel)
    {
        sPendingSounds.clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void DrawSoundExportDialog()
{
    if (sPendingSounds.empty() || IsSoundExportRunning())
        return;

    s32 seqCount = 0;
    s32 strmCount = 0;
    s32 waveCount = 0;

    for (const Sound* sound : sPendingSounds)
    {
        switch (sound->getSoundType())
        {
            case Sound::SoundType::Seq:  seqCount++; break;
            case Sound::SoundType::Strm: strmCount++; break;
            case Sound::SoundType::Wave: waveCount++; break;
            default: break;
        }
    }

    if (seqCount > 0 || strmCount > 0)
    {
        DrawSoundExportOptions(seqCount, strmCount, waveCount);
        return;
    }

    if (waveCount == static_cast<s32>(sPendingSounds.size()))
        StartSoundExport(sPendingSounds, MakeSoundExportOptions());

    sPendingSounds.clear();
}
