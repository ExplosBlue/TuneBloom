#include <ui/bank/BankFileEditor.h>

#include <bfsar/seq/SeqCommand.h>

#include <ui/widgets/ContextMenu.h>
#include <ui/widgets/Knob.h>
#include <ui/Messages.h>
#include <ui/bank/PianoRollPalette.h>
#include <ui/app/PopupMgr.h>
#include <ui/settings/Preferences.h>
#include <ui/sound/SoundProperties.h>
#include <ui/app/UI.h>
#include <ui/settings/UiColors.h>

#include <imgui/imgui_custom.h>

#include <algorithm>
#include <cstdio>
#include <vector>

static constexpr f32 cPianoKeysHeightEm = 3.5f;

static constexpr f32 cKeyboardPaneInitialHeight = 200.0f;
static constexpr f32 cRegionPaneInitialHeight   = 128.0f;
static constexpr f32 cRegionPaneMinHeight       = 48.0f;
static constexpr f32 cRegionPaneHeightReserve   = 220.0f;
static constexpr f32 cSplitterHeight            = 6.0f;
static constexpr f32 cSplitterLineInset         = 6.0f;
static constexpr f32 cSplitterLineThickness     = 2.0f;

static constexpr f32 cAddInstrumentNameWidth       = 240.0f;
static constexpr f32 cAddInstrumentProgramWidth    = 120.0f;
static constexpr s16 cAddInstrumentProgramStep     = 1;
static constexpr s16 cAddInstrumentProgramStepFast = 10;

static constexpr f32 cRegionMappingFieldEm   = 3.0f;
static constexpr f32 cRegionMappingMinGapEm  = 0.75f;
static constexpr f32 cRegionRangeDragSpeed   = 0.25f;
static constexpr f32 cProgramNoDragSpeed     = 0.25f;
static constexpr f32 cRegionPanelPadXEm      = 0.5f;
static constexpr f32 cRegionPanelPadYEm      = 0.3f;
static constexpr f32 cRegionSampleComboMinEm = 6.0f;
static constexpr f32 cRegionWaveFieldMinEm   = 10.0f;

static constexpr s32 cRegionRangeMin  = 0;
static constexpr s32 cRegionRangeMax  = 127;
static constexpr s32 cKeyGroupMax     = 15;
static constexpr s32 cEmptyFieldValue = 0;

static constexpr u8 cRegionPanCenter     = 64;
static constexpr u8 cRegionVolumeMax     = 255;
static constexpr u8 cRegionVolumeDefault = 127;

static const char* cRegionPitchKnobs[]  = { messages::param::cCoarse, messages::param::cFine };
static const char* cRegionOutputKnobs[] = { messages::common::cVolume, messages::common::cPan };

static sead::FixedSafeString<24> FormatKeyName(s32 key)
{
    if (key < 0 || key >= static_cast<s32>(MmlCommandNote::sKeysNum))
        return sead::FixedSafeString<24>(messages::common::cNoValue);

    const char* s = MmlCommandNote::sKeys[key];
    char letter = s[0];

    if (letter >= 'a' && letter <= 'z')
        letter = static_cast<char>(letter - 'a' + 'A');

    const char* accidental = (s[1] == 's') ? "#" : "";

    char octave[8];
    if (s[2] == 'm')
        snprintf(octave, sizeof(octave), "-%s", s + 3);
    else
        snprintf(octave, sizeof(octave), "%s", s + 2);

    return sead::FormatFixedSafeString<24>("%c%s%s (%d)", letter, accidental, octave, key);
}

static sead::FixedSafeString<8> FormatKeyText(s32 key)
{
    sead::FixedSafeString<24> name = FormatKeyName(key);

    const s32 space = name.findIndex(" ");
    if (space >= 0)
        name.trim(space);

    return sead::FixedSafeString<8>(name.cstr());
}

enum class SampleLayout
{
    Strip,
    Inline,
    Lines,
};

static void DrawRootKeyField(BankFile::VelocityRegion& velRegion)
{
    s32 rootKey = velRegion.getRootKey();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * cRegionMappingFieldEm);
    SetNextKnobText(FormatKeyText(rootKey).cstr());
    SetNextKnobReset(cPianoDefaultCenter);
    if (DragField("##RootKey", ImGuiDataType_S32, &rootKey, cRegionRangeDragSpeed, &cRegionRangeMin, &cRegionRangeMax, "%d", ImGuiSliderFlags_AlwaysClamp))
    {
        const u8 oldRootKey = velRegion.getRootKey();
        velRegion.setRootKey(static_cast<u8>(rootKey));

        if (velRegion.getRootKey() != oldRootKey)
            SetUnsavedChanges(true);
    }

    KnobTooltip("%s", FormatKeyName(rootKey).cstr());
}

static void DrawKeyGroupField(BankFile::VelocityRegion& velRegion)
{
    s32 keyGroup = velRegion.getKeyGroup();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * cRegionMappingFieldEm);
    SetNextKnobReset(0.0);
    if (DragField("##KeyGroup", ImGuiDataType_S32, &keyGroup, cRegionRangeDragSpeed, &cRegionRangeMin, &cKeyGroupMax, "%d", ImGuiSliderFlags_AlwaysClamp))
    {
        const u8 oldKeyGroup = velRegion.getKeyGroup();
        velRegion.setKeyGroup(static_cast<u8>(keyGroup));

        if (velRegion.getKeyGroup() != oldKeyGroup)
            SetUnsavedChanges(true);
    }
}

static f32 CalcInterpolationLabelWidth()
{
    return ImGui::CalcTextSize(messages::common::cInterpolation).x + ImGui::GetStyle().ItemSpacing.x;
}

static f32 CalcInterpolationFieldWidth()
{
    const ImGuiStyle& style = ImGui::GetStyle();

    f32 textWidth = 0.0f;
    for (const char* type : messages::bank::cInterpolationTypes)
        textWidth = std::max(textWidth, ImGui::CalcTextSize(type).x);

    return CalcInterpolationLabelWidth() + textWidth + style.FramePadding.x * 2.0f + ImGui::GetFrameHeight();
}

static f32 CalcSampleInlineGap()
{
    return ImGui::GetFontSize() * cRegionGapEm;
}

static bool CanInlineSampleFields(f32 width)
{
    return width >= ImGui::GetFontSize() * cRegionWaveFieldMinEm + CalcSampleInlineGap() + CalcInterpolationFieldWidth();
}

static f32 CalcSampleFieldsHeight(SampleLayout layout)
{
    if (layout == SampleLayout::Inline)
        return ImGui::GetFrameHeight();

    return ImGui::GetFrameHeight() * 2.0f + ImGui::GetStyle().ItemSpacing.y;
}

static void CenterSampleFieldsInStrip()
{
    const ImVec2 origin = ImGui::GetCursorPos();
    const f32 height = CalcRegionStripHeight();
    const f32 fieldsHeight = CalcSampleFieldsHeight(SampleLayout::Lines);

    ImGui::Dummy(ImVec2(0.0f, height));
    ImGui::SetCursorPos(ImVec2(origin.x, origin.y + ImFloor(std::max(0.0f, height - fieldsHeight) * 0.5f)));
}

static void DrawWaveFileField(BankFile::VelocityRegion& velRegion, f32 width)
{
    ImGui::SetNextItemWidth(width - TrailingButtonsWidth(1));

    Item* waveFile = velRegion.getWaveFileRef().getItem();
    if (ItemSelector("##RegionWave", sBfsar.getWaveFileList(), &waveFile))
    {
        velRegion.getWaveFileRef().attach(waveFile);
        SetUnsavedChanges(true);
    }

    ImGui::BeginDisabled(!waveFile);

    if (TrailingButton(ICON_LC_EXTERNAL_LINK "###GoWave"))
        SelectItem(waveFile);

    ImGui::EndDisabled();

    if (!waveFile)
        ImGui::SetItemTooltip("%s", messages::reference::cNoWaveFile);
}

static void DrawInterpolationField(BankFile::VelocityRegion& velRegion, f32 width)
{
    const f32 right = ImGui::GetCursorPosX() + width;
    s32 interpolationType = static_cast<s32>(sead::Mathu::min(velRegion.getInterpolationType(), IM_ARRAYSIZE(messages::bank::cInterpolationTypes) - 1));

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(messages::common::cInterpolation);

    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x);
    ImGui::SetNextItemWidth(right - ImGui::GetCursorPosX());
    if (ComboScroll("##Interpolation", &interpolationType, messages::bank::cInterpolationTypes, IM_ARRAYSIZE(messages::bank::cInterpolationTypes)))
    {
        velRegion.setInterpolationType(static_cast<u8>(interpolationType));
        SetUnsavedChanges(true);
    }
}

static void DrawSampleUI(BankFile::VelocityRegion& velRegion, SampleLayout layout)
{
    RegionHeader(messages::bank::cPanelSample);

    const f32 width = ImGui::GetContentRegionAvail().x;

    if (layout == SampleLayout::Inline)
    {
        const f32 gap = CalcSampleInlineGap();
        const f32 interpolationWidth = CalcInterpolationFieldWidth();

        DrawWaveFileField(velRegion, width - gap - interpolationWidth);

        ImGui::SameLine(0.0f, gap);
        DrawInterpolationField(velRegion, interpolationWidth);

        return;
    }

    if (layout == SampleLayout::Strip)
        CenterSampleFieldsInStrip();

    DrawWaveFileField(velRegion, width);
    DrawInterpolationField(velRegion, width);
}

void BankFile::VelocityRegion::drawPitchUI()
{
    RegionHeader(messages::common::cPitch);
    CenterRegionKnobs(CalcKnobRowWidth(cRegionPitchKnobs, IM_ARRAYSIZE(cRegionPitchKnobs)));

    f32 pitch = getPitch();

    if (DrawPitchKnobs(&pitch, ImVec2(CalcKnobWidth(cRegionPitchKnobs, IM_ARRAYSIZE(cRegionPitchKnobs)), CalcRegionStripHeight())))
    {
        setPitch(pitch);
        SetUnsavedChanges(true);
    }
}

void BankFile::VelocityRegion::drawOutputUI()
{
    RegionHeader(messages::panel::cOutput);
    CenterRegionKnobs(CalcKnobRowWidth(cRegionOutputKnobs, IM_ARRAYSIZE(cRegionOutputKnobs)));

    const ImVec2 knobSize(CalcKnobWidth(cRegionOutputKnobs, IM_ARRAYSIZE(cRegionOutputKnobs)), CalcRegionStripHeight());

    {
        u8 volume = getVolume();
        SetNextKnobReset(cRegionVolumeDefault);
        if (KnobU8(messages::common::cVolume, &volume, cRegionRangeMin, cRegionVolumeMax, knobSize))
        {
            setVolume(volume);
            SetUnsavedChanges(true);
        }

        KnobTooltip("%.3f", static_cast<f32>(volume) / static_cast<f32>(cRegionVolumeDefault));
    }

    ImGui::SameLine(0.0f, ImGui::GetFontSize() * cRegionKnobGapEm);
    {
        u8 pan = getPan();
        SetNextKnobText(FormatPanText(pan, cRegionPanCenter).cstr());
        if (KnobU8(messages::common::cPan, &pan, cRegionRangeMin, cRegionRangeMax, cRegionPanCenter, knobSize))
        {
            setPan(pan);
            SetUnsavedChanges(true);
        }

        KnobTooltip("%s", FormatPanLabel(pan, cRegionRangeMin, cRegionPanCenter, cRegionRangeMax).cstr());
    }
}

void BankFile::VelocityRegion::drawEnvelopeUI()
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 headerPos = ImGui::GetCursorScreenPos();
    const f32 noteOffWidth = ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + ImGui::CalcTextSize(messages::bank::cIgnoreNoteOff).x;
    const f32 noteOffX = headerPos.x + ImGui::GetContentRegionAvail().x - noteOffWidth;

    RegionHeader(messages::panel::cEnvelope, noteOffWidth + style.ItemSpacing.x);

    ImGui::SameLine();
    ImGui::SetCursorScreenPos(ImVec2(noteOffX, headerPos.y));
    {
        bool ignoreNoteOff = getIsIgnoreNoteOff();
        if (ImGui::Checkbox(messages::bank::cIgnoreNoteOff, &ignoreNoteOff))
        {
            setIsIgnoreNoteOff(ignoreNoteOff);
            SetUnsavedChanges(true);
        }

        ImGui::SetItemTooltip("%s", messages::bank::cIgnoreNoteOffDetail);
    }

    snd::AdshrCurve adshrCurve = getAdshrCurve();

    if (DrawAdshrEditor(adshrCurve, CalcRegionStripHeight()))
    {
        setAdshrCurve(adshrCurve);
        SetUnsavedChanges(true);
    }
}

void BankFile::Instrument::drawUI()
{
    DrawItemNameRow(this);

    PropertyRow(messages::bank::cProgramNumber);
    {
        const bool shared = IsProgramNoShared(this);
        s16 program = getProgramNo();

        if (shared)
            ImGui::PushStyleColor(ImGuiCol_Text, uicolors::Marker());

        const bool changed = DragField("##ProgramNo", ImGuiDataType_S16, &program, cProgramNoDragSpeed, &cProgramNoMin, &cProgramNoMax, "%d", ImGuiSliderFlags_AlwaysClamp);

        if (shared)
        {
            ImGui::PopStyleColor();
            ImGui::SetItemTooltip("%s", messages::bank::cProgramNumberShared);
        }

        if (changed && program != getProgramNo())
        {
            setProgramNo(program);
            SetUnsavedChanges(true);
        }
    }
}

void BankFile::drawUI()
{
    mVersion = sBfsar.getVersionForBfbnk();
    mEndian = sBfsar.getEndian();
    mFormat = sBfsar.getFormat();

    HelpMarker(messages::innerfile::cArchiveDetail);

    ImGui::BeginDisabled();
    InnerFile::drawUI();
    ImGui::EndDisabled();
}

struct RegionMappingContext
{
    BankFile::Instrument* instrument;
    BankFile::KeyRegion* keyRegion;
    BankFile::VelocityRegion* velRegion;
};

using DrawMappingFieldsFunc = void (*)(RegionMappingContext& context);

struct RegionMappingGroup
{
    const char* label;
    s32 fieldNum;
    DrawMappingFieldsFunc drawFields;
};

static constexpr s32 cRegionMappingGroupNum = 4;
static constexpr s32 cRegionMappingColumnChoices[] = { 4, 2, 1 };

struct RegionMappingLayout
{
    s32 columnNum;
    f32 width;
    f32 columnX[cRegionMappingGroupNum];
    f32 labelWidth[cRegionMappingGroupNum];
};

static void DrawKeyRangeFields(RegionMappingContext& context)
{
    const f32 spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    const f32 fieldWidth = ImGui::GetFontSize() * cRegionMappingFieldEm;

    s32 keyMin = context.keyRegion->getKeyMin();
    ImGui::SetNextItemWidth(fieldWidth);
    SetNextKnobText(FormatKeyText(keyMin).cstr());
    if (DragField("##KeyMin", ImGuiDataType_S32, &keyMin, cRegionRangeDragSpeed, &cRegionRangeMin, &cRegionRangeMax, "%d", ImGuiSliderFlags_AlwaysClamp))
    {
        snd::internal::driver::SoundThreadLock lock;
        const u8 oldKeyMin = context.keyRegion->getKeyMin();
        context.keyRegion->setKeyMin(static_cast<u8>(keyMin), *context.instrument);

        if (context.keyRegion->getKeyMin() != oldKeyMin)
            SetUnsavedChanges(true);
    }

    KnobTooltip("%s", FormatKeyName(context.keyRegion->getKeyMin()).cstr());

    s32 keyMax = context.keyRegion->getKeyMax();
    ImGui::SameLine(0.0f, spacing);
    ImGui::SetNextItemWidth(fieldWidth);
    SetNextKnobText(FormatKeyText(keyMax).cstr());
    if (DragField("##KeyMax", ImGuiDataType_S32, &keyMax, cRegionRangeDragSpeed, &cRegionRangeMin, &cRegionRangeMax, "%d", ImGuiSliderFlags_AlwaysClamp))
    {
        snd::internal::driver::SoundThreadLock lock;
        const u8 oldKeyMax = context.keyRegion->getKeyMax();
        context.keyRegion->setKeyMax(static_cast<u8>(keyMax), *context.instrument);

        if (context.keyRegion->getKeyMax() != oldKeyMax)
            SetUnsavedChanges(true);
    }

    KnobTooltip("%s", FormatKeyName(context.keyRegion->getKeyMax()).cstr());
}

static void DrawVelocityRangeFields(RegionMappingContext& context)
{
    const f32 spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    const f32 fieldWidth = ImGui::GetFontSize() * cRegionMappingFieldEm;

    BankFile::VelocityRegion* prev = context.velRegion->getPrev(*context.keyRegion);
    BankFile::VelocityRegion* next = context.velRegion->getNext(*context.keyRegion);

    s32 velMin = context.velRegion->getVelocityMin();
    ImGui::BeginDisabled(!prev);
    ImGui::SetNextItemWidth(fieldWidth);
    if (DragField("##VelMin", ImGuiDataType_S32, &velMin, cRegionRangeDragSpeed, &cRegionRangeMin, &cRegionRangeMax, "%d", ImGuiSliderFlags_AlwaysClamp) && prev)
    {
        snd::internal::driver::SoundThreadLock lock;
        velMin = sead::MathCalcCommon<s32>::clamp2(prev->getVelocityMin() + 1, velMin, context.velRegion->getVelocityMax());

        if (velMin != context.velRegion->getVelocityMin())
        {
            context.velRegion->setVelocityMin(static_cast<u8>(velMin));
            prev->setVelocityMax(static_cast<u8>(velMin - 1));
            SetUnsavedChanges(true);
        }
    }
    ImGui::EndDisabled();

    if (!prev)
        ImGui::SetItemTooltip("%s", messages::bank::cVelocityMinFixed);

    s32 velMax = context.velRegion->getVelocityMax();
    ImGui::SameLine(0.0f, spacing);
    ImGui::BeginDisabled(!next);
    ImGui::SetNextItemWidth(fieldWidth);
    if (DragField("##VelMax", ImGuiDataType_S32, &velMax, cRegionRangeDragSpeed, &cRegionRangeMin, &cRegionRangeMax, "%d", ImGuiSliderFlags_AlwaysClamp) && next)
    {
        snd::internal::driver::SoundThreadLock lock;
        velMax = sead::MathCalcCommon<s32>::clamp2(context.velRegion->getVelocityMin(), velMax, next->getVelocityMax() - 1);

        if (velMax != context.velRegion->getVelocityMax())
        {
            context.velRegion->setVelocityMax(static_cast<u8>(velMax));
            next->setVelocityMin(static_cast<u8>(velMax + 1));
            SetUnsavedChanges(true);
        }
    }
    ImGui::EndDisabled();

    if (!next)
        ImGui::SetItemTooltip("%s", messages::bank::cVelocityMaxFixed);
}

static void DrawRootKeyFields(RegionMappingContext& context)
{
    DrawRootKeyField(*context.velRegion);
}

static void DrawKeyGroupFields(RegionMappingContext& context)
{
    DrawKeyGroupField(*context.velRegion);
}

static const RegionMappingGroup cRegionMappingGroups[] = {
    { messages::bank::cKeyRange,      2, DrawKeyRangeFields      },
    { messages::bank::cVelocityRange, 2, DrawVelocityRangeFields },
    { messages::bank::cRootKey,       1, DrawRootKeyFields       },
    { messages::bank::cKeyGroup,      1, DrawKeyGroupFields      },
};

static_assert(IM_ARRAYSIZE(cRegionMappingGroups) == cRegionMappingGroupNum);

static void DrawEmptyRegionField(const char* id)
{
    s32 value = cEmptyFieldValue;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * cRegionMappingFieldEm);
    SetNextKnobText(messages::common::cNoValue);
    DragField(id, ImGuiDataType_S32, &value, 0.0f, &cEmptyFieldValue, &cEmptyFieldValue, "%d", ImGuiSliderFlags_AlwaysClamp);
}

static void DrawEmptyMappingFields(s32 fieldNum)
{
    for (s32 i = 0; i < fieldNum; i++)
    {
        if (i > 0)
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);

        ImGui::PushID(i);
        DrawEmptyRegionField("##Empty");
        ImGui::PopID();
    }
}

static f32 CalcMappingFieldsWidth(s32 fieldNum)
{
    return ImGui::GetFontSize() * cRegionMappingFieldEm * fieldNum + ImGui::GetStyle().ItemInnerSpacing.x * (fieldNum - 1);
}

static RegionMappingLayout CalcRegionMappingColumns(s32 columnNum)
{
    const f32 gap = ImGui::GetFontSize() * cRegionMappingMinGapEm;
    const f32 labelSpacing = ImGui::GetStyle().ItemSpacing.x;

    RegionMappingLayout layout = {};
    layout.columnNum = columnNum;

    f32 fieldsWidth[cRegionMappingGroupNum] = {};

    for (s32 i = 0; i < cRegionMappingGroupNum; i++)
    {
        const s32 column = i % columnNum;

        layout.labelWidth[column] = std::max(layout.labelWidth[column], ImGui::CalcTextSize(cRegionMappingGroups[i].label).x);
        fieldsWidth[column] = std::max(fieldsWidth[column], CalcMappingFieldsWidth(cRegionMappingGroups[i].fieldNum));
    }

    f32 x = 0.0f;

    for (s32 column = 0; column < columnNum; column++)
    {
        layout.columnX[column] = x;
        x += layout.labelWidth[column] + labelSpacing + fieldsWidth[column] + gap;
    }

    layout.width = x - gap;

    return layout;
}

static void JustifyRegionMappingColumns(RegionMappingLayout* layout, f32 maxWidth)
{
    const f32 spare = maxWidth - layout->width;

    if (layout->columnNum < 2 || spare <= 0.0f)
        return;

    const f32 extraGap = spare / static_cast<f32>(layout->columnNum - 1);

    for (s32 column = 1; column < layout->columnNum; column++)
        layout->columnX[column] = ImFloor(layout->columnX[column] + extraGap * column);

    layout->width = maxWidth;
}

static RegionMappingLayout CalcRegionMappingLayout(f32 maxWidth)
{
    RegionMappingLayout layout = {};

    for (s32 columnNum : cRegionMappingColumnChoices)
    {
        layout = CalcRegionMappingColumns(columnNum);

        if (layout.width <= maxWidth)
            break;
    }

    JustifyRegionMappingColumns(&layout, maxWidth);

    return layout;
}

static void DrawRegionMappingGrid(RegionMappingContext& context, const RegionMappingLayout& layout)
{
    const f32 startX = ImGui::GetCursorPosX();
    const f32 labelSpacing = ImGui::GetStyle().ItemSpacing.x;
    const bool hasRegion = context.velRegion != nullptr;

    ImGui::BeginDisabled(!hasRegion);

    for (s32 i = 0; i < cRegionMappingGroupNum; i++)
    {
        const RegionMappingGroup& group = cRegionMappingGroups[i];
        const s32 column = i % layout.columnNum;

        if (column > 0)
            ImGui::SameLine();

        ImGui::SetCursorPosX(startX + layout.columnX[column]);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(group.label);

        ImGui::SameLine();
        ImGui::SetCursorPosX(startX + layout.columnX[column] + layout.labelWidth[column] + labelSpacing);

        ImGui::PushID(i);

        if (hasRegion)
            group.drawFields(context);
        else
            DrawEmptyMappingFields(group.fieldNum);

        ImGui::PopID();
    }

    ImGui::EndDisabled();
}

static sead::FixedSafeString<160> FormatMidiStatus()
{
    const u32 midiCount = MidiConnectedCount();

    sead::FixedSafeString<160> status;

    if (midiCount == 1)
        status = messages::keyboard::cMidiOn;
    else if (midiCount > 1)
        status.format(messages::keyboard::cMidiDevicesFormat, midiCount);
    else
        status = messages::keyboard::cMidiOff;

    return status;
}

static f32 CalcMidiControlsWidth()
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 midiWidth = ImGui::CalcTextSize(ICON_LC_PIANO).x + style.FramePadding.x * 2.0f;
    const f32 helpWidth = ImGui::CalcTextSize(ICON_LC_KEYBOARD).x + style.FramePadding.x * 2.0f;

    return midiWidth + helpWidth + style.ItemSpacing.x;
}

static void DrawKeyboardHelpPopup()
{
    if (!ImGui::BeginPopup("Keyboard Help"))
        return;

    ImGui::TextUnformatted(messages::keyboard::cHelpTitle);
    ImGui::Separator();
    ImGui::TextUnformatted(messages::keyboard::cHelpLowerRow);
    ImGui::TextUnformatted(messages::keyboard::cHelpLowerWhite);
    ImGui::TextUnformatted(messages::keyboard::cHelpLowerBlack);
    ImGui::TextUnformatted(messages::keyboard::cHelpUpperRow);
    ImGui::TextUnformatted(messages::keyboard::cHelpUpperWhite);
    ImGui::TextUnformatted(messages::keyboard::cHelpUpperBlack);
    ImGui::Separator();
    ImGui::TextUnformatted(messages::keyboard::cHelpControls);
    ImGui::TextUnformatted(messages::keyboard::cHelpOctave);
    ImGui::TextUnformatted(messages::keyboard::cHelpReset);
    ImGui::EndPopup();
}

static void DrawMidiButton()
{
    const bool connected = MidiConnectedCount() > 0;

    ImGui::PushStyleColor(ImGuiCol_Text, connected ? uicolors::Success() : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    const bool clicked = ImGui::SmallButton(ICON_LC_PIANO "###MidiSettings");
    ImGui::PopStyleColor();

    if (clicked)
        OpenPreferencesWindow(PreferencesPage::Midi);

    if (ImGui::BeginItemTooltip())
    {
        ImGui::TextUnformatted(FormatMidiStatus().cstr());
        ImGui::TextDisabled("%s", messages::keyboard::cMidiSettingsHint);
        ImGui::EndTooltip();
    }
}

static void DrawMidiControls()
{
    DrawMidiButton();

    ImGui::SameLine();
    if (ImGui::SmallButton(ICON_LC_KEYBOARD "###KeyboardHelp"))
        ImGui::OpenPopup("Keyboard Help");

    ImGui::SetItemTooltip("%s", messages::keyboard::cHelpTitle);

    DrawKeyboardHelpPopup();
}

static void DrawRegionHeaderRow(BankFile::Instrument* instrument)
{
    const f32 inset = ImFloor(ImGui::GetFontSize() * cRegionPanelPadXEm);
    const RegionMappingLayout layout = CalcRegionMappingLayout(ImGui::GetContentRegionAvail().x - inset * 2.0f);

    RegionMappingContext context = { instrument, nullptr, nullptr };
    context.velRegion = FindSelectedRegion(instrument, &context.keyRegion);

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + inset);
    DrawRegionMappingGrid(context, layout);
}

struct RegionPanelLayout
{
    enum class Mode
    {
        Wide,
        Split,
        Stacked,
    };

    Mode mode;
    SampleLayout sampleLayout;
    f32 height;
    f32 sampleWidth;
    f32 pitchWidth;
    f32 outputWidth;
    f32 envelopeWidth;
};

static f32 CalcRegionPanelHeight(f32 contentHeight, f32 paddingY)
{
    return ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y + contentHeight + paddingY * 2.0f;
}

static RegionPanelLayout CalcRegionPanelLayout(f32 avail, const ImVec2& padding)
{
    const f32 em = ImGui::GetFontSize();
    const f32 spacingY = ImGui::GetStyle().ItemSpacing.y;
    const f32 envelopeShrink = em * (cRegionCurveEm - cRegionCurveMinEm);
    const f32 stripPanelHeight = CalcRegionPanelHeight(CalcRegionStripHeight(), padding.y);

    RegionPanelLayout layout;
    layout.sampleWidth = CalcInterpolationFieldWidth();
    layout.pitchWidth = CalcKnobRowWidth(cRegionPitchKnobs, IM_ARRAYSIZE(cRegionPitchKnobs));
    layout.outputWidth = CalcKnobRowWidth(cRegionOutputKnobs, IM_ARRAYSIZE(cRegionOutputKnobs));
    layout.envelopeWidth = CalcAdshrEditorWidth(false);

    const f32 sampleMinWidth = CalcInterpolationLabelWidth() + em * cRegionSampleComboMinEm;
    const f32 envelopeMinWidth = layout.envelopeWidth - envelopeShrink;
    const f32 knobPanelsWidth = layout.pitchWidth + layout.outputWidth + padding.x * 4.0f;

    if (avail >= sampleMinWidth + knobPanelsWidth + envelopeMinWidth + padding.x * 4.0f)
    {
        const f32 sampleRoom = avail - knobPanelsWidth - layout.envelopeWidth - padding.x * 4.0f;
        const f32 sampleShortfall = std::max(0.0f, layout.sampleWidth - sampleRoom);

        layout.mode = RegionPanelLayout::Mode::Wide;
        layout.sampleLayout = SampleLayout::Strip;
        layout.height = stripPanelHeight;
        layout.envelopeWidth = ImCeil(std::max(envelopeMinWidth, layout.envelopeWidth - sampleShortfall));

        return layout;
    }

    layout.sampleLayout = CanInlineSampleFields(avail - padding.x * 2.0f) ? SampleLayout::Inline : SampleLayout::Lines;

    const f32 samplePanelHeight = CalcRegionPanelHeight(CalcSampleFieldsHeight(layout.sampleLayout), padding.y);

    if (avail >= knobPanelsWidth + CalcAdshrEditorWidth(true) + padding.x * 2.0f)
    {
        layout.mode = RegionPanelLayout::Mode::Split;
        layout.height = samplePanelHeight + spacingY + stripPanelHeight;

        return layout;
    }

    layout.mode = RegionPanelLayout::Mode::Stacked;
    layout.height = samplePanelHeight + (spacingY + stripPanelHeight) * 2.0f;

    return layout;
}

static void DrawNoRegionPanel(const RegionPanelLayout& layout)
{
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, layout.height);
    const ImVec2 textSize = ImGui::CalcTextSize(messages::bank::cNoRegionSelected);
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    ImGui::Dummy(size);
    drawList->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::GetColorU32(ImGuiCol_TableBorderStrong));
    drawList->AddText(ImVec2(pos.x + (size.x - textSize.x) * 0.5f, pos.y + (size.y - textSize.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), messages::bank::cNoRegionSelected);
}

static void DrawRegionPanelRow(BankFile::VelocityRegion& velRegion, const RegionPanelLayout& layout, bool withSample)
{
    const s32 columnNum = withSample ? 4 : 3;
    const ImGuiTableColumnFlags envelopeFlags = withSample ? ImGuiTableColumnFlags_WidthFixed : ImGuiTableColumnFlags_WidthStretch;

    if (!ImGui::BeginTable(withSample ? "RegionPanels" : "RegionKnobPanels", columnNum, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV))
        return;

    if (withSample)
        ImGui::TableSetupColumn("Sample", ImGuiTableColumnFlags_WidthStretch, layout.sampleWidth);

    ImGui::TableSetupColumn("Pitch", ImGuiTableColumnFlags_WidthFixed, layout.pitchWidth);
    ImGui::TableSetupColumn("Output", ImGuiTableColumnFlags_WidthFixed, layout.outputWidth);
    ImGui::TableSetupColumn("Envelope", envelopeFlags, layout.envelopeWidth);

    if (withSample)
    {
        ImGui::TableNextColumn();
        DrawSampleUI(velRegion, layout.sampleLayout);
    }

    ImGui::TableNextColumn();
    velRegion.drawPitchUI();
    ImGui::TableNextColumn();
    velRegion.drawOutputUI();
    ImGui::TableNextColumn();
    velRegion.drawEnvelopeUI();

    ImGui::EndTable();
}

template <typename DrawPanel>
static void DrawSingleRegionPanel(const char* id, DrawPanel drawPanel)
{
    if (!ImGui::BeginTable(id, 1, ImGuiTableFlags_BordersOuter))
        return;

    ImGui::TableSetupColumn(id, ImGuiTableColumnFlags_WidthStretch);

    ImGui::TableNextColumn();
    drawPanel();

    ImGui::EndTable();
}

static void DrawPitchOutputPanels(BankFile::VelocityRegion& velRegion)
{
    if (!ImGui::BeginTable("RegionPitchOutput", 2, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV))
        return;

    ImGui::TableSetupColumn("Pitch", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Output", ImGuiTableColumnFlags_WidthStretch);

    ImGui::TableNextColumn();
    velRegion.drawPitchUI();
    ImGui::TableNextColumn();
    velRegion.drawOutputUI();

    ImGui::EndTable();
}

void DrawRegionPanels(BankFile::Instrument* instrument)
{
    BankFile::KeyRegion* keyRegion = nullptr;
    BankFile::VelocityRegion* velRegion = FindSelectedRegion(instrument, &keyRegion);

    const f32 em = ImGui::GetFontSize();
    const ImVec2 padding(ImFloor(em * cRegionPanelPadXEm), ImFloor(em * cRegionPanelPadYEm));
    const RegionPanelLayout layout = CalcRegionPanelLayout(ImGui::GetContentRegionAvail().x, padding);

    if (!velRegion)
    {
        DrawNoRegionPanel(layout);
        return;
    }

    const auto drawSample = [&]()
    {
        DrawSampleUI(*velRegion, layout.sampleLayout);
    };

    const auto drawEnvelope = [&]()
    {
        velRegion->drawEnvelopeUI();
    };

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, padding);

    switch (layout.mode)
    {
    case RegionPanelLayout::Mode::Wide:
        DrawRegionPanelRow(*velRegion, layout, true);
        break;

    case RegionPanelLayout::Mode::Split:
        DrawSingleRegionPanel("RegionSample", drawSample);

        SetRegionPanelFill(true);
        DrawRegionPanelRow(*velRegion, layout, false);
        SetRegionPanelFill(false);
        break;

    case RegionPanelLayout::Mode::Stacked:
        SetRegionPanelFill(true);
        DrawSingleRegionPanel("RegionSample", drawSample);
        DrawPitchOutputPanels(*velRegion);
        DrawSingleRegionPanel("RegionEnvelope", drawEnvelope);
        SetRegionPanelFill(false);
        break;
    }

    ImGui::PopStyleVar();
}

static BankFile *sCurrentEditBank = nullptr;

bool DrawNextProgramButton(const char* id)
{
    const f32 width = ImGui::CalcTextSize(ICON_LC_WAND_2).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SameLine();
    const f32 avail = ImGui::GetContentRegionAvail().x;

    if (avail > width)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - width);

    const bool pressed = ImGui::Button(sead::FormatFixedSafeString<64>(ICON_LC_WAND_2 "%s", id).cstr());
    ImGui::SetItemTooltip("%s", messages::bank::cNextProgramNumberTooltip);

    return pressed;
}

InstanciateItemCallback CreateInstrumentFunc(bool clear)
{
    static s16 sAddProgramNo = 0;
    static sead::FixedSafeString<256> sAddName;

    if (clear)
    {
        sAddName = messages::common::cInstrument;
        sAddProgramNo = sCurrentEditBank ? FindNextAvailableProgramNo(sCurrentEditBank->getInstrumentList(), nullptr) : 0;
    }

    if (sCurrentEditBank)
    {
        ImGui::SetNextItemWidth(cAddInstrumentNameWidth);
        ImGui::InputText(messages::common::cName, sAddName.getBuffer(), sAddName.getBufferSize());

        ImGui::SetNextItemWidth(cAddInstrumentProgramWidth);
        ImGui::InputScalar(messages::bank::cProgramNumber, ImGuiDataType_S16, &sAddProgramNo, &cAddInstrumentProgramStep, &cAddInstrumentProgramStepFast);

        if (DrawNextProgramButton("###NextAvailAdd"))
            sAddProgramNo = FindNextAvailableProgramNo(sCurrentEditBank->getInstrumentList(), nullptr);
    }

    auto doCreate = []() -> Item *
    {
        BankFile::Instrument *instr = new BankFile::Instrument();
        instr->setEnableName(true);
        instr->getName() = sAddName.isEmpty() ? messages::common::cInstrument : sAddName.cstr();
        instr->setProgramNo(sAddProgramNo);

        BankFile::KeyRegion *keyRegion = new BankFile::KeyRegion(0, 127);
        keyRegion->setId(0);
        keyRegion->setEnableName(true);
        keyRegion->getName() = "KeyRegion";

        instr->getKeyRegionList().pushBack(keyRegion);

        BankFile::VelocityRegion *velRegion = new BankFile::VelocityRegion(0, 127);
        velRegion->setId(0);
        velRegion->setEnableName(true);
        velRegion->getName() = "VelocityRegion";

        keyRegion->getVelocityRegionList().pushBack(velRegion);

        return instr;
    };

    return doCreate;
}

static void DuplicateInstrument(BankFile* bankFile, const BankFile::Instrument* src)
{
    Item::List& bankList = bankFile->getInstrumentList();

    BankFile::Instrument* dup = new BankFile::Instrument();
    dup->setEnableName(src->isEnableName());
    dup->getName() = src->getName();

    for (const Item* keyItem : src->getKeyRegionList())
    {
        const BankFile::KeyRegion* srcKeyRegion = static_cast<const BankFile::KeyRegion*>(keyItem);
        BankFile::KeyRegion* keyRegion = new BankFile::KeyRegion(srcKeyRegion->getKeyMin(), srcKeyRegion->getKeyMax());

        for (const Item* velItem : srcKeyRegion->getVelocityRegionList())
        {
            const BankFile::VelocityRegion* srcVelRegion = static_cast<const BankFile::VelocityRegion*>(velItem);
            BankFile::VelocityRegion* velRegion = new BankFile::VelocityRegion(srcVelRegion->getVelocityMin(), srcVelRegion->getVelocityMax());

            CopyVelocityRegion(velRegion, srcVelRegion);
            keyRegion->getVelocityRegionList().pushBack(velRegion);
        }

        dup->getKeyRegionList().pushBack(keyRegion);
    }

    dup->setProgramNo(FindNextAvailableProgramNo(bankList));
    bankList.pushBack(dup);
    sBfsar.updateList(bankList);
    SetUnsavedChanges(true);
}

static const WaveFile* GetInstrumentFirstWave(const BankFile::Instrument* instrument)
{
    if (!instrument || instrument->getKeyRegionList().size() == 0)
        return nullptr;

    const auto* keyRegion = static_cast<const BankFile::KeyRegion*>(instrument->getKeyRegionList().front()->val());

    if (keyRegion->getVelocityRegionList().size() == 0)
        return nullptr;

    const auto* velRegion = static_cast<const BankFile::VelocityRegion*>(keyRegion->getVelocityRegionList().front()->val());

    if (!velRegion->getWaveFileRef().isAttached())
        return nullptr;

    return static_cast<const WaveFile*>(velRegion->getWaveFileRef().getItem());
}

static bool CanAutoNameInstrument(const Item* item)
{
    const WaveFile* wave = GetInstrumentFirstWave(static_cast<const BankFile::Instrument*>(item));

    return wave && !wave->getName().isEmpty();
}

static void AutoNameInstruments(const std::vector<Item*>& instruments)
{
    for (Item* item : instruments)
    {
        if (!CanAutoNameInstrument(item))
            continue;

        BankFile::Instrument* instrument = static_cast<BankFile::Instrument*>(item);
        const WaveFile* wave = GetInstrumentFirstWave(instrument);

        if (instrument->isEnableName() && instrument->getName() == wave->getName())
            continue;

        instrument->getName() = wave->getName();
        instrument->setEnableName(true);
        SetUnsavedChanges(true);
    }
}

static void SortInstrumentsByNumber(BankFile* bankFile)
{
    Item::List& list = bankFile->getInstrumentList();
    std::vector<BankFile::Instrument*> instruments;

    for (Item* item : list)
        instruments.push_back(static_cast<BankFile::Instrument*>(item));

    const std::vector<BankFile::Instrument*> oldOrder = instruments;

    std::stable_sort(instruments.begin(), instruments.end(),
                     [](const BankFile::Instrument* a, const BankFile::Instrument* b)
                     { return a->getProgramNo() < b->getProgramNo(); });

    if (instruments == oldOrder)
        return;

    for (BankFile::Instrument* instrument : instruments)
        static_cast<Item*>(instrument)->erase();

    for (BankFile::Instrument* instrument : instruments)
        list.pushBack(instrument);

    sBfsar.updateList(list);
    SetUnsavedChanges(true);
}

static void InstrumentContextMenuFunc(Item* item)
{
    if (!sCurrentEditBank)
        return;

    if (item && ContextMenuItem(ICON_LC_COPY, messages::common::cDuplicate))
        DuplicateInstrument(sCurrentEditBank, static_cast<BankFile::Instrument*>(item));

    ContextMenuSeparator();

    if (item)
    {
        const std::vector<Item*> targets = IsItemMultiSelected(item) ? sMultiSelectedItems : std::vector<Item*>{item};
        const bool canAutoName = std::any_of(targets.begin(), targets.end(), CanAutoNameInstrument);

        if (ContextMenuItem(ICON_LC_REPLACE, messages::common::cReplace))
            RequestReplaceInstrument(item, sCurrentEditBank);

        if (ContextMenuItem(ICON_LC_WAND_SPARKLES, messages::menu::cAutoName, canAutoName, messages::menu::cAutoNameNoWaveFile))
            AutoNameInstruments(targets);

        if (canAutoName)
            ImGui::SetItemTooltip("%s", messages::menu::cAutoNameDetail);
    }

    if (ContextMenuItem(ICON_LC_ARROW_DOWN_0_1, messages::menu::cSortByNumber))
        SortInstrumentsByNumber(sCurrentEditBank);

    ContextMenuSeparator();

    if (ContextMenuItem(ICON_LC_FILE_INPUT, messages::common::cImport))
        RequestImportInstrument(sCurrentEditBank);

    if (item && ContextMenuItem(ICON_LC_FILE_OUTPUT, messages::common::cExport))
        RequestExportInstrument(item);
}

static f32 ClampRegionPaneHeight(f32 regionHeight, f32 totalHeight)
{
    return sead::MathCalcCommon<f32>::clamp2(cRegionPaneMinHeight, regionHeight, std::max(cRegionPaneMinHeight, totalHeight - cRegionPaneHeightReserve));
}

static void DrawInstrumentListPane(Item::List& instrumentList, f32 height)
{
    if (ImGui::BeginChild("Instruments", ImVec2(0.0f, height)))
    {
        SetNextItemListFooter(&DrawMidiControls, CalcMidiControlsWidth());
        DrawAllItemsUI("Instrument", messages::common::cInstrument, instrumentList, &CreateInstrumentFunc, nullptr, &InstrumentContextMenuFunc);
    }

    ImGui::EndChild();
}

static void DrawRegionSplitter(f32* regionHeight)
{
    ImGui::InvisibleButton("##RegionSplitter", ImVec2(ImGui::GetContentRegionAvail().x, cSplitterHeight));

    const bool active = ImGui::IsItemActive();
    const bool highlighted = active || ImGui::IsItemHovered();

    if (highlighted)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

    if (active)
        *regionHeight -= ImGui::GetIO().MouseDelta.y;

    const ImVec2 splitterMin = ImGui::GetItemRectMin();
    const ImVec2 splitterMax = ImGui::GetItemRectMax();
    const f32 centerY = (splitterMin.y + splitterMax.y) * 0.5f;
    const ImU32 lineColor = highlighted ? GetPianoRollPalette().splitterHover : ImGui::GetColorU32(ImGuiCol_Separator);

    ImGui::GetWindowDrawList()->AddLine(ImVec2(splitterMin.x + cSplitterLineInset, centerY), ImVec2(splitterMax.x - cSplitterLineInset, centerY), lineColor, cSplitterLineThickness);
}

static f32 DrawKeyboardPane(f32 regionHeight)
{
    const f32 startY = ImGui::GetCursorScreenPos().y;
    f32 keyboardHeight = 0.0f;

    if (ImGui::BeginChild("Keyboard", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border, ImGuiWindowFlags_NoScrollWithMouse))
    {
        BankFile::Instrument* instrument = (sSelectedItem && sSelectedItem->getItemType() == Item::ItemType::BankFileInstrument)
            ? static_cast<BankFile::Instrument*>(sSelectedItem)
            : nullptr;

        DrawRegionHeaderRow(instrument);

        ImGui::Separator();

        const f32 keysHeight = ImFloor(ImGui::GetFontSize() * cPianoKeysHeightEm) + GetPianoRulerHeight();
        DrawKeyboardWithRegions(ImGui::GetContentRegionAvail().x, keysHeight, regionHeight, cRegionRangeMin, cRegionRangeMax, instrument);

        keyboardHeight = ImGui::GetCursorScreenPos().y - startY + ImGui::GetScrollY() + ImGui::GetStyle().WindowPadding.y;
    }

    ImGui::EndChild();

    return keyboardHeight;
}

void BankFile::drawFileUI()
{
    sCurrentEditBank = this;

    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID keyboardHeightId = ImGui::GetID("##KeyboardPaneHeight");
    const ImGuiID regionHeightId = ImGui::GetID("##RegionPaneHeight");

    const f32 totalHeight = ImGui::GetContentRegionAvail().y;
    const f32 keyboardHeight = storage->GetFloat(keyboardHeightId, cKeyboardPaneInitialHeight);
    f32 regionHeight = ClampRegionPaneHeight(storage->GetFloat(regionHeightId, cRegionPaneInitialHeight), totalHeight);

    const f32 splitterFootprint = cSplitterHeight + ImGui::GetStyle().ItemSpacing.y;
    const f32 listHeight = std::max(ImGui::GetFrameHeight(), totalHeight - keyboardHeight - splitterFootprint);

    DrawInstrumentListPane(mInstrumentList, listHeight);
    DrawRegionSplitter(&regionHeight);

    const f32 measuredKeyboardHeight = DrawKeyboardPane(regionHeight);

    if (measuredKeyboardHeight > 0.0f)
        storage->SetFloat(keyboardHeightId, measuredKeyboardHeight);

    storage->SetFloat(regionHeightId, regionHeight);
}
