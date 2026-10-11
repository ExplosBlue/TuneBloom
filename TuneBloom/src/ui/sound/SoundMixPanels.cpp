#include <ui/sound/SoundProperties.h>

#include <ui/Messages.h>
#include <ui/sound/SoundPanels.h>
#include <ui/app/UI.h>

#include <bfsar/files/BfwsdFile.h>

static constexpr ImU8 cVolumeMin         = 0;
static constexpr ImU8 cVolumeMax         = 255;
static constexpr f32  cSoundStartLabelEm = 12.0f;
static constexpr u8   cSoundPanCenter    = 64;

static const char* cSoundOutputKnobs[] = { messages::common::cVolume };
static const char* cSoundPitchKnobs[]  = { messages::param::cCoarse, messages::param::cFine };
static const char* cSoundPanKnobs[]    = { messages::common::cPan, messages::param::cSurroundPan };
static const char* cSoundSendKnobs[]   = { messages::param::cSendMain, messages::param::cSendFxA, messages::param::cSendFxB, messages::param::cSendFxC };
static const char* cSoundFilterKnobs[] = { messages::common::cLpf, messages::param::cBiquad };
static const char* cSoundBiquadLabel   = messages::common::cType;

static const char* cBiquadTypes[] = { messages::common::cOff, messages::common::cLpf, messages::sound::cBiquadHpf, messages::sound::cBiquadBpf512, messages::sound::cBiquadBpf1024, messages::sound::cBiquadBpf2048 };
static const char* cBiquadDetails[] = {
    nullptr,
    messages::sound::cBiquadLpfDetail,
    messages::sound::cBiquadHpfDetail,
    messages::sound::cBiquadBpf512Detail,
    messages::sound::cBiquadBpf1024Detail,
    messages::sound::cBiquadBpf2048Detail
};

enum class SoundPanel
{
    Output,
    Pitch,
    Send,
    Pan,
    Filter,
    StartLabel
};

f32 CalcBiquadTypeWidth()
{
    f32 width = 0.0f;
    for (const char* type : cBiquadTypes)
        width = sead::Mathf::max(width, ImGui::CalcTextSize(type).x);

    return width + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2.0f;
}

bool DrawBiquadTypeCombo(u8* biquadType, f32 width)
{
    const sead::FormatFixedSafeString<16> customType("%u", *biquadType);
    bool changed = false;

    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo("##BiquadType", *biquadType < IM_ARRAYSIZE(cBiquadTypes) ? cBiquadTypes[*biquadType] : customType.cstr()))
    {
        for (u8 type = 0; type < IM_ARRAYSIZE(cBiquadTypes); type++)
        {
            if (ImGui::Selectable(cBiquadTypes[type], type == *biquadType))
            {
                *biquadType = type;
                changed = true;
            }

            ListItemDetailTooltip(cBiquadDetails[type]);
        }

        ImGui::EndCombo();
    }

    return changed;
}

static void OffsetSoundPanelBody(f32 offset)
{
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
}

static f32 CalcSoundSideFieldWidth(const char* label, f32 fieldWidth)
{
    return ImFloor(ImGui::GetFontSize() * cSoundSideGapEm) + ImGui::CalcTextSize(label).x + ImGui::GetStyle().ItemSpacing.x + fieldWidth;
}

static void DrawSoundOutputKnobs(Sound* sound)
{
    u8 volume = sound->getVolume();
    SetNextKnobReset(cSoundVolumeDefault);
    if (KnobU8(cSoundOutputKnobs[0], &volume, cVolumeMin, cVolumeMax, ImVec2(CalcKnobWidth(cSoundOutputKnobs, IM_ARRAYSIZE(cSoundOutputKnobs)), CalcRegionStripHeight())))
    {
        sound->setVolume(volume);
        SetUnsavedChanges(true);
    }

    KnobTooltip("%.3f", static_cast<f32>(volume) / 127.0f);
}

template <typename SoundInfo>
static void DrawSoundPitchKnobs(SoundInfo& soundInfo)
{
    f32 pitch = soundInfo.getPitch();
    if (DrawPitchKnobs(&pitch, ImVec2(CalcKnobWidth(cSoundPitchKnobs, IM_ARRAYSIZE(cSoundPitchKnobs)), CalcRegionStripHeight())))
    {
        soundInfo.setPitch(pitch);
        SetUnsavedChanges(true);
    }
}

static void DrawSoundPanKnobs(Sound::WaveSoundInfo& waveSoundInfo)
{
    const ImVec2 knobSize(CalcKnobWidth(cSoundPanKnobs, IM_ARRAYSIZE(cSoundPanKnobs)), CalcRegionStripHeight());

    u8 pan = waveSoundInfo.getPan();
    SetNextKnobText(FormatPanText(pan, cSoundPanCenter).cstr());
    if (KnobU8(cSoundPanKnobs[0], &pan, cPanMin, cPanMax, cSoundPanCenter, knobSize))
    {
        waveSoundInfo.setPan(pan);
        SetUnsavedChanges(true);
    }

    KnobTooltip("%s", FormatPanLabel(pan, cPanMin, cSoundPanCenter, cPanMax).cstr());

    ImGui::SameLine(0.0f, CalcKnobGap());

    u8 surroundPan = static_cast<u8>(sead::MathCalcCommon<s32>::clamp2(cPanMin, waveSoundInfo.getSurroundPan(), cPanMax));
    SetNextKnobReset(0.0);
    if (KnobU8(cSoundPanKnobs[1], &surroundPan, cPanMin, cPanMax, knobSize))
    {
        waveSoundInfo.setSurroundPan(static_cast<s8>(surroundPan));
        SetUnsavedChanges(true);
    }

    KnobTooltip("%.3f", static_cast<f32>(surroundPan) / 127.0f);
    DetailTooltip(messages::sound::cSurroundPanDetail);
}

template <typename SoundInfo>
static void DrawSoundSendKnobs(SoundInfo& soundInfo)
{
    const ImVec2 knobSize(CalcKnobWidth(cSoundSendKnobs, IM_ARRAYSIZE(cSoundSendKnobs)), CalcRegionStripHeight());

    u8 mainSend = soundInfo.getMainSend();
    SetNextKnobReset(cSoundMainSendDefault);
    if (KnobU8(cSoundSendKnobs[0], &mainSend, cPanMin, cPanMax, knobSize))
    {
        soundInfo.setMainSend(mainSend);
        SetUnsavedChanges(true);
    }

    KnobTooltip("%.3f", static_cast<f32>(mainSend) / 127.0f);
    DetailTooltip(messages::sound::cMainSendDetail);

    for (u32 i = 0; i < 3; i++)
    {
        ImGui::SameLine(0.0f, CalcKnobGap());

        u8 fxSend = soundInfo.getFxSend(i);
        SetNextKnobReset(0.0);
        if (KnobU8(cSoundSendKnobs[i + 1], &fxSend, cPanMin, cPanMax, knobSize))
        {
            soundInfo.setFxSend(i, fxSend);
            SetUnsavedChanges(true);
        }

        KnobTooltip("%.3f", static_cast<f32>(fxSend) / 127.0f);
        DetailTooltip(sead::FormatFixedSafeString<128>(messages::sound::cFxSendDetailFormat, static_cast<char>('A' + i)).cstr());
    }
}

static f32 CalcSoundFilterWidth()
{
    return CalcKnobRowWidth(cSoundFilterKnobs, IM_ARRAYSIZE(cSoundFilterKnobs)) + CalcSoundSideFieldWidth(cSoundBiquadLabel, CalcBiquadTypeWidth());
}

static void DrawSoundFilterKnobs(Sound::WaveSoundInfo& waveSoundInfo)
{
    const f32 stripHeight = CalcRegionStripHeight();
    const ImVec2 knobSize(CalcKnobWidth(cSoundFilterKnobs, IM_ARRAYSIZE(cSoundFilterKnobs)), stripHeight);

    u8 lpfFreq = waveSoundInfo.getLpfFreq();
    SetNextKnobReset(cSoundLpfMax);
    if (KnobU8(cSoundFilterKnobs[0], &lpfFreq, 0, cSoundLpfMax, knobSize))
    {
        waveSoundInfo.setLpfFreq(lpfFreq);
        SetUnsavedChanges(true);
    }

    KnobTooltip("%.3f", (static_cast<f32>(lpfFreq) / 64.0f) - 1.0f);
    DetailTooltip(messages::sound::cLpfDetail);

    ImGui::SameLine(0.0f, CalcKnobGap());

    u8 biquadValue = waveSoundInfo.getBiquadValue();
    SetNextKnobReset(0.0);
    if (KnobU8(cSoundFilterKnobs[1], &biquadValue, cPanMin, cPanMax, knobSize))
    {
        waveSoundInfo.setBiquadValue(biquadValue);
        SetUnsavedChanges(true);
    }

    KnobTooltip("%.3f", static_cast<f32>(biquadValue) / 127.0f);
    DetailTooltip(messages::sound::cBiquadDetail);

    DrawSoundSideField(cSoundBiquadLabel, nullptr, CalcBiquadTypeWidth(), stripHeight, [&]
    {
        u8 biquadType = waveSoundInfo.getBiquadType();
        if (DrawBiquadTypeCombo(&biquadType, CalcBiquadTypeWidth()))
        {
            waveSoundInfo.setBiquadType(biquadType);
            SetUnsavedChanges(true);
        }
    });
}

static void DrawWaveSoundPanel(SoundPanel panel, Sound::WaveSoundInfo& waveSoundInfo, f32 offset)
{
    switch (panel)
    {
        case SoundPanel::Pitch:
        {
            bool enablePitch = waveSoundInfo.isEnablePitch();
            if (DrawSoundPanelHeader(messages::common::cPitch,&enablePitch))
            {
                waveSoundInfo.setEnablePitch(enablePitch);
                SetUnsavedChanges(true);
            }

            ImGui::BeginDisabled(!enablePitch);
            OffsetSoundPanelBody(offset);
            DrawSoundPitchKnobs(waveSoundInfo);
            ImGui::EndDisabled();
            break;
        }

        case SoundPanel::Pan:
        {
            bool enablePan = waveSoundInfo.isEnablePan();
            if (DrawSoundPanelHeader(messages::common::cPan,&enablePan))
            {
                waveSoundInfo.setEnablePan(enablePan);
                SetUnsavedChanges(true);
            }

            ImGui::BeginDisabled(!enablePan);
            OffsetSoundPanelBody(offset);
            DrawSoundPanKnobs(waveSoundInfo);
            ImGui::EndDisabled();
            break;
        }

        case SoundPanel::Send:
        {
            bool enableSend = waveSoundInfo.isEnableSend();
            if (DrawSoundPanelHeader(messages::panel::cSend,&enableSend))
            {
                waveSoundInfo.setEnableSend(enableSend);
                SetUnsavedChanges(true);
            }

            ImGui::BeginDisabled(!enableSend);
            OffsetSoundPanelBody(offset);
            DrawSoundSendKnobs(waveSoundInfo);
            ImGui::EndDisabled();
            break;
        }

        case SoundPanel::Filter:
        {
            bool enableFilter = waveSoundInfo.isEnableFilter();
            if (DrawSoundPanelHeader(messages::common::cFilter,&enableFilter))
            {
                waveSoundInfo.setEnableFilter(enableFilter);
                SetUnsavedChanges(true);
            }

            ImGui::BeginDisabled(!enableFilter);
            OffsetSoundPanelBody(offset);
            DrawSoundFilterKnobs(waveSoundInfo);
            ImGui::EndDisabled();
            break;
        }

        default:
            break;
    }
}

void DrawSoundMixPanels(Sound* sound)
{
    const Sound::SoundType soundType = sound->getSoundType();
    const bool isWave = soundType == Sound::SoundType::Wave;
    const bool hasPitchAndSend = isWave || (soundType == Sound::SoundType::Strm && sBfsar.isStreamSendAvailable());

    SoundPanel panels[5];
    f32 widths[5];
    s32 panelNum = 0;

    const bool hasFilter = isWave && BfwsdFile::isFilterSupportedVersion(sBfsar.getVersionForBfwsd(), sBfsar.getFormat());
    const bool twoLines = isWave && !hasFilter;

    const auto addPanel = [&](SoundPanel panel, f32 width)
    {
        panels[panelNum] = panel;
        widths[panelNum++] = width;
    };

    const f32 pitchWidth = CalcKnobRowWidth(cSoundPitchKnobs, IM_ARRAYSIZE(cSoundPitchKnobs));
    const f32 sendWidth = CalcKnobRowWidth(cSoundSendKnobs, IM_ARRAYSIZE(cSoundSendKnobs));

    addPanel(SoundPanel::Output, CalcKnobRowWidth(cSoundOutputKnobs, IM_ARRAYSIZE(cSoundOutputKnobs)));

    if (twoLines)
    {
        addPanel(SoundPanel::Send, sendWidth);
        addPanel(SoundPanel::Pitch, pitchWidth);
    }
    else if (hasPitchAndSend)
    {
        addPanel(SoundPanel::Pitch, pitchWidth);
        addPanel(SoundPanel::Send, sendWidth);
    }

    if (isWave)
        addPanel(SoundPanel::Pan, CalcKnobRowWidth(cSoundPanKnobs, IM_ARRAYSIZE(cSoundPanKnobs)));

    if (hasFilter)
        addPanel(SoundPanel::Filter, CalcSoundFilterWidth());

    if (soundType == Sound::SoundType::Seq)
    {
        addPanel(SoundPanel::StartLabel, ImFloor(ImGui::GetFontSize() * cSoundStartLabelEm));
    }

    const auto draw = [&](s32 index, f32 offset)
    {
        if (panels[index] == SoundPanel::Output)
        {
            RegionHeader(messages::panel::cOutput);
            OffsetSoundPanelBody(offset);
            DrawSoundOutputKnobs(sound);
        }
        else if (panels[index] == SoundPanel::StartLabel)
        {
            DrawSequenceStartLabelPanel(sound->getSequenceSoundInfo());
        }
        else if (isWave)
        {
            DrawWaveSoundPanel(panels[index], sound->getWaveSoundInfo(), offset);
        }
        else if (panels[index] == SoundPanel::Pitch)
        {
            RegionHeader(messages::common::cPitch);
            OffsetSoundPanelBody(offset);
            DrawSoundPitchKnobs(sound->getStreamSoundInfo());
        }
        else
        {
            RegionHeader(messages::panel::cSend);
            OffsetSoundPanelBody(offset);
            DrawSoundSendKnobs(sound->getStreamSoundInfo());
        }
    };

    if (!twoLines)
    {
        DrawSoundPanels("SoundMixPanels", widths, panelNum, draw);
        return;
    }

    DrawSoundPanels("SoundMixPanels", widths, 2, draw);
    DrawSoundPanels("SoundMixPanels2", widths + 2, 2, [&](s32 index, f32 offset) { draw(index + 2, offset); });
}
