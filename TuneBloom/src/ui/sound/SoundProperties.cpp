#include <ui/sound/SoundProperties.h>

#include <ui/Messages.h>
#include <ui/sound/SoundPanels.h>
#include <ui/app/UI.h>
#include <ui/settings/UiColors.h>

static constexpr f32 cSoundNumberFieldEm     = 3.0f;
static constexpr f32 cSoundComboExtraEm      = 0.5f;
static constexpr f32 cSoundUserParamMinEm    = 5.5f;
static constexpr f32 cSoundTypeTitleGapEm    = 0.3f;
static constexpr f32 cSoundDecayKnobSpeed    = 0.005f;
static constexpr u8  cSoundPriorityMax       = 127;
static constexpr u8  cSoundDopplerMax        = 255;
static constexpr u8  cSoundPriorityDefault   = 64;
static constexpr f32 cSoundDecayRatioDefault = 0.5f;

static const char* cSoundDecayKnobs[]    = { messages::sound::cKnobDecayRatio };
static const char* cSoundDopplerKnobs[]  = { messages::sound::cKnobDoppler };
static const char* cSoundRemoteKnobs[]   = { messages::sound::cKnobRemoteFilter };
static const char* cSoundSectionKnobs[]  = { messages::common::cPriority, messages::sound::cKnobDecayRatio, messages::sound::cKnobDoppler, messages::sound::cKnobRemoteFilter };

static const char* cSoundPanModes[]       = { messages::sound::cPanModeDual, messages::sound::cPanModeBalance };
static const char* cSoundPanCurves[]      = {
    messages::sound::cPanCurveSqrt,
    messages::sound::cPanCurveSqrt0Db,
    messages::sound::cPanCurveSqrt0DbClamp,
    messages::sound::cPanCurveSinCos,
    messages::sound::cPanCurveSinCos0Db,
    messages::sound::cPanCurveSinCos0DbClamp,
    messages::common::cLinear,
    messages::sound::cPanCurveLinear0Db,
    messages::sound::cPanCurveLinear0DbClamp
};
static const char* cSoundDecayCurves[]    = { messages::sound::c3DDecayLog, messages::common::cLinear };
static const char* cSoundFrontBypasses[]  = { messages::common::cOff, messages::common::cOn };

static const char* cSoundPanModeDetails[] = { messages::sound::cPanModeDualDetail, messages::sound::cPanModeBalanceDetail };
static const char* cSoundPanCurveDetails[] = {
    messages::sound::cPanCurveDip3DbDetail,
    messages::sound::cPanCurveBoost3DbDetail,
    messages::sound::cPanCurveClampDetail,
    messages::sound::cPanCurveDip3DbDetail,
    messages::sound::cPanCurveBoost3DbDetail,
    messages::sound::cPanCurveClampDetail,
    messages::sound::cPanCurveLinearDetail,
    messages::sound::cPanCurveLinear0DbDetail,
    messages::sound::cPanCurveClampDetail
};
static const char* cSoundDecayCurveDetails[]   = { messages::sound::c3DDecayLogDetail, messages::sound::c3DDecayLinearDetail };
static const char* cSoundFrontBypassDetails[]  = { nullptr, nullptr };

f32 TrailingButtonsWidth(s32 count)
{
    return (ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x) * static_cast<f32>(count);
}

bool TrailingButton(const char* label)
{
    const f32 size = ImGui::GetFrameHeight();

    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);

    return ImGui::Button(label, ImVec2(size, size));
}

bool ItemSelectorRow(const char* label, const Item::List& list, Item** item, bool allowNone, f32 trailingWidth)
{
    const bool missing = *item == nullptr && !allowNone;

    if (missing)
        ImGui::PushStyleColor(ImGuiCol_Text, uicolors::Marker());

    PropertyRow(label);

    if (missing)
        ImGui::PopStyleColor();

    if (trailingWidth > 0.0f)
        ImGui::SetNextItemWidth(-trailingWidth);

    return ItemSelector(sead::FormatFixedSafeString<64>("##%s", label).cstr(), list, item, allowNone);
}

bool DrawSoundPanelHeader(const char* title, bool* enabled)
{
    return DrawPanelHeader(title, enabled, messages::sound::cPanelEnableDetail);
}

bool BeginSoundSection(const char* title, bool* enabled, bool* changed, const char* detail)
{
    return BeginPropertySection(title, enabled, changed, true, 1, detail, nullptr, messages::sound::cPanelEnableDetail);
}

static f32 CalcSoundComboWidth(const char* const* items, s32 itemNum)
{
    f32 width = 0.0f;
    for (s32 i = 0; i < itemNum; i++)
        width = sead::Mathf::max(width, ImGui::CalcTextSize(items[i]).x);

    return width + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2.0f + ImFloor(ImGui::GetFontSize() * cSoundComboExtraEm);
}

static bool DrawSoundCheckSlot(const char* label, bool* value, f32 slotWidth, f32 slotHeight)
{
    const f32 size = ImGui::GetFrameHeight();
    bool changed = false;

    DrawSoundSlot(label, slotWidth, ImVec2(size, size), slotHeight, [&]
    {
        changed = ImGui::Checkbox(sead::FormatFixedSafeString<64>("##%s", label).cstr(), value);
    });

    return changed;
}

static bool DrawSoundSideCombo(const char* label, const char* detail, s32* value, const char* const* items, const char* const* details, s32 itemNum, f32 knobHeight)
{
    const f32 width = CalcSoundComboWidth(items, itemNum);
    bool changed = false;

    DrawSoundSideField(label, detail, width, knobHeight, [&]
    {
        ImGui::SetNextItemWidth(width);
        changed = ComboScroll(sead::FormatFixedSafeString<64>("##%s", label).cstr(), value, items, details, itemNum, detail);
    });

    return changed;
}

static void DrawSoundPanModeSection(Sound* sound)
{
    bool enablePanParam = sound->isEnablePanParam();
    bool changed = false;
    const bool open = BeginSoundSection(sead::FormatFixedSafeString<64>("%s###Pan Mode", messages::sound::cSectionPanMode).cstr(), &enablePanParam, &changed);

    if (changed)
    {
        sound->setEnablePanParam(enablePanParam);
        SetUnsavedChanges(true);
    }

    if (!open)
        return;

    PropertyRow(messages::sound::cPanModeLabel);

    s32 panMode = static_cast<s32>(sound->getPanMode());
    if (ComboScroll("##panMode", &panMode, cSoundPanModes, cSoundPanModeDetails, IM_ARRAYSIZE(cSoundPanModes)))
    {
        sound->setPanMode(static_cast<snd::PanMode>(panMode));
        SetUnsavedChanges(true);
    }

    PropertyRow(messages::common::cCurve);

    s32 panCurve = static_cast<s32>(sound->getPanCurve());
    if (ComboScroll("##panCurve", &panCurve, cSoundPanCurves, cSoundPanCurveDetails, IM_ARRAYSIZE(cSoundPanCurves)))
    {
        sound->setPanCurve(static_cast<snd::PanCurve>(panCurve));
        SetUnsavedChanges(true);
    }

    EndPropertySection();
}

static void DrawSound3DSection(Sound* sound)
{
    struct Flag
    {
        const char* label;
        u32 mask;
        const char* detail;
    };

    static const Flag sFlags[] = {
        { messages::common::cVolume,   Sound::Sound3DInfo::Flags::Volume,   messages::sound::c3DVolumeDetail },
        { messages::common::cPriority, Sound::Sound3DInfo::Flags::Priority, messages::sound::c3DPriorityDetail },
        { messages::common::cPan,      Sound::Sound3DInfo::Flags::Pan,      messages::sound::c3DPanDetail },
        { messages::common::cSurround, Sound::Sound3DInfo::Flags::SPan,     messages::sound::c3DSurroundDetail },
        { messages::common::cFilter,   Sound::Sound3DInfo::Flags::Filter,   messages::sound::c3DFilterDetail }
    };

    bool enableSound3DInfo = sound->isEnableSound3DInfo();
    bool changed = false;
    const bool open = BeginSoundSection(sead::FormatFixedSafeString<64>("%s###3D", messages::sound::cSection3D).cstr(), &enableSound3DInfo, &changed, messages::sound::c3DSectionDetail);

    if (changed)
    {
        sound->setEnableSound3DInfo(enableSound3DInfo);
        SetUnsavedChanges(true);
    }

    if (!open)
        return;

    Sound::Sound3DInfo& sound3DInfo = sound->getSound3DInfo();
    const f32 stripHeight = CalcRegionStripHeight();

    RegionHeader(messages::sound::c3DApplyTitle);
    {
        f32 slotWidth = 0.0f;
        for (const Flag& flag : sFlags)
            slotWidth = sead::Mathf::max(slotWidth, ImGui::CalcTextSize(flag.label).x);

        slotWidth = sead::Mathf::min(slotWidth, ImFloor((ImGui::GetContentRegionAvail().x - CalcKnobGap() * (IM_ARRAYSIZE(sFlags) - 1)) / IM_ARRAYSIZE(sFlags)));

        u32 flags = sound3DInfo.getFlags();

        for (const Flag& flag : sFlags)
        {
            if (&flag != sFlags)
                ImGui::SameLine(0.0f, CalcKnobGap());

            bool on = (flags & flag.mask) != 0;
            if (DrawSoundCheckSlot(flag.label, &on, slotWidth, CalcSoundSlotHeight()))
            {
                sound3DInfo.setFlags(on ? flags | flag.mask : flags & ~flag.mask);
                SetUnsavedChanges(true);
            }

            DetailTooltip(flag.detail);
        }
    }

    RegionHeader(messages::sound::c3DDistanceTitle);
    {
        f32 decayRatio = sound3DInfo.getDecayRatio();
        SetNextKnobReset(cSoundDecayRatioDefault);
        if (KnobF32(cSoundDecayKnobs[0], &decayRatio, 0.0f, 1.0f, "%.2f", cSoundDecayKnobSpeed, ImVec2(CalcKnobFitWidth(cSoundSectionKnobs, IM_ARRAYSIZE(cSoundSectionKnobs)), stripHeight)))
        {
            sound3DInfo.setDecayRatio(decayRatio);
            SetUnsavedChanges(true);
        }

        KnobTooltip("%.3f", decayRatio);
        DetailTooltip(messages::sound::c3DDecayRatioDetail);

        ImGui::SameLine(0.0f, CalcKnobGap());

        u8 dopplerFactor = sound3DInfo.getDopplerFactor();
        SetNextKnobReset(0.0);
        if (KnobU8(cSoundDopplerKnobs[0], &dopplerFactor, 0, cSoundDopplerMax, ImVec2(CalcKnobFitWidth(cSoundSectionKnobs, IM_ARRAYSIZE(cSoundSectionKnobs)), stripHeight)))
        {
            sound3DInfo.setDopplerFactor(dopplerFactor);
            SetUnsavedChanges(true);
        }

        KnobTooltip("%u", dopplerFactor);
        DetailTooltip(messages::sound::c3DDopplerDetail);

        s32 decayCurve = static_cast<s32>(sound3DInfo.getDecayCurve()) - 1;
        if (DrawSoundSideCombo(messages::sound::c3DDecayCurve, nullptr, &decayCurve, cSoundDecayCurves, cSoundDecayCurveDetails, IM_ARRAYSIZE(cSoundDecayCurves), stripHeight))
        {
            sound3DInfo.setDecayCurve(decayCurve + 1);
            SetUnsavedChanges(true);
        }
    }

    EndPropertySection();
}

static void DrawPlayerPrioritySection(Sound* sound)
{
    bool enablePlayerParam = sound->isEnablePlayerParam();
    bool changed = false;
    const bool open = BeginSoundSection(sead::FormatFixedSafeString<64>("%s###Player Priority", messages::sound::cSectionPlayerPriority).cstr(), &enablePlayerParam, &changed);

    if (changed)
    {
        sound->setEnablePlayerParam(enablePlayerParam);
        SetUnsavedChanges(true);
    }

    if (!open)
        return;

    const f32 stripHeight = CalcRegionStripHeight();
    const f32 fieldWidth = ImFloor(ImGui::GetFontSize() * cSoundNumberFieldEm);

    u8 playerPriority = sound->getPlayerPriority();
    SetNextKnobReset(cSoundPriorityDefault);
    if (KnobU8(sead::FormatFixedSafeString<32>("%s###PlayerPriority", messages::common::cPriority).cstr(),&playerPriority, 0, cSoundPriorityMax, ImVec2(CalcKnobFitWidth(cSoundSectionKnobs, IM_ARRAYSIZE(cSoundSectionKnobs)), stripHeight)))
    {
        sound->setPlayerPriority(playerPriority);
        SetUnsavedChanges(true);
    }

    KnobTooltip("%u", playerPriority);
    DetailTooltip(messages::sound::cPlayerPriorityDetail);

    ImGui::SameLine(0.0f, CalcKnobGap());

    DrawSoundSlot(messages::sound::cActorPlayer,0.0f, ImVec2(fieldWidth, ImGui::GetFrameHeight()), stripHeight, [&]
    {
        u8 actorPlayerId = sound->getActorPlayerId();

        ImGui::SetNextItemWidth(fieldWidth);
        if (ImGui::InputScalar("##actorPlayerId", ImGuiDataType_U8, &actorPlayerId))
        {
            sound->setActorPlayerId(actorPlayerId);
            SetUnsavedChanges(true);
        }
    });

    DetailTooltip(messages::sound::cActorPlayerDetail);

    EndPropertySection();
}

template <typename SoundInfo>
static void DrawChannelPrioritySection(SoundInfo& soundInfo)
{
    bool enablePriority = soundInfo.isEnablePriority();
    bool changed = false;
    const bool open = BeginSoundSection(sead::FormatFixedSafeString<64>("%s###Channel Priority", messages::sound::cSectionChannelPriority).cstr(), &enablePriority, &changed);

    if (changed)
    {
        soundInfo.setEnablePriority(enablePriority);
        SetUnsavedChanges(true);
    }

    if (!open)
        return;

    const f32 stripHeight = CalcRegionStripHeight();

    u8 channelPriority = soundInfo.getChannelPriority();
    SetNextKnobReset(cSoundPriorityDefault);
    if (KnobU8(sead::FormatFixedSafeString<32>("%s###ChannelPriority", messages::common::cPriority).cstr(),&channelPriority, 0, cSoundPriorityMax, ImVec2(CalcKnobFitWidth(cSoundSectionKnobs, IM_ARRAYSIZE(cSoundSectionKnobs)), stripHeight)))
    {
        soundInfo.setChannelPriority(channelPriority);
        SetUnsavedChanges(true);
    }

    KnobTooltip("%u", channelPriority);
    DetailTooltip(messages::sound::cChannelPriorityDetail);

    ImGui::SameLine(0.0f, CalcKnobGap());

    bool isReleasePriorityFix = soundInfo.getIsReleasePriorityFix();
    if (DrawSoundCheckSlot(messages::sound::cFixAtRelease,&isReleasePriorityFix, 0.0f, stripHeight))
    {
        soundInfo.setIsReleasePriorityFix(isReleasePriorityFix);
        SetUnsavedChanges(true);
    }

    DetailTooltip(messages::sound::cFixAtReleaseDetail);

    EndPropertySection();
}

static void DrawSoundPrioritySections(Sound* sound)
{
    DrawPlayerPrioritySection(sound);

    if (sound->getSoundType() == Sound::SoundType::Seq)
        DrawChannelPrioritySection(sound->getSequenceSoundInfo());
    else if (sound->getSoundType() == Sound::SoundType::Wave)
        DrawChannelPrioritySection(sound->getWaveSoundInfo());
}

static void DrawSoundTypeUI(Sound* sound)
{
    static const char* sSoundTypes[] = {
        messages::common::cSequence,
        messages::common::cStream,
        messages::common::cWave
    };

    static const char* sSoundTypeDetails[] = {
        messages::sound::cTypeSequenceDetail,
        messages::sound::cTypeStreamDetail,
        messages::sound::cTypeWaveDetail
    };

    const f32 em = ImGui::GetFontSize();
    const f32 comboWidth = CalcSoundComboWidth(sSoundTypes, IM_ARRAYSIZE(sSoundTypes));

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ImFloor(em * cSoundPanelPadXEm), ImFloor(em * cSoundPanelPadYEm)));

    if (!ImGui::BeginTable("SoundType", 1, ImGuiTableFlags_BordersOuter))
    {
        ImGui::PopStyleVar();
        return;
    }

    ImGui::TableNextColumn();

    {
        const ImVec2 headerPos = ImGui::GetCursorScreenPos();
        const f32 comboX = headerPos.x + ImGui::GetContentRegionAvail().x - comboWidth;

        RegionHeader(messages::common::cType,comboWidth + ImGui::GetStyle().ItemSpacing.x);

        ImGui::SameLine();
        ImGui::SetCursorScreenPos(ImVec2(comboX, headerPos.y));
        ImGui::SetNextItemWidth(comboWidth);

        s32 soundType = static_cast<s32>(sound->getSoundType()) - 1;
        if (ComboScroll("##soundType", &soundType, sSoundTypes, sSoundTypeDetails, IM_ARRAYSIZE(sSoundTypes)))
        {
            sound->setSoundType(static_cast<Sound::SoundType>(soundType + 1));
            SetUnsavedChanges(true);
        }

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImFloor(em * cSoundTypeTitleGapEm));
    }

    switch (sound->getSoundType())
    {
        case Sound::SoundType::Seq:
            DrawSequenceSoundUI(sound->getSequenceSoundInfo());
            break;

        case Sound::SoundType::Strm:
            DrawStreamSoundUI(sound, sound->getStreamSoundInfo());
            break;

        case Sound::SoundType::Wave:
            DrawWaveSoundUI(sound->getWaveSoundInfo());
            break;

        default:
            break;
    }

    ImGui::EndTable();
    ImGui::PopStyleVar();
}

static void DrawSoundUserParamSection(Sound* sound)
{
    const f32 em = ImGui::GetFontSize();
    const s32 columnNum = ImGui::GetContentRegionAvail().x >= em * cSoundUserParamMinEm * 4.0f ? 4 : 2;

    if (!BeginPropertySection(sead::FormatFixedSafeString<64>("%s###User Parameters", messages::sound::cSectionUserParam).cstr(),nullptr, nullptr, false, columnNum, messages::sound::cUserParamSectionDetail))
        return;

    for (u32 i = 0; i < 4; i++)
    {
        if (i != 0)
            ImGui::TableNextColumn();

        ImGui::PushID(i);

        bool enableUserParam = sound->isEnableUserParam(i);
        if (DrawSoundPanelHeader(sead::FormatFixedSafeString<16>(messages::sound::cUserParamFormat,i).cstr(), &enableUserParam))
        {
            sound->setEnableUserParam(i, enableUserParam);
            SetUnsavedChanges(true);
        }

        ImGui::BeginDisabled(!enableUserParam);

        u32 userParam = enableUserParam ? sound->getUserParam(i) : 0;

        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputScalar("##userParam", ImGuiDataType_U32, &userParam))
        {
            sound->setUserParam(i, userParam);
            SetUnsavedChanges(true);
        }

        ImGui::EndDisabled();

        ImGui::PopID();
    }

    EndPropertySection();
}

static void DrawSoundOtherSection(Sound* sound)
{
    if (!BeginPropertySection(sead::FormatFixedSafeString<64>("%s###Other", messages::sound::cSectionOther).cstr(),nullptr, nullptr, false))
        return;

    const f32 stripHeight = CalcRegionStripHeight();

    const bool remoteFilterUsed = sBfsar.getFormat() != ArchiveFormat::BCSAR;
    u8 remoteFilter = sound->getRemoteFilter();

    ImGui::BeginDisabled(!remoteFilterUsed);
    SetNextKnobReset(0.0);
    if (KnobU8(cSoundRemoteKnobs[0], &remoteFilter, cPanMin, cPanMax, ImVec2(CalcKnobFitWidth(cSoundSectionKnobs, IM_ARRAYSIZE(cSoundSectionKnobs)), stripHeight)))
    {
        sound->setRemoteFilter(remoteFilter);
        SetUnsavedChanges(true);
    }
    ImGui::EndDisabled();

    if (remoteFilterUsed)
    {
        KnobTooltip("%.3f", static_cast<f32>(remoteFilter) / 127.0f);
        DetailTooltip(messages::sound::cRemoteFilterDetail);
    }
    else
        SetDisabledTooltip(messages::sound::cRemoteFilterNoCtr);

    if (sound->getSoundType() != Sound::SoundType::Strm)
    {
        s32 frontBypass = sound->isEnableIsFrontBypass() && sound->getIsFrontBypass() ? 1 : 0;

        if (DrawSoundSideCombo(messages::common::cFrontBypass,messages::sound::cFrontBypassDetail, &frontBypass, cSoundFrontBypasses, cSoundFrontBypassDetails, IM_ARRAYSIZE(cSoundFrontBypasses), stripHeight))
        {
            if (frontBypass == 1)
                sound->setEnableIsFrontBypass(true);

            sound->setIsFrontBypass(frontBypass == 1);
            SetUnsavedChanges(true);
        }
    }

    if (sound->getSoundType() == Sound::SoundType::Wave)
        DrawWaveExtraOptionsUI(sound->getWaveSoundInfo());

    EndPropertySection();
}

void DrawSoundPropertiesUI()
{
    Sound* sound = static_cast<Sound*>(sSelectedItem);

    {
        Item* player = sound->getPlayerRef().getItem();
        if (ItemSelectorRow(messages::common::cPlayer,sBfsar.getPlayerList(), &player))
        {
            sound->getPlayerRef().attach(player);
            SetUnsavedChanges(true);
        }
    }

    if (sBfsar.isV3Bfsar())
    {
        char tag[0x20];
        sound->getV3Tag(tag, sizeof(tag));

        PropertyRow(messages::sound::cTagLabel);
        if (ImGui::InputText("##tag", tag, sizeof(tag)))
        {
            sound->setV3Tag(tag);
            SetUnsavedChanges(true);
        }
    }

    DrawSoundTypeUI(sound);
    DrawSoundMixPanels(sound);

    const Sound::SoundType soundType = sound->getSoundType();

    if (soundType == Sound::SoundType::Strm && sBfsar.isStreamSendAvailable())
        DrawStreamExtensionUI(sound->getStreamSoundInfo());

    if (soundType == Sound::SoundType::Wave)
        DrawWaveEnvelopeSection(sound->getWaveSoundInfo());

    DrawSoundPanModeSection(sound);
    DrawSound3DSection(sound);
    DrawSoundPrioritySections(sound);
    DrawSoundUserParamSection(sound);
    DrawSoundOtherSection(sound);
}
