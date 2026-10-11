#include <ui/bank/BankFileEditor.h>

#include <bfsar/seq/SeqCommand.h>

#include <ui/widgets/ContextMenu.h>
#include <ui/widgets/Knob.h>
#include <ui/Messages.h>
#include <ui/bank/PianoRollPalette.h>
#include <ui/app/PopupMgr.h>
#include <ui/app/UI.h>

#include <imgui/imgui_custom.h>

#include <algorithm>
#include <cstdio>
#include <unordered_set>
#include <vector>

static constexpr f32 cPianoKeysHeightEm = 3.5f;

static constexpr f32 cRegionMappingFieldEm = 3.0f;
static constexpr f32 cRegionRangeDragSpeed = 0.25f;
static constexpr f32 cRegionPanelPadXEm    = 0.5f;
static constexpr f32 cRegionPanelPadYEm    = 0.3f;

static const char* cRegionPitchKnobs[]  = { messages::param::cCoarse, messages::param::cFine };
static const char* cRegionOutputKnobs[] = { messages::param::cVolume, messages::param::cPan };

static const char* cRegionKeyRangeLabel      = messages::bank::cKeyRange;
static const char* cRegionVelocityRangeLabel = messages::bank::cVelocityRange;
static const char* cRegionRootKeyLabel       = messages::bank::cRootKey;
static const char* cRegionKeyGroupLabel      = messages::bank::cKeyGroup;

static void RegionLabel(const char* label)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x);
}

static void NextRegionGroup(const char* label, s32 fieldNum)
{
    const f32 em = ImGui::GetFontSize();
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 width = ImGui::CalcTextSize(label).x + style.ItemSpacing.x + em * cRegionMappingFieldEm * fieldNum + style.ItemInnerSpacing.x * (fieldNum - 1);

    ImGui::SameLine(0.0f, em * cRegionGapEm);
    if (ImGui::GetContentRegionAvail().x < width)
        ImGui::NewLine();

    RegionLabel(label);
}

static sead::FixedSafeString<24> FormatKeyName(s32 key)
{
    if (key < 0 || key >= static_cast<s32>(MmlCommandNote::sKeysNum))
        return sead::FixedSafeString<24>(messages::bank::cNoValue);

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

void BankFile::VelocityRegion::drawMappingUI()
{
    static const ImU8 cU8Min = 0;
    static const ImU8 cRootKeyMax = 127;
    static const ImU8 cKeyGroupMax = 15;

    const f32 fieldWidth = ImGui::GetFontSize() * cRegionMappingFieldEm;

    NextRegionGroup(cRegionRootKeyLabel, 1);
    {
        u8 rootKey = getRootKey();
        ImGui::SetNextItemWidth(fieldWidth);
        SetNextKnobText(FormatKeyText(rootKey).cstr());
        SetNextKnobReset(cPianoDefaultCenter);
        if (DragField("##RootKey", ImGuiDataType_U8, &rootKey, cRegionRangeDragSpeed, &cU8Min, &cRootKeyMax, "%d", ImGuiSliderFlags_AlwaysClamp))
        {
            const u8 oldRootKey = getRootKey();
            setRootKey(rootKey);

            if (getRootKey() != oldRootKey)
                SetUnsavedChanges(true);
        }

        KnobTooltip("%s", FormatKeyName(rootKey).cstr());
    }

    NextRegionGroup(cRegionKeyGroupLabel, 1);
    {
        u8 keyGroup = getKeyGroup();
        ImGui::SetNextItemWidth(fieldWidth);
        SetNextKnobReset(0.0);
        if (DragField("##KeyGroup", ImGuiDataType_U8, &keyGroup, cRegionRangeDragSpeed, &cU8Min, &cKeyGroupMax, "%d", ImGuiSliderFlags_AlwaysClamp))
        {
            const u8 oldKeyGroup = getKeyGroup();
            setKeyGroup(keyGroup);

            if (getKeyGroup() != oldKeyGroup)
                SetUnsavedChanges(true);
        }
    }
}

void BankFile::VelocityRegion::drawSampleUI()
{
    static const char* cInterpolationLabel = messages::bank::cInterpolation;

    RegionHeader(messages::bank::cPanelSample);

    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 spacing = style.ItemInnerSpacing.x;
    const f32 height = CalcRegionStripHeight();
    const f32 frameHeight = ImGui::GetFrameHeight();
    const f32 width = ImGui::GetContentRegionAvail().x;
    const ImVec2 origin = ImGui::GetCursorPos();
    const f32 rowY = origin.y + ImFloor(std::max(0.0f, height - frameHeight * 2.0f - style.ItemSpacing.y) * 0.5f);

    ImGui::Dummy(ImVec2(0.0f, height));

    ImGui::SetCursorPos(ImVec2(origin.x, rowY));
    {
        const f32 buttonWidth = ImGui::CalcTextSize(ICON_LC_EXTERNAL_LINK).x + style.FramePadding.x * 2.0f;
        ImGui::SetNextItemWidth(width - spacing - buttonWidth);

        Item* waveFile = getWaveFileRef().getItem();
        if (ItemSelector("##RegionWave", sBfsar.getWaveFileList(), &waveFile))
        {
            getWaveFileRef().attach(waveFile);
            SetUnsavedChanges(true);
        }

        if (!waveFile)
        {
            ImGui::BeginDisabled();
        }

        ImGui::SameLine(0.0f, spacing);
        if (ImGui::Button(ICON_LC_EXTERNAL_LINK "###GoWave"))
        {
            SelectItem(waveFile);
        }

        if (!waveFile)
        {
            ImGui::EndDisabled();

            SetDisabledTooltip(messages::reference::cNoWaveFile);
        }
    }

    ImGui::SetCursorPos(ImVec2(origin.x, rowY + frameHeight + style.ItemSpacing.y));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(cInterpolationLabel);
    ImGui::SameLine(0.0f, style.ItemSpacing.x);
    {
        static const char* sInterpolationTypes[] = {
            messages::bank::cInterpolationPolyphase,
            messages::bank::cInterpolationLinear,
        };

        u32 interpolationType = sead::Mathu::min(getInterpolationType(), IM_ARRAYSIZE(sInterpolationTypes) - 1);

        ImGui::SetNextItemWidth(origin.x + width - ImGui::GetCursorPosX());
        if (ComboScroll("##Interpolation", (s32*)&interpolationType, sInterpolationTypes, IM_ARRAYSIZE(sInterpolationTypes)))
        {
            setInterpolationType(interpolationType);
            SetUnsavedChanges(true);
        }
    }
}

void BankFile::VelocityRegion::drawPitchUI()
{
    RegionHeader(messages::panel::cPitch);
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
    static const ImU8 cU8Min = 0;
    static const ImU8 cPanMax = 127;
    static const ImU8 cPanCenter = 64;
    static const ImU8 cVolumeMax = 255;
    static const ImU8 cRegionVolumeDefault = 127;

    RegionHeader(messages::panel::cOutput);
    CenterRegionKnobs(CalcKnobRowWidth(cRegionOutputKnobs, IM_ARRAYSIZE(cRegionOutputKnobs)));

    const ImVec2 knobSize(CalcKnobWidth(cRegionOutputKnobs, IM_ARRAYSIZE(cRegionOutputKnobs)), CalcRegionStripHeight());

    {
        u8 volume = getVolume();
        SetNextKnobReset(cRegionVolumeDefault);
        if (KnobU8(messages::param::cVolume, &volume, cU8Min, cVolumeMax, knobSize))
        {
            setVolume(volume);
            SetUnsavedChanges(true);
        }

        KnobTooltip("%.3f", static_cast<f32>(volume) / 127.0f);
    }

    ImGui::SameLine(0.0f, ImGui::GetFontSize() * cRegionKnobGapEm);
    {
        u8 pan = getPan();
        SetNextKnobText(FormatPanText(pan, cPanCenter).cstr());
        if (KnobU8(messages::param::cPan, &pan, cU8Min, cPanMax, cPanCenter, knobSize))
        {
            setPan(pan);
            SetUnsavedChanges(true);
        }

        KnobTooltip("%s", FormatPanLabel(pan, cU8Min, cPanCenter, cPanMax).cstr());
    }
}

void BankFile::VelocityRegion::drawEnvelopeUI()
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 headerPos = ImGui::GetCursorScreenPos();
    const f32 noteOffWidth = ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + ImGui::CalcTextSize(messages::bank::cIgnoreNoteOff).x;
    const f32 noteOffX = headerPos.x + ImGui::GetContentRegionAvail().x - noteOffWidth;

    RegionHeader(messages::panel::cEnvelope,noteOffWidth + style.ItemSpacing.x);

    ImGui::SameLine();
    ImGui::SetCursorScreenPos(ImVec2(noteOffX, headerPos.y));
    {
        bool ignoreNoteOff = getIsIgnoreNoteOff();
        if (ImGui::Checkbox(messages::bank::cIgnoreNoteOff, &ignoreNoteOff))
        {
            setIsIgnoreNoteOff(ignoreNoteOff);
            SetUnsavedChanges(true);
        }

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("%s", messages::bank::cIgnoreNoteOffDetail);
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
    static const s16 cProgramMin = 0;
    static const s16 cProgramMax = 32766;

    DrawItemNameRow(this);

    PropertyRow(messages::bank::cProgramNumber);
    {
        s16 program = getProgramNo();
        if (DragField("##ProgramNo", ImGuiDataType_S16, &program, cRegionRangeDragSpeed, &cProgramMin, &cProgramMax, "%d", ImGuiSliderFlags_AlwaysClamp))
        {
            const s16 oldProgram = getProgramNo();
            setProgramNo(program);

            if (getProgramNo() != oldProgram)
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

static void DrawRegionRanges(BankFile::Instrument& instrument, BankFile::KeyRegion& keyRegion, BankFile::VelocityRegion& velRegion)
{
    static const s32 cRangeMin = 0;
    static const s32 cRangeMax = 127;

    const f32 spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    const f32 fieldWidth = ImGui::GetFontSize() * cRegionMappingFieldEm;

    RegionLabel(cRegionKeyRangeLabel);
    {
        s32 keyMin = keyRegion.getKeyMin();
        ImGui::SetNextItemWidth(fieldWidth);
        SetNextKnobText(FormatKeyText(keyMin).cstr());
        if (DragField("##KeyMin", ImGuiDataType_S32, &keyMin, cRegionRangeDragSpeed, &cRangeMin, &cRangeMax, "%d", ImGuiSliderFlags_AlwaysClamp))
        {
            snd::internal::driver::SoundThreadLock lock;
            const u8 oldKeyMin = keyRegion.getKeyMin();
            keyRegion.setKeyMin(static_cast<u8>(keyMin), instrument);

            if (keyRegion.getKeyMin() != oldKeyMin)
                SetUnsavedChanges(true);
        }

        KnobTooltip("%s", FormatKeyName(keyRegion.getKeyMin()).cstr());

        s32 keyMax = keyRegion.getKeyMax();
        ImGui::SameLine(0.0f, spacing);
        ImGui::SetNextItemWidth(fieldWidth);
        SetNextKnobText(FormatKeyText(keyMax).cstr());
        if (DragField("##KeyMax", ImGuiDataType_S32, &keyMax, cRegionRangeDragSpeed, &cRangeMin, &cRangeMax, "%d", ImGuiSliderFlags_AlwaysClamp))
        {
            snd::internal::driver::SoundThreadLock lock;
            const u8 oldKeyMax = keyRegion.getKeyMax();
            keyRegion.setKeyMax(static_cast<u8>(keyMax), instrument);

            if (keyRegion.getKeyMax() != oldKeyMax)
                SetUnsavedChanges(true);
        }

        KnobTooltip("%s", FormatKeyName(keyRegion.getKeyMax()).cstr());
    }

    NextRegionGroup(cRegionVelocityRangeLabel, 2);
    {
        BankFile::VelocityRegion* prev = velRegion.getPrev(keyRegion);
        BankFile::VelocityRegion* next = velRegion.getNext(keyRegion);

        s32 velMin = velRegion.getVelocityMin();
        ImGui::BeginDisabled(!prev);
        ImGui::SetNextItemWidth(fieldWidth);
        if (DragField("##VelMin", ImGuiDataType_S32, &velMin, cRegionRangeDragSpeed, &cRangeMin, &cRangeMax, "%d", ImGuiSliderFlags_AlwaysClamp) && prev)
        {
            snd::internal::driver::SoundThreadLock lock;
            velMin = sead::MathCalcCommon<s32>::clamp2(prev->getVelocityMin() + 1, velMin, velRegion.getVelocityMax());

            if (velMin != velRegion.getVelocityMin())
            {
                velRegion.setVelocityMin(static_cast<u8>(velMin));
                prev->setVelocityMax(static_cast<u8>(velMin - 1));
                SetUnsavedChanges(true);
            }
        }
        ImGui::EndDisabled();

        s32 velMax = velRegion.getVelocityMax();
        ImGui::SameLine(0.0f, spacing);
        ImGui::BeginDisabled(!next);
        ImGui::SetNextItemWidth(fieldWidth);
        if (DragField("##VelMax", ImGuiDataType_S32, &velMax, cRegionRangeDragSpeed, &cRangeMin, &cRangeMax, "%d", ImGuiSliderFlags_AlwaysClamp) && next)
        {
            snd::internal::driver::SoundThreadLock lock;
            velMax = sead::MathCalcCommon<s32>::clamp2(velRegion.getVelocityMin(), velMax, next->getVelocityMax() - 1);

            if (velMax != velRegion.getVelocityMax())
            {
                velRegion.setVelocityMax(static_cast<u8>(velMax));
                next->setVelocityMin(static_cast<u8>(velMax + 1));
                SetUnsavedChanges(true);
            }
        }
        ImGui::EndDisabled();
    }
}

static void DrawEmptyRegionField(const char* id)
{
    static const s32 cZero = 0;

    s32 value = 0;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * cRegionMappingFieldEm);
    SetNextKnobText(messages::bank::cNoValue);
    DragField(id, ImGuiDataType_S32, &value, 0.0f, &cZero, &cZero, "%d", ImGuiSliderFlags_AlwaysClamp);
}

static void DrawRegionMapping(BankFile::Instrument* instrument)
{
    BankFile::KeyRegion* keyRegion = nullptr;
    BankFile::VelocityRegion* velRegion = FindSelectedRegion(instrument, &keyRegion);

    if (!velRegion)
    {
        const f32 spacing = ImGui::GetStyle().ItemInnerSpacing.x;

        ImGui::BeginDisabled();

        RegionLabel(cRegionKeyRangeLabel);
        DrawEmptyRegionField("##KeyMin");
        ImGui::SameLine(0.0f, spacing);
        DrawEmptyRegionField("##KeyMax");

        NextRegionGroup(cRegionVelocityRangeLabel, 2);
        DrawEmptyRegionField("##VelMin");
        ImGui::SameLine(0.0f, spacing);
        DrawEmptyRegionField("##VelMax");

        NextRegionGroup(cRegionRootKeyLabel, 1);
        DrawEmptyRegionField("##RootKey");

        NextRegionGroup(cRegionKeyGroupLabel, 1);
        DrawEmptyRegionField("##KeyGroup");

        ImGui::EndDisabled();

        return;
    }

    DrawRegionRanges(*instrument, *keyRegion, *velRegion);
    velRegion->drawMappingUI();
}

void DrawRegionPanels(BankFile::Instrument* instrument)
{
    BankFile::KeyRegion* keyRegion = nullptr;
    BankFile::VelocityRegion* velRegion = FindSelectedRegion(instrument, &keyRegion);

    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 em = ImGui::GetFontSize();
    const f32 avail = ImGui::GetContentRegionAvail().x;
    const ImVec2 padding(ImFloor(em * cRegionPanelPadXEm), ImFloor(em * cRegionPanelPadYEm));
    const f32 panelHeight = ImGui::GetFrameHeight() + style.ItemSpacing.y + CalcRegionStripHeight() + padding.y * 2.0f;

    const f32 interpolationWidth = std::max(ImGui::CalcTextSize(messages::bank::cInterpolationPolyphase).x, ImGui::CalcTextSize(messages::bank::cInterpolationLinear).x) + style.FramePadding.x * 2.0f + ImGui::GetFrameHeight();
    const f32 sampleWidth = ImGui::CalcTextSize(messages::bank::cInterpolation).x + style.ItemSpacing.x + interpolationWidth;
    const f32 pitchWidth = CalcKnobRowWidth(cRegionPitchKnobs, IM_ARRAYSIZE(cRegionPitchKnobs));
    const f32 outputWidth = CalcKnobRowWidth(cRegionOutputKnobs, IM_ARRAYSIZE(cRegionOutputKnobs));
    const f32 envelopeWidth = CalcAdshrEditorWidth(false);
    const f32 envelopeMinWidth = envelopeWidth - em * (cRegionCurveEm - cRegionCurveMinEm);

    s32 lineNum = 3;
    if (avail >= sampleWidth + pitchWidth + outputWidth + envelopeWidth + padding.x * 8.0f)
        lineNum = 1;
    else if (avail >= pitchWidth + outputWidth + envelopeMinWidth + padding.x * 6.0f)
        lineNum = 2;

    if (!velRegion)
    {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 size(avail, panelHeight * lineNum + style.ItemSpacing.y * (lineNum - 1));
        const ImVec2 textSize = ImGui::CalcTextSize(messages::bank::cNoRegionSelected);

        ImGui::Dummy(size);
        ImGui::GetWindowDrawList()->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::GetColorU32(ImGuiCol_TableBorderStrong));
        ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x + (size.x - textSize.x) * 0.5f, pos.y + (size.y - textSize.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), messages::bank::cNoRegionSelected);
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, padding);

    if (lineNum == 1)
    {
        if (ImGui::BeginTable("RegionPanels", 4, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV))
        {
            ImGui::TableSetupColumn("Sample", ImGuiTableColumnFlags_WidthStretch, sampleWidth);
            ImGui::TableSetupColumn("Pitch", ImGuiTableColumnFlags_WidthFixed, pitchWidth);
            ImGui::TableSetupColumn("Output", ImGuiTableColumnFlags_WidthFixed, outputWidth);
            ImGui::TableSetupColumn("Envelope", ImGuiTableColumnFlags_WidthFixed, envelopeWidth);

            ImGui::TableNextColumn();
            velRegion->drawSampleUI();

            ImGui::TableNextColumn();
            velRegion->drawPitchUI();

            ImGui::TableNextColumn();
            velRegion->drawOutputUI();

            ImGui::TableNextColumn();
            velRegion->drawEnvelopeUI();

            ImGui::EndTable();
        }

        ImGui::PopStyleVar();
        return;
    }

    SetRegionPanelFill(true);

    if (ImGui::BeginTable("RegionSample", 1, ImGuiTableFlags_BordersOuter))
    {
        ImGui::TableSetupColumn("Sample", ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextColumn();
        velRegion->drawSampleUI();

        ImGui::EndTable();
    }

    if (lineNum == 2)
    {
        if (ImGui::BeginTable("RegionKnobs", 3, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV))
        {
            ImGui::TableSetupColumn("Pitch", ImGuiTableColumnFlags_WidthFixed, pitchWidth);
            ImGui::TableSetupColumn("Output", ImGuiTableColumnFlags_WidthFixed, outputWidth);
            ImGui::TableSetupColumn("Envelope", ImGuiTableColumnFlags_WidthStretch);

            ImGui::TableNextColumn();
            velRegion->drawPitchUI();

            ImGui::TableNextColumn();
            velRegion->drawOutputUI();

            ImGui::TableNextColumn();
            velRegion->drawEnvelopeUI();

            ImGui::EndTable();
        }
    }
    else
    {
        if (ImGui::BeginTable("RegionPitchOutput", 2, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV))
        {
            ImGui::TableSetupColumn("Pitch", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Output", ImGuiTableColumnFlags_WidthStretch);

            ImGui::TableNextColumn();
            velRegion->drawPitchUI();

            ImGui::TableNextColumn();
            velRegion->drawOutputUI();

            ImGui::EndTable();
        }

        if (ImGui::BeginTable("RegionEnvelope", 1, ImGuiTableFlags_BordersOuter))
        {
            ImGui::TableSetupColumn("Envelope", ImGuiTableColumnFlags_WidthStretch);

            ImGui::TableNextColumn();
            velRegion->drawEnvelopeUI();

            ImGui::EndTable();
        }
    }

    SetRegionPanelFill(false);

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
        sAddName = "Instrument";
        sAddProgramNo = sCurrentEditBank ? FindNextAvailableProgramNo(sCurrentEditBank->getInstrumentList(), nullptr) : 0;
    }

    if (sCurrentEditBank)
    {
        ImGui::SetNextItemWidth(240.0f);
        ImGui::InputText(messages::bank::cInstrumentNameLabel,sAddName.getBuffer(), sAddName.getBufferSize());

        s16 progStep = 1;
        s16 progStepFast = 10;
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputScalar(messages::bank::cProgramNumber,ImGuiDataType_S16, &sAddProgramNo, &progStep, &progStepFast);

        if (DrawNextProgramButton("###NextAvailAdd"))
            sAddProgramNo = FindNextAvailableProgramNo(sCurrentEditBank->getInstrumentList(), nullptr);
    }

    auto doCreate = []() -> Item *
    {
        BankFile::Instrument *instr = new BankFile::Instrument();
        instr->setEnableName(true);
        instr->getName() = sAddName.isEmpty() ? "Instrument" : sAddName.cstr();
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

    std::unordered_set<s16> used;
    for (const Item* item : bankList)
        used.insert(static_cast<const BankFile::Instrument*>(item)->getProgramNo());

    s16 nextProg = 0;
    while (used.count(nextProg))
        nextProg++;

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

    dup->setProgramNo(nextProg);
    bankList.pushBack(dup);
    sBfsar.updateList(bankList);
    SetUnsavedChanges(true);
}

static const WaveFile *GetInstrumentFirstWave_(const BankFile::Instrument *instr)
{
    if (!instr || instr->getKeyRegionList().size() == 0)
        return nullptr;

    const auto *kr = static_cast<const BankFile::KeyRegion *>(instr->getKeyRegionList().front()->val());

    if (kr->getVelocityRegionList().size() == 0)
        return nullptr;

    const auto *vr = static_cast<const BankFile::VelocityRegion *>(kr->getVelocityRegionList().front()->val());

    if (!vr->getWaveFileRef().isAttached())
        return nullptr;

    return static_cast<const WaveFile *>(vr->getWaveFileRef().getItem());
}

static bool CanAutoNameInstrument(const Item* item)
{
    const WaveFile* wave = GetInstrumentFirstWave_(static_cast<const BankFile::Instrument*>(item));

    return wave && !wave->getName().isEmpty();
}

static void AutoNameInstruments(const std::vector<Item*>& instruments)
{
    for (Item* item : instruments)
    {
        if (!CanAutoNameInstrument(item))
            continue;

        BankFile::Instrument* instrument = static_cast<BankFile::Instrument*>(item);
        const WaveFile* wave = GetInstrumentFirstWave_(instrument);

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

    if (item && ContextMenuItem(ICON_LC_COPY, messages::menu::cDuplicate))
        DuplicateInstrument(sCurrentEditBank, static_cast<BankFile::Instrument*>(item));

    // Edit
    ContextMenuSeparator();

    if (item)
    {
        const std::vector<Item*> targets = IsItemMultiSelected(item) ? sMultiSelectedItems : std::vector<Item*>{item};
        const bool canAutoName = std::any_of(targets.begin(), targets.end(), CanAutoNameInstrument);

        if (ContextMenuItem(ICON_LC_REPLACE, messages::import::cReplaceMenuLabel))
            RequestReplaceInstrument(item, sCurrentEditBank);

        if (ContextMenuItem(ICON_LC_WAND_SPARKLES, messages::menu::cAutoName, canAutoName, messages::menu::cAutoNameNoWaveFile))
            AutoNameInstruments(targets);

        if (canAutoName)
            ImGui::SetItemTooltip("%s", messages::menu::cAutoNameDetail);
    }

    if (ContextMenuItem(ICON_LC_ARROW_DOWN_0_1, messages::menu::cSortByNumber))
        SortInstrumentsByNumber(sCurrentEditBank);

    // Files
    ContextMenuSeparator();

    if (ContextMenuItem(ICON_LC_FILE_INPUT, messages::menu::cImport))
        RequestImportInstrument(sCurrentEditBank);

    if (item && ContextMenuItem(ICON_LC_FILE_OUTPUT, messages::menu::cExport))
        RequestExportInstrument(item);
}

void BankFile::drawFileUI()
{
    sCurrentEditBank = this;

    static f32 sKeyboardHeight = 200.0f; // initial guess
    static f32 sRegionHeight = 128.0f;
    f32 totalHeight = ImGui::GetContentRegionAvail().y;

    sRegionHeight = sead::MathCalcCommon<f32>::clamp2(48.0f, sRegionHeight, std::max(48.0f, totalHeight - 220.0f));

    const f32 kSplitterH = 6.0f;
    const f32 splitterFootprint = kSplitterH + ImGui::GetStyle().ItemSpacing.y;

    const f32 topHeight = std::max(ImGui::GetFrameHeight(), totalHeight - sKeyboardHeight - splitterFootprint);

    if (ImGui::BeginChild("Instruments", ImVec2(0.0f, topHeight)))
        DrawAllItemsUI("Instrument", messages::item::cTypeInstrument,mInstrumentList, &CreateInstrumentFunc, nullptr, &InstrumentContextMenuFunc);

    ImGui::EndChild();

    ImGui::InvisibleButton("##RegionSplitter", ImVec2(ImGui::GetContentRegionAvail().x, kSplitterH));
    const bool splitterActive = ImGui::IsItemActive();
    if (splitterActive || ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    if (splitterActive)
        sRegionHeight -= ImGui::GetIO().MouseDelta.y;
    {
        const ImVec2 gmin = ImGui::GetItemRectMin();
        const ImVec2 gmax = ImGui::GetItemRectMax();
        const f32 cy = (gmin.y + gmax.y) * 0.5f;
        const ImU32 gcol = (splitterActive || ImGui::IsItemHovered()) ? GetPianoRollPalette().splitterHover : IM_COL32(120, 130, 145, 130);
        ImGui::GetWindowDrawList()->AddLine(ImVec2(gmin.x + 6.0f, cy), ImVec2(gmax.x - 6.0f, cy), gcol, 2.0f);
    }

    f32 startY = ImGui::GetCursorScreenPos().y;
    if (ImGui::BeginChild("Keyboard", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border, ImGuiWindowFlags_NoScrollWithMouse))
    {
        Instrument* instrument = (sSelectedItem && sSelectedItem->getItemType() == Item::ItemType::BankFileInstrument)
            ? static_cast<Instrument *>(sSelectedItem)
            : nullptr;

        DrawRegionMapping(instrument);

        ImGui::SameLine();
        {
            u32 midiCount = MidiConnectedCount();
            bool connected = midiCount > 0;

            sead::FixedSafeString<160> statusStr;
            if (midiCount == 1)
                statusStr.format(messages::keyboard::cMidiOn);
            else if (midiCount > 1)
                statusStr.format(messages::keyboard::cMidiDevicesFormat, midiCount);
            else
                statusStr.format(messages::keyboard::cMidiOff);

            f32 spacing = ImGui::GetStyle().ItemSpacing.x;
            f32 statusW = ImGui::CalcTextSize(statusStr.cstr()).x;
            f32 btnW = ImGui::CalcTextSize(messages::keyboard::cMidiSettings).x + ImGui::GetStyle().FramePadding.x * 2;
            f32 helpW = ImGui::CalcTextSize(ICON_LC_KEYBOARD).x + ImGui::GetStyle().FramePadding.x * 2;

            float rightX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
            f32 midiX = rightX - statusW - btnW - helpW - spacing * 3;

            if (ImGui::GetCursorPosX() > midiX)
                ImGui::NewLine();

            ImGui::SetCursorPosX(midiX);

            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(connected ? ImVec4(0.45f, 0.85f, 0.5f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", statusStr.cstr());

            ImGui::SameLine(0.0f, spacing);
            if (ImGui::SmallButton(messages::keyboard::cMidiSettings))
                OpenPreferencesWindow(PreferencesPage::Midi);

            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_LC_KEYBOARD "###KeyboardHelp"))
                ImGui::OpenPopup("Keyboard Help");
            ImGui::SetItemTooltip("%s", messages::bank::cComputerKeysTooltip);
            if (ImGui::BeginPopup("Keyboard Help"))
            {
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
        }

        ImGui::Separator();

        f32 width = ImGui::GetContentRegionAvail().x;

        DrawKeyboardWithRegions(
            width,
            ImFloor(ImGui::GetFontSize() * cPianoKeysHeightEm) + GetPianoRulerHeight(), // keyboard height
            sRegionHeight, // region height
            0,
            127,
            instrument);

        f32 endY = ImGui::GetCursorScreenPos().y;
        sKeyboardHeight = endY - startY + ImGui::GetScrollY() + ImGui::GetStyle().WindowPadding.y;
    }
    ImGui::EndChild();
}
