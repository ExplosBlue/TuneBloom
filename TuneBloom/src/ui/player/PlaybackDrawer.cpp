#include <ui/player/PlaybackDrawer.h>

#include <bfsar/seq/SeqParseUtil.h>

#include <ui/Messages.h>
#include <ui/app/Shortcuts.h>
#include <ui/stream/StreamTracks.h>
#include <ui/app/UI.h>

#include <cmath>

constexpr f32 cDrawerKnobSpeed      = 0.01f;
constexpr f32 cDrawerBiquadSpeed    = 0.005f;
constexpr f32 cDrawerPercentSpeed   = 1.0f;
constexpr f32 cDrawerVolumeMinDb    = -48.0f;
constexpr f32 cDrawerVolumeMaxDb    = 12.0f;
constexpr f32 cDrawerVolumeMax      = 4.0f;
constexpr f32 cDrawerVolumeDbSpeed  = 0.2f;
constexpr f32 cDrawerVolumeShortDb  = 9.95f;
constexpr f32 cDrawerPanMax         = 2.0f;
constexpr f32 cDrawerTempoMax       = 8.0f;
constexpr f32 cDrawerDefaultRatio   = 1.0f;
constexpr f32 cDrawerListGapEm      = 0.75f;
constexpr f32 cDrawerTitleLineEm    = 1.0f;
constexpr f32 cTrackVolumeKnobEm    = 2.0f;
constexpr f32 cTrackVolumeGapEm     = 0.3f;
constexpr f32 cTrackVolumeMaxDb     = 6.0f;
constexpr f32 cTrackVolumeMax       = 2.0f;
constexpr f32 cTrackVolumeDbSpeed   = 0.25f;
constexpr f32 cTrackSwitchHeightEm  = 1.1f;
constexpr f32 cTrackWaitBarHeight   = 3.0f;
constexpr f32 cOverrideDotRadius    = 2.5f;
constexpr f32 cOverrideDotInset     = 5.0f;
constexpr f32 cSeqVarFramePaddingY  = 1.0f;
constexpr f32 cSeqVarOverrideAlpha  = 0.45f;
constexpr f32 cSeqVarDotRadiusEm    = 0.075f;
constexpr s32 cSeqVarNum            = 16;
constexpr s32 cSeqVarVisibleRows    = 5;

static const char* cDrawerKnobs[] = { messages::param::cVolume, messages::param::cCoarse, messages::param::cFine, messages::param::cPan, messages::param::cLpf, messages::param::cBiquad, messages::playback::cKnobTempo };
static const char* cDrawerBiquadLabel = messages::sound::cBiquadTypeLabel;
static const char* cDrawerBiquadTypes[] = { messages::playback::cBiquadInherit, messages::sound::cBiquadOff, messages::sound::cBiquadLpf, messages::sound::cBiquadHpf, messages::sound::cBiquadBpf512, messages::sound::cBiquadBpf1024, messages::sound::cBiquadBpf2048 };

static bool sShowPlaybackDrawer = false;
static PlaybackDrawerView sPlaybackDrawerView = PlaybackDrawerView::Parameters;

bool IsPlaybackDrawerOpen()
{
    return sShowPlaybackDrawer;
}

void SetPlaybackDrawerOpen(bool open)
{
    sShowPlaybackDrawer = open;
}

PlaybackDrawerView GetPlaybackDrawerView()
{
    return sPlaybackDrawerView;
}

void SetPlaybackDrawerView(PlaybackDrawerView view)
{
    sPlaybackDrawerView = view;
}

PlaybackDrawerLayout CalcPlaybackDrawerLayout()
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 em = ImGui::GetFontSize();

    PlaybackDrawerLayout layout = {};
    layout.padding = ImVec2(ImFloor(em * cSoundPanelPadXEm), ImFloor(em * cSoundPanelPadYEm));
    layout.knobSize = ImVec2(CalcKnobWidth(cDrawerKnobs, IM_ARRAYSIZE(cDrawerKnobs)), CalcRegionStripHeight());
    layout.knobGap = CalcKnobGap();

    for (const char* type : cDrawerBiquadTypes)
        layout.listWidth = sead::Mathf::max(layout.listWidth, ImGui::CalcTextSize(type).x);

    layout.listWidth += ImGui::GetFrameHeight() + style.FramePadding.x * 2.0f;
    layout.trackKnobSize = ImFloor(em * cTrackVolumeKnobEm);
    layout.trackPitch = layout.trackKnobSize + ImFloor(em * cTrackVolumeGapEm);

    layout.panelWidths[PlaybackPanel_Sound] = layout.knobSize.x * 4.0f + layout.knobGap * 3.0f;
    layout.panelWidths[PlaybackPanel_Filter] = layout.knobSize.x * 2.0f + layout.knobGap + ImFloor(em * cDrawerListGapEm) + ImGui::CalcTextSize(cDrawerBiquadLabel).x + style.ItemSpacing.x + layout.listWidth;
    layout.panelWidths[PlaybackPanel_Sequence] = sead::Mathf::max(layout.knobSize.x, ImGui::CalcTextSize(messages::playback::cPanelSequence).x + style.ItemSpacing.x + ImFloor(em * cDrawerTitleLineEm));
    layout.panelWidths[PlaybackPanel_Tracks] = layout.trackPitch * SoundPlayer::cMaxTracks - ImFloor(em * cTrackVolumeGapEm);

    layout.parametersWidth = 1.0f;
    for (f32 width : layout.panelWidths)
        layout.parametersWidth += width + layout.padding.x * 2.0f + 1.0f;

    layout.parametersHeight = ImGui::GetFrameHeight() + style.ItemSpacing.y + layout.knobSize.y + layout.padding.y * 2.0f + 2.0f;

    const f32 seqVarRowHeight = em + cSeqVarFramePaddingY * 2.0f + style.CellPadding.y * 2.0f;

    layout.seqVarLabelWidth = ImGui::CalcTextSize(messages::playback::cSeqVarTrackWidest).x;
    layout.seqVarValueWidth = ImGui::CalcTextSize("-32768").x + style.FramePadding.x * 2.0f;
    layout.seqVarWidth = layout.seqVarLabelWidth + style.CellPadding.x * 2.0f + (layout.seqVarValueWidth + style.CellPadding.x * 2.0f + 1.0f) * cSeqVarNum + style.ScrollbarSize + 2.0f;
    layout.seqVarTableHeight = em + style.CellPadding.y * 2.0f + seqVarRowHeight * cSeqVarVisibleRows + 2.0f;
    layout.seqVarHeight = layout.seqVarTableHeight + style.ItemSpacing.y + em;

    return layout;
}

f32 CalcPlaybackDrawerHeight(const PlaybackDrawerLayout& layout, f32 availWidth)
{
    if (sPlaybackDrawerView == PlaybackDrawerView::Variables)
        return layout.seqVarWidth > availWidth ? layout.seqVarHeight + ImGui::GetStyle().ScrollbarSize : layout.seqVarHeight;

    if (layout.parametersWidth > availWidth)
        return layout.parametersHeight + ImGui::GetStyle().ScrollbarSize;

    return layout.parametersHeight;
}

static bool IsPlaybackDrawerViewOpen(PlaybackDrawerView view)
{
    return sShowPlaybackDrawer && sPlaybackDrawerView == view;
}

static void TogglePlaybackDrawer(PlaybackDrawerView view)
{
    if (IsPlaybackDrawerViewOpen(view))
    {
        sShowPlaybackDrawer = false;
    }
    else
    {
        sShowPlaybackDrawer = true;
        sPlaybackDrawerView = view;
    }

    ImGui::MarkIniSettingsDirty();
}

static bool HasPlaybackOverride()
{
    return sSoundPlayer.hasParameterOverride() || sSoundPlayer.hasTrackOverride() || sSoundPlayer.hasSeqVarOverride();
}

static void BeginPlaybackPanel(const char* title, f32 width, f32 contentWidth)
{
    ImGui::TableNextColumn();
    RegionHeader(title);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImFloor((width - contentWidth) * 0.5f));
}

static sead::FixedSafeString<16> FormatVolumeDb(f32 ratio)
{
    sead::FixedSafeString<16> out;

    if (ratio <= 0.0f)
        out.copy(messages::playback::cVolumeSilent);
    else
        out.format(messages::playback::cVolumeDbFormat, roundf(20.0f * log10f(ratio) * 10.0f) / 10.0f + 0.0f);

    return out;
}

static bool DrawOverrideTooltip(bool hasOverride, const char* text)
{
    if (hasOverride && !ImGui::IsItemActive())
        KnobTooltip("%s\n%s", text, messages::playback::cRightClickReset);
    else
        KnobTooltip("%s", text);

    return hasOverride && ImGui::IsItemClicked(ImGuiMouseButton_Right);
}

static bool DrawVolumeDbKnob(const char* label, f32* volume, f32 maxDb, f32 maxVolume, bool tenths, f32 speed, const ImVec2& size)
{
    const f32 scale = tenths ? 10.0f : 1.0f;

    f32 db = cDrawerVolumeMinDb;
    if (*volume > 0.0f)
        db = sead::Mathf::clamp2(cDrawerVolumeMinDb, roundf(20.0f * log10f(*volume) * scale) / scale + 0.0f, maxDb);

    const char* format = db <= cDrawerVolumeMinDb ? messages::playback::cKnobVolumeSilent :!tenths || fabsf(db) >= cDrawerVolumeShortDb ? "%.0f" : "%.1f";

    if (!KnobF32(label, &db, cDrawerVolumeMinDb, maxDb, 0.0f, format, speed, size))
        return false;

    if (db <= cDrawerVolumeMinDb)
        *volume = 0.0f;
    else
        *volume = sead::Mathf::min(powf(10.0f, db / 20.0f), maxVolume);

    return true;
}

static void DrawVolumeKnob(const PlaybackDrawerLayout& layout)
{
    f32 volume = sSoundPlayer.getVolume();

    if (DrawVolumeDbKnob(messages::param::cVolume,&volume, cDrawerVolumeMaxDb, cDrawerVolumeMax, true, cDrawerVolumeDbSpeed, layout.knobSize))
        sSoundPlayer.setVolume(volume);

    if (DrawOverrideTooltip(volume != cDrawerDefaultRatio, sead::FormatFixedSafeString<64>(messages::playback::cVolumeFormat, FormatVolumeDb(volume).cstr()).cstr()))
        sSoundPlayer.setVolume(cDrawerDefaultRatio);
}

static void DrawSoundPanel(const PlaybackDrawerLayout& layout)
{
    BeginPlaybackPanel(messages::playback::cPanelSound,layout.panelWidths[PlaybackPanel_Sound], layout.panelWidths[PlaybackPanel_Sound]);

    DrawVolumeKnob(layout);

    ImGui::SameLine(0.0f, layout.knobGap);
    ImGui::BeginGroup();

    f32 pitch = sSoundPlayer.getPitch();
    if (DrawPitchKnobs(&pitch, layout.knobSize))
        sSoundPlayer.setPitch(pitch);

    ImGui::EndGroup();

    if (pitch != cDrawerDefaultRatio && ImGui::IsItemClicked(ImGuiMouseButton_Right))
        sSoundPlayer.setPitch(cDrawerDefaultRatio);

    ImGui::SameLine(0.0f, layout.knobGap);

    f32 pan = roundf(sSoundPlayer.getPan() * 100.0f) + 0.0f;
    if (KnobF32(messages::param::cPan,&pan, -cDrawerPanMax * 100.0f, cDrawerPanMax * 100.0f, 0.0f, "%.0f", cDrawerPercentSpeed, layout.knobSize))
        sSoundPlayer.setPan(pan / 100.0f);

    sead::FixedSafeString<64> panText(messages::playback::cPanOffsetNone);
    if (pan != 0.0f)
        panText.format(messages::playback::cPanOffsetFormat, FormatPanLabel(pan / 100.0f).cstr());

    if (DrawOverrideTooltip(pan != 0.0f, panText.cstr()))
        sSoundPlayer.setPan(0.0f);
}

static void DrawFilterPanel(const PlaybackDrawerLayout& layout)
{
    BeginPlaybackPanel(messages::panel::cFilter,layout.panelWidths[PlaybackPanel_Filter], layout.panelWidths[PlaybackPanel_Filter]);

    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 origin = ImGui::GetCursorPos();

    f32 lpfFreq = roundf(sSoundPlayer.getLpfFreq() * 100.0f) + 0.0f;
    if (KnobF32(messages::param::cLpf,&lpfFreq, -100.0f, 0.0f, 0.0f, "%.0f", cDrawerPercentSpeed, layout.knobSize))
        sSoundPlayer.setLpfFreq(lpfFreq / 100.0f);

    if (DrawOverrideTooltip(lpfFreq != 0.0f, sead::FormatFixedSafeString<64>(messages::playback::cParameterFormat, messages::param::cLpf,lpfFreq / 100.0f).cstr()))
        sSoundPlayer.setLpfFreq(0.0f);

    ImGui::SameLine(0.0f, layout.knobGap);

    f32 biquadValue = sSoundPlayer.getBiquadValue();
    if (KnobF32(messages::param::cBiquad,&biquadValue, 0.0f, 1.0f, 0.0f, "%.2f", cDrawerBiquadSpeed, layout.knobSize))
        sSoundPlayer.setBiquadFilter(sSoundPlayer.getBiquadType(), biquadValue);

    if (DrawOverrideTooltip(biquadValue != 0.0f, sead::FormatFixedSafeString<64>(messages::playback::cParameterFormat, messages::param::cBiquad,biquadValue).cstr()))
        sSoundPlayer.setBiquadFilter(sSoundPlayer.getBiquadType(), 0.0f);

    // Biquad type
    const f32 knobDiameter = layout.knobSize.y - ImGui::GetFontSize() - cSoundSlotLabelGap;

    ImGui::SetCursorPos(ImVec2(origin.x + layout.knobSize.x * 2.0f + layout.knobGap + ImFloor(ImGui::GetFontSize() * cDrawerListGapEm), origin.y + ImFloor((knobDiameter - ImGui::GetFrameHeight()) * 0.5f)));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(cDrawerBiquadLabel);
    ImGui::SameLine(0.0f, style.ItemSpacing.x);

    s32 biquadType = sSoundPlayer.getBiquadType() + 1;
    ImGui::SetNextItemWidth(layout.listWidth);
    if (ComboScroll("##biquadType", &biquadType, cDrawerBiquadTypes, IM_ARRAYSIZE(cDrawerBiquadTypes)))
        sSoundPlayer.setBiquadFilter(biquadType - 1, sSoundPlayer.getBiquadValue());
}

static void DrawSequencePanel(const PlaybackDrawerLayout& layout)
{
    BeginPlaybackPanel(messages::playback::cPanelSequence,layout.panelWidths[PlaybackPanel_Sequence], layout.knobSize.x);

    ImGui::BeginDisabled(sSoundPlayer.isActive() && !sSoundPlayer.isCurrentPlayerSequence());

    f32 tempoRatio = sead::Mathf::clamp2(1.0f / cDrawerTempoMax, sSoundPlayer.getSeqTempoRatio(), cDrawerTempoMax);
    if (KnobRatio(messages::playback::cKnobTempo, &tempoRatio, 1.0f / cDrawerTempoMax, cDrawerTempoMax, "%.2f", cDrawerKnobSpeed, layout.knobSize))
        sSoundPlayer.setSeqTempoRatio(tempoRatio);

    if (DrawOverrideTooltip(tempoRatio != cDrawerDefaultRatio, sead::FormatFixedSafeString<64>(messages::playback::cTempoFormat, tempoRatio).cstr()))
        sSoundPlayer.setSeqTempoRatio(cDrawerDefaultRatio);

    ImGui::EndDisabled();
}

static void DrawTrackSwitch(u32 trackNo, const ImVec2& size, bool exists)
{
    const u32 bit = 1 << trackNo;
    const u32 muteTracks = sSoundPlayer.getMuteTracks();
    const u32 soloTracks = sSoundPlayer.getSoloTracks();
    const bool isMute = (muteTracks & bit) != 0;
    const bool isSolo = (soloTracks & bit) != 0;

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(exists ? ImGuiCol_Text : ImGuiCol_TextDisabled));
    const bool pressed = DrawTrackToggle(sead::FormatFixedSafeString<16>("%u###switch", trackNo).cstr(), isMute || isSolo, isSolo ? cTrackSoloColor : cTrackMuteColor, size);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    ImGui::SetItemTooltip("%s", messages::playback::cTrackSwitchTip);

    if (!pressed)
        return;

    if (shortcuts::Held(shortcuts::Modifier::ToggleSelection))
        sSoundPlayer.setTrackSwitches(muteTracks, soloTracks == bit ? 0 : bit);
    else if (isSolo)
        sSoundPlayer.setTrackSwitches(muteTracks, soloTracks & ~bit);
    else
        sSoundPlayer.setTrackSwitches(muteTracks ^ bit, soloTracks);
}

static void DrawTracksPanel(const PlaybackDrawerLayout& layout)
{
    BeginPlaybackPanel(messages::playback::cPanelTrackVolume,layout.panelWidths[PlaybackPanel_Tracks], layout.panelWidths[PlaybackPanel_Tracks]);

    const bool isStream = sSoundPlayer.getStreamPlayer().isActive();
    const bool isSequence = sSoundPlayer.getSequencePlayer().isActive();
    const bool isWave = sSoundPlayer.getWavePlayer().isActive();

    const f32 em = ImGui::GetFontSize();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 knobSize(layout.trackKnobSize, layout.trackKnobSize);
    const ImVec2 switchSize(layout.trackKnobSize, ImFloor(em * cTrackSwitchHeightEm));
    const f32 switchY = layout.knobSize.y - switchSize.y;
    const f32 knobY = ImFloor(sead::Mathf::max(0.0f, switchY - cSoundSlotLabelGap - cTrackWaitBarHeight - layout.trackKnobSize) * 0.5f);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    for (u32 i = 0; i < SoundPlayer::cMaxTracks; i++)
    {
        bool exists = !isWave;
        s32 wait = 0;
        s32 waitAmount = 0;

        if (isStream)
        {
            exists = i < sSoundPlayer.getStreamPlayer().getTrackCount();
        }
        else if (isSequence)
        {
            snd::internal::driver::SoundThreadLock lock;

            const SequenceTrack* track = sSoundPlayer.getSequencePlayer().getPlayerTrack(i);
            exists = track != nullptr;

            if (track)
            {
                wait = track->getParserTrackParam().wait;
                waitAmount = track->getParserTrackParam().waitAmount;
            }
        }

        const ImVec2 pos(origin.x + layout.trackPitch * i, origin.y);

        ImGui::PushID(static_cast<int>(i));
        ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + knobY));

        const bool dim = !exists || !sSoundPlayer.isTrackAudible(i);
        if (dim)
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * (1.0f - cTrackSilentAlpha));

        f32 volume = sSoundPlayer.getTrackVolume(i);
        if (DrawVolumeDbKnob("##volume", &volume, cTrackVolumeMaxDb, cTrackVolumeMax, false, cTrackVolumeDbSpeed, knobSize))
            sSoundPlayer.setTrackVolume(i, volume);

        if (dim)
            ImGui::PopStyleVar();

        sead::FixedSafeString<64> tooltip;
        if (isSequence && exists)
            tooltip.format(messages::playback::cTrackWaitFormat, i, FormatVolumeDb(volume).cstr(), wait, waitAmount);
        else
            tooltip.format(messages::playback::cTrackVolumeFormat, i, FormatVolumeDb(volume).cstr());

        if (DrawOverrideTooltip(volume != cDrawerDefaultRatio, tooltip.cstr()))
            sSoundPlayer.setTrackVolume(i, cDrawerDefaultRatio);

        if (isSequence && exists && waitAmount != 0)
        {
            const f32 fill = layout.trackKnobSize * sead::Mathf::clamp2(0.0f, static_cast<f32>(wait) / static_cast<f32>(waitAmount), 1.0f);
            const f32 top = pos.y + switchY - cSoundSlotLabelGap - cTrackWaitBarHeight;

            draw->AddRectFilled(ImVec2(pos.x, top), ImVec2(pos.x + fill, top + cTrackWaitBarHeight), ImGui::GetColorU32(gAccentColor));
        }

        ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + switchY));
        DrawTrackSwitch(i, switchSize, exists);

        ImGui::PopID();
    }

    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(layout.panelWidths[PlaybackPanel_Tracks], layout.knobSize.y));
}

static void DrawPlaybackParameters(const PlaybackDrawerLayout& layout)
{
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + sead::Mathf::max(0.0f, ImFloor((ImGui::GetContentRegionAvail().x - layout.parametersWidth) * 0.5f)));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, layout.padding);

    if (ImGui::BeginTable("##parameters", PlaybackPanel_Num, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_NoHostExtendX | ImGuiTableFlags_SizingFixedFit))
    {
        for (f32 width : layout.panelWidths)
            ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, width);

        DrawSoundPanel(layout);
        DrawFilterPanel(layout);
        DrawSequencePanel(layout);
        DrawTracksPanel(layout);

        ImGui::EndTable();
    }

    ImGui::PopStyleVar();
}

static u32 GetSeqVarNo(SoundPlayer::SeqVarScope scope, s32 varNo)
{
    switch (scope)
    {
    case SoundPlayer::SeqVarScope::Player:
        return varNo;
    case SoundPlayer::SeqVarScope::Global:
        return SequenceSoundPlayer::cPlayerVariableNum + varNo;
    default:
        return SequenceSoundPlayer::cPlayerVariableNum + SequenceSoundPlayer::cGlobalVariableNum + varNo;
    }
}

static void DrawSeqVarTooltip(SoundPlayer::SeqVarScope scope, s32 varNo, bool isUsed, bool isOverride)
{
    const char* name = GetSeqVariableName(GetSeqVarNo(scope, varNo));

    if (isOverride)
        ImGui::SetItemTooltip(messages::playback::cSeqVarOverrideFormat, name);
    else
        ImGui::SetItemTooltip(isUsed ? messages::playback::cSeqVarUsedTooltipFormat : messages::playback::cSeqVarUnusedTooltipFormat, name);
}

static void DrawSeqVarCell(SoundPlayer::SeqVarScope scope, s32 trackNo, s32 varNo, bool isUsed)
{
    ImGui::TableNextColumn();
    ImGui::PushID(varNo);

    const ImGuiStyle& style = ImGui::GetStyle();
    const ImGuiID id = ImGui::GetID("##value");
    const bool isOverride = sSoundPlayer.hasSeqVarOverride(scope, trackNo, varNo);
    const bool isQuiet = !isUsed && !isOverride && ImGui::GetHoveredID() != id && ImGui::GetActiveID() != id;

    ImVec4 frameColor = isOverride ? gAccentColor : style.Colors[ImGuiCol_FrameBg];

    if (isOverride)
        frameColor.w = cSeqVarOverrideAlpha;
    else if (!isUsed)
        frameColor.w = 0.0f;

    ImVec4 textColor = style.Colors[isUsed || isOverride ? ImGuiCol_Text : ImGuiCol_TextDisabled];

    if (isQuiet)
        textColor.w = 0.0f;

    ImGui::PushStyleColor(ImGuiCol_FrameBg, frameColor);
    ImGui::PushStyleColor(ImGuiCol_Text, textColor);

    s16 value = sSoundPlayer.getSeqVar(scope, trackNo, varNo);

    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputScalar("##value", ImGuiDataType_S16, &value))
        sSoundPlayer.setSeqVar(scope, trackNo, varNo, value);

    ImGui::PopStyleColor(2);

    if (isQuiet)
    {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 center(min.x + style.FramePadding.x + ImFloor(ImGui::CalcTextSize("0").x * 0.5f), ImFloor((min.y + ImGui::GetItemRectMax().y) * 0.5f));

        ImGui::GetWindowDrawList()->AddCircleFilled(center, ImGui::GetFontSize() * cSeqVarDotRadiusEm, ImGui::GetColorU32(ImGuiCol_TextDisabled));
    }

    if (ImGui::IsItemHovered())
        DrawSeqVarTooltip(scope, varNo, isUsed, isOverride);

    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        sSoundPlayer.resetSeqVar(scope, trackNo, varNo);

    ImGui::PopID();
}

static void DrawSeqVarRow(const char* label, SoundPlayer::SeqVarScope scope, s32 trackNo, u16 used)
{
    ImGui::PushID(static_cast<s32>(scope));
    ImGui::PushID(trackNo);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImGui::GetStyleColorVec4(used != 0 ? ImGuiCol_Text : ImGuiCol_TextDisabled), "%s", label);

    for (s32 i = 0; i < cSeqVarNum; i++)
        DrawSeqVarCell(scope, trackNo, i, (used >> i & 1) != 0);

    ImGui::PopID();
    ImGui::PopID();
}

static bool IsSeqVarTrackRowShown(s32 trackNo, const SeqVarUse* use)
{
    if (use && (use->tracks >> trackNo & 1) != 0)
        return true;

    for (s32 i = 0; i < SequenceTrack::cTrackVariableNum; i++)
    {
        if (sSoundPlayer.hasSeqVarOverride(SoundPlayer::SeqVarScope::Track, trackNo, i))
            return true;
    }

    return false;
}

static void DrawSeqVarRows(const SeqVarUse* use)
{
    DrawSeqVarRow(messages::playback::cSeqVarGlobal, SoundPlayer::SeqVarScope::Global, 0, use ? use->global : 0);
    DrawSeqVarRow(messages::playback::cSeqVarPlayer, SoundPlayer::SeqVarScope::Player, 0, use ? use->player : 0);

    for (s32 trackNo = 0; trackNo < SequenceSoundPlayer::cTrackNumPerPlayer; trackNo++)
    {
        if (!IsSeqVarTrackRowShown(trackNo, use))
            continue;

        DrawSeqVarRow(sead::FormatFixedSafeString<16>(messages::stream::cTrackNameFormat, trackNo).cstr(), SoundPlayer::SeqVarScope::Track, trackNo, use ? use->track[trackNo] : 0);
    }
}

static void DrawSeqVarStatus(const SeqVarUse* use)
{
    if (!use)
    {
        ImGui::TextDisabled("%s", messages::playback::cSeqVarStatusNone);
        return;
    }

    const Item* item = ResolveDisplayItem();
    const char* name = item ? item->getName().cstr() : messages::playback::cDisplaySequence;
    const u32 count = use->count();

    if (count == 0)
        ImGui::TextDisabled(messages::playback::cSeqVarStatusUnusedFormat, name);
    else if (count == 1)
        ImGui::TextDisabled(messages::playback::cSeqVarStatusOneFormat, name);
    else
        ImGui::TextDisabled(messages::playback::cSeqVarStatusManyFormat, name, count);
}

static void DrawPlaybackVariables(const PlaybackDrawerLayout& layout)
{
    static_assert(SequenceSoundPlayer::cGlobalVariableNum == cSeqVarNum && SequenceSoundPlayer::cPlayerVariableNum == cSeqVarNum && SequenceTrack::cTrackVariableNum == cSeqVarNum);

    const SeqTimeline* timeline = ResolveSeqTimeline();
    const SeqVarUse use = timeline ? sSoundPlayer.getSeqVarUse(*timeline) : SeqVarUse();

    const f32 avail = ImGui::GetContentRegionAvail().x;
    const f32 width = sead::Mathf::min(layout.seqVarWidth, avail);
    const f32 left = ImGui::GetCursorPosX() + ImFloor((avail - width) * 0.5f);
    const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | (width < layout.seqVarWidth ? ImGuiTableFlags_ScrollX : ImGuiTableFlags_None);

    ImGui::SetCursorPosX(left);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, cSeqVarFramePaddingY));

    if (ImGui::BeginTable("##vars", 1 + cSeqVarNum, flags, ImVec2(width, width < layout.seqVarWidth ? layout.seqVarTableHeight + ImGui::GetStyle().ScrollbarSize : layout.seqVarTableHeight)))
    {
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, layout.seqVarLabelWidth);

        for (s32 i = 0; i < cSeqVarNum; i++)
            ImGui::TableSetupColumn(sead::FormatFixedSafeString<8>("%d", i).cstr(), ImGuiTableColumnFlags_WidthFixed, layout.seqVarValueWidth);

        ImGui::TableHeadersRow();
        DrawSeqVarRows(timeline ? &use : nullptr);

        ImGui::EndTable();
    }

    ImGui::PopStyleVar();

    ImGui::SetCursorPosX(left);
    DrawSeqVarStatus(timeline ? &use : nullptr);
}

void DrawPlaybackDrawer(const PlaybackDrawerLayout& layout, f32 height)
{
    if (!sShowPlaybackDrawer)
        return;

    if (sPlaybackDrawerView == PlaybackDrawerView::Variables)
    {
        DrawPlaybackVariables(layout);
        return;
    }

    if (ImGui::BeginChild("##drawer", ImVec2(0.0f, height), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
        DrawPlaybackParameters(layout);

    ImGui::EndChild();
}

static void DrawPlaybackDrawerButton(const char* icon, PlaybackDrawerView view, bool hasOverride, const char* tooltip)
{
    const bool open = IsPlaybackDrawerViewOpen(view);

    ImVec4 accent = gAccentColor;
    accent.w = 1.0f;

    if (open)
        ImGui::PushStyleColor(ImGuiCol_Button, accent);

    if (ImGui::Button(icon))
        TogglePlaybackDrawer(view);

    if (open)
        ImGui::PopStyleColor();

    if (hasOverride)
    {
        const ImVec2 max = ImGui::GetItemRectMax();
        const ImVec2 min = ImGui::GetItemRectMin();

        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(max.x - cOverrideDotInset, min.y + cOverrideDotInset), cOverrideDotRadius, open ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(accent));
    }

    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone))
        return;

    if (hasOverride)
        ImGui::SetTooltip("%s\n%s", tooltip, messages::playback::cOverridesPersist);
    else
        ImGui::SetTooltip("%s", tooltip);
}

f32 CalcPlaybackOverrideButtonsWidth()
{
    return ImGui::GetFrameHeight() * 3.0f + ImGui::GetStyle().ItemSpacing.x * 2.0f;
}

void DrawPlaybackOverrideButtons()
{
    const bool hasOverride = HasPlaybackOverride();

    ImGui::PushID("##overrides");

    DrawPlaybackDrawerButton(ICON_LC_SLIDERS_HORIZONTAL, PlaybackDrawerView::Parameters, sSoundPlayer.hasParameterOverride() || sSoundPlayer.hasTrackOverride(), messages::playback::cParametersOpen);

    ImGui::SameLine();
    DrawPlaybackDrawerButton(ICON_LC_VARIABLE, PlaybackDrawerView::Variables, sSoundPlayer.hasSeqVarOverride(), messages::playback::cVariablesOpen);

    ImGui::SameLine();
    ImGui::BeginDisabled(!hasOverride);

    if (ImGui::Button(ICON_LC_LIST_RESTART))
    {
        sSoundPlayer.resetParameters();
        sSoundPlayer.resetTracks();
        sSoundPlayer.resetSeqVars();
    }

    ImGui::EndDisabled();

    SetDisabledTooltip(messages::playback::cOverridesReset);

    ImGui::PopID();
}
