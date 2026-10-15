#include <ui/stream/StreamTracks.h>

#include <ui/widgets/ContextMenu.h>
#include <ui/Messages.h>
#include <ui/app/Shortcuts.h>
#include <ui/sound/SoundPanels.h>
#include <ui/stream/StreamTrackImport.h>
#include <ui/app/UI.h>
#include <ui/settings/UiColors.h>
#include <ui/widgets/Widgets.h>

static constexpr f32 cTrackKnobEm         = 2.0f;
static constexpr f32 cTrackSlotGapEm      = 0.3f;
static constexpr f32 cTrackCellPadYEm     = 0.2f;
static constexpr f32 cTrackIndexWidthEm   = 1.0f;
static constexpr u8  cTrackPanCenter      = 64;
static constexpr f32 cTrackToggleHeightEm = 1.6f;

static constexpr const char* cTrackMuteLabel = messages::stream::cTrackMute;
static constexpr const char* cTrackSoloLabel = messages::stream::cTrackSolo;

f32 CalcTrackKnobSize()
{
    return ImFloor(ImGui::GetFontSize() * cTrackKnobEm);
}

f32 TrackSlots::calcWidth() const
{
    f32 width = ImGui::GetFontSize() * cTrackSlotGapEm * (num - 1);
    for (s32 i = 0; i < num; i++)
        width += widths[i];

    return width;
}

void TrackSlots::begin()
{
    origin = ImGui::GetCursorPos();
    offset = 0.0f;
    index = 0;
}

const char* TrackSlots::next(const ImVec2& itemSize, f32 rowHeight)
{
    SEAD_ASSERT(index < num);

    const f32 width = widths[index];

    ImGui::SetCursorPos(ImVec2(origin.x + offset + ImFloor((width - itemSize.x) * 0.5f), origin.y + ImFloor((rowHeight - itemSize.y) * 0.5f)));
    offset += width + ImGui::GetFontSize() * cTrackSlotGapEm;

    return labels[index++];
}

void TrackSlots::drawLabels() const
{
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const f32 y = pos.y + ImGui::GetStyle().FramePadding.y;
    f32 x = pos.x;

    for (s32 i = 0; i < num; i++)
    {
        const f32 labelWidth = ImGui::CalcTextSize(labels[i]).x;

        ImGui::GetWindowDrawList()->AddText(ImVec2(x + ImFloor((widths[i] - labelWidth) * 0.5f), y), ImGui::GetColorU32(ImGuiCol_Text), labels[i]);
        x += widths[i] + ImGui::GetFontSize() * cTrackSlotGapEm;
    }

    ImGui::Dummy(ImVec2(calcWidth(), ImGui::GetFrameHeight()));
}

static f32 CalcTrackSlotWidth()
{
    f32 width = CalcTrackKnobSize();

    for (const char* label : cTrackMixLabels)
        width = sead::Mathf::max(width, ImGui::CalcTextSize(label).x);

    for (const char* label : cTrackSendLabels)
        width = sead::Mathf::max(width, ImGui::CalcTextSize(label).x);

    for (const char* label : cTrackFilterLabels)
        width = sead::Mathf::max(width, ImGui::CalcTextSize(label).x);

    return width;
}

TrackSlots MakeTrackSlots(const char* const* labels, s32 labelNum)
{
    SEAD_ASSERT(labelNum <= TrackSlots::cSlotMax);

    TrackSlots slots = {};
    slots.labels = labels;
    slots.num = labelNum;

    for (s32 i = 0; i < labelNum; i++)
        slots.widths[i] = CalcTrackSlotWidth();

    return slots;
}

static ImVec2 CalcTrackCellPadding()
{
    const f32 em = ImGui::GetFontSize();

    return ImVec2(ImFloor(em * cTrackCellPadXEm), ImFloor(em * cTrackCellPadYEm));
}

TrackTableLayout CalcTrackTableLayout(const TrackContext& ctx)
{
    const f32 frameHeight = ImGui::GetFrameHeight();

    TrackTableLayout layout = {};

    layout.muteSlots = MakeTrackSlots(&cTrackMuteLabel, 1);
    layout.muteSlots.widths[0] = ImCeil(sead::Mathf::max(frameHeight, ImGui::CalcTextSize(cTrackMuteLabel).x));

    layout.soloSlots = MakeTrackSlots(&cTrackSoloLabel, 1);
    layout.soloSlots.widths[0] = ImCeil(sead::Mathf::max(frameHeight, ImGui::CalcTextSize(cTrackSoloLabel).x));

    layout.mixSlots = MakeTrackSlots(cTrackMixLabels, cTrackBypassIndex);
    layout.bypassSlots = MakeTrackSlots(cTrackMixLabels + cTrackBypassIndex, IM_ARRAYSIZE(cTrackMixLabels) - cTrackBypassIndex);
    layout.sendSlots = MakeTrackSlots(cTrackSendLabels, IM_ARRAYSIZE(cTrackSendLabels));
    layout.filterSlots = MakeTrackSlots(cTrackFilterLabels, IM_ARRAYSIZE(cTrackFilterLabels));
    layout.filterSlots.widths[1] = sead::Mathf::max(layout.filterSlots.widths[1], ctx.biquadWidth);

    layout.cellPadding = CalcTrackCellPadding();
    layout.columnNum = 6 + (ctx.enableSend ? 1 : 0) + (ctx.enableFilter ? 1 : 0);
    layout.indexWidth = ImCeil(ImGui::GetFontSize() * cTrackIndexWidthEm);
    layout.muteWidth = layout.muteSlots.calcWidth();
    layout.soloWidth = layout.soloSlots.calcWidth();
    layout.mixWidth = layout.mixSlots.calcWidth();
    layout.bypassWidth = layout.bypassSlots.calcWidth();
    layout.sendWidth = ctx.enableSend ? layout.sendSlots.calcWidth() : 0.0f;
    layout.filterWidth = ctx.enableFilter ? layout.filterSlots.calcWidth() : 0.0f;

    layout.columnExtra = layout.cellPadding.x * 2.0f + 1.0f;
    layout.leadWidth = layout.indexWidth + layout.muteWidth + layout.soloWidth + layout.columnExtra * 4.0f + 1.0f;

    layout.groupWidths[layout.groupNum++] = layout.mixWidth + layout.columnExtra;
    layout.groupWidths[layout.groupNum++] = layout.bypassWidth + layout.columnExtra;

    if (ctx.enableSend)
        layout.groupWidths[layout.groupNum++] = layout.sendWidth + layout.columnExtra;

    if (ctx.enableFilter)
        layout.groupWidths[layout.groupNum++] = layout.filterWidth + layout.columnExtra;

    layout.fixedWidth = layout.leadWidth;
    for (s32 i = 0; i < layout.groupNum; i++)
        layout.fixedWidth += layout.groupWidths[i];

    return layout;
}

bool DrawTrackHandle(const TrackContext& ctx, Sound::StreamSoundInfo::Track* track, const ImVec2& size, ImGuiSelectableFlags flags, TrackEdit* edit)
{
    using Track = Sound::StreamSoundInfo::Track;

    ImGui::PushStyleColor(ImGuiCol_Header, uicolors::cTransparent);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, uicolors::cTransparent);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, uicolors::cTransparent);

    if (ImGui::Selectable("###Row", track == GetHighlightedStreamTrack(), flags, size))
        HighlightStreamTrack(track);

    ImGui::PopStyleColor(3);

    const bool hovered = ImGui::IsItemHovered();

    if (BeginItemContextMenu("TrackMenu"))
    {
        const sead::FormatFixedSafeString<64> limit(messages::stream::cTrackLimitFormat, cStrmTrackNum);

        HighlightStreamTrack(track);

        if (ContextMenuItem(ICON_LC_LIST_PLUS, messages::menu::cInsertAfter, ctx.canAdd, limit.cstr()))
        {
            edit->add = true;
            edit->insertAfter = track;
        }

        if (ContextMenuItem(ICON_LC_IMPORT, messages::import::cTrackImportMenuLabel, ctx.canAdd, limit.cstr()))
            RequestStreamTrackImport(ctx.sound);

        ContextMenuSeparator();

        if (ContextMenuDangerItem(ICON_LC_TRASH_2, messages::menu::cDeleteTrack))
            edit->deleteTrack = track;

        EndContextMenu();
    }

    if (ImGui::BeginDragDropSource())
    {
        ImGui::SetDragDropPayload("TRACK", &track, sizeof(track));
        ImGui::Text(messages::item::cMovingItemFormat,track->getFormattedName().cstr());
        ImGui::EndDragDropSource();
    }

    if (ImGui::BeginDragDropTarget())
    {
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TRACK");
        if (payload)
        {
            edit->moveTrack = *static_cast<Track**>(payload->Data);
            edit->moveTarget = track;
        }

        ImGui::EndDragDropTarget();
    }

    return hovered;
}

void DrawTrackListMenu(const TrackContext& ctx, TrackEdit* edit)
{
    if (!BeginWindowContextMenu("TrackListMenu"))
        return;

    const sead::FormatFixedSafeString<64> limit(messages::stream::cTrackLimitFormat, cStrmTrackNum);

    if (ContextMenuItem(ICON_LC_PLUS, messages::common::cAdd, ctx.canAdd, limit.cstr()))
        edit->add = true;

    if (ContextMenuItem(ICON_LC_IMPORT, messages::import::cTrackImportMenuLabel, ctx.canAdd, limit.cstr()))
        RequestStreamTrackImport(ctx.sound);

    ContextMenuSeparator();

    if (ContextMenuCheckItem(messages::stream::cTrackMixerView, IsTrackMixerView()))
        edit->toggleView = true;

    EndContextMenu();
}

static ImVec2 CalcTrackToggleSize(f32 width)
{
    const f32 frameHeight = ImGui::GetFrameHeight();

    if (width <= frameHeight)
        return ImVec2(frameHeight, frameHeight);

    return ImVec2(width, ImFloor(ImGui::GetFontSize() * cTrackToggleHeightEm));
}

bool DrawTrackToggle(const char* label, bool on, const ImVec4& color, const ImVec2& size)
{
    if (on)
        PushBrightButtonColors(color);

    const bool pressed = ImGui::Button(label, size);

    if (on)
        PopBrightButtonColors();

    return pressed;
}

static void DrawTrackMuteButton(const TrackContext& ctx, s32 index, const ImVec2& size)
{
    Sound::StreamSoundInfo& strmInfo = ctx.sound->getStreamSoundInfo();
    const u32 bit = 1 << index;
    const u32 muteTracks = strmInfo.getMuteTracks();

    if (!DrawTrackToggle(sead::FormatFixedSafeString<32>("%s###Mute", messages::stream::cTrackMuteShort).cstr(),(muteTracks & bit) != 0, uicolors::cMarker, size))
        return;

    strmInfo.setMuteTracks(muteTracks ^ bit);
    sSoundPlayer.updateStreamTrackMute(ctx.sound);
}

static void DrawTrackSoloButton(const TrackContext& ctx, s32 index, const ImVec2& size)
{
    Sound::StreamSoundInfo& strmInfo = ctx.sound->getStreamSoundInfo();
    const u32 bit = 1 << index;
    const u32 soloTracks = strmInfo.getSoloTracks();

    const bool pressed = DrawTrackToggle(sead::FormatFixedSafeString<32>("%s###Solo", messages::stream::cTrackSoloShort).cstr(),(soloTracks & bit) != 0, uicolors::PlaybackFill(), size);

    ImGui::SetItemTooltip(messages::stream::cTrackSoloTipFormat, shortcuts::Label(shortcuts::Modifier::ToggleSelection));

    if (!pressed)
        return;

    if (shortcuts::Held(shortcuts::Modifier::ToggleSelection))
        strmInfo.setSoloTracks(soloTracks ^ bit);
    else
        strmInfo.setSoloTracks(soloTracks == bit ? 0 : bit);

    sSoundPlayer.updateStreamTrackMute(ctx.sound);
}

void DrawTrackWaveSelector(Sound::StreamSoundInfo::Track* track, f32 width, bool compact)
{
    Item* waveFile = track->getWaveFileRef().getItem();

    ImGui::SetNextItemWidth(width);
    if (ItemSelector("##WaveFile", sBfsar.getWaveFileList(), &waveFile, false, compact))
    {
        track->getWaveFileRef().attach(waveFile);
        SetUnsavedChanges(true);
    }
}

ImVec2 CalcTrackParamSize(const TrackContext& ctx, TrackParam param, f32 width)
{
    const f32 frameHeight = ImGui::GetFrameHeight();
    const f32 knobSize = CalcTrackKnobSize();

    switch (param)
    {
        case TrackParam::Mute:
        case TrackParam::Solo:
            return CalcTrackToggleSize(width);

        case TrackParam::FrontBypass:
            return ImVec2(frameHeight, frameHeight);

        case TrackParam::WaveFile:
            return ImVec2(width, frameHeight);

        case TrackParam::BiquadType:
            return ImVec2(sead::Mathf::min(width, ctx.biquadWidth), frameHeight);

        default:
            return ImVec2(knobSize, knobSize);
    }
}

void DrawTrackParam(const TrackContext& ctx, s32 index, TrackParam param, f32 width)
{
    Sound::StreamSoundInfo::Track* track = ctx.tracks[index];
    const f32 size = CalcTrackKnobSize();
    const ImVec2 knobSize(size, size);

    ImGui::PushID(static_cast<s32>(param));

    switch (param)
    {
        case TrackParam::Mute:
            DrawTrackMuteButton(ctx, index, CalcTrackToggleSize(width));
            break;

        case TrackParam::Solo:
            DrawTrackSoloButton(ctx, index, CalcTrackToggleSize(width));
            break;

        case TrackParam::WaveFile:
            DrawTrackWaveSelector(track, width, true);
            break;

        case TrackParam::Volume:
        {
            u8 volume = track->getVolume();
            SetNextKnobReset(cSoundVolumeDefault);
            if (KnobU8("##Knob", &volume, 0, 255, 0, knobSize))
            {
                track->setVolume(volume);
                SetUnsavedChanges(true);
            }

            KnobTooltip(messages::stream::cTrackVolumeFormat, static_cast<f32>(volume) / 127.0f);
            break;
        }

        case TrackParam::Pan:
        {
            u8 pan = track->getPan();
            SetNextKnobText(FormatPanText(pan, cTrackPanCenter).cstr());
            if (KnobU8("##Knob", &pan, 0, cPanMax, cTrackPanCenter, knobSize))
            {
                track->setPan(pan);
                SetUnsavedChanges(true);
            }

            KnobTooltip("%s", FormatPanLabel(pan, cPanMin, cTrackPanCenter, cPanMax).cstr());
            break;
        }

        case TrackParam::SurroundPan:
        {
            u8 span = track->getSPan();
            SetNextKnobReset(0.0);
            if (KnobU8("##Knob", &span, 0, 127, 0, knobSize))
            {
                track->setSPan(span);
                SetUnsavedChanges(true);
            }

            KnobTooltip("%s", messages::stream::cTrackSurroundPan);
            break;
        }

        case TrackParam::FrontBypass:
        {
            bool flags = track->getFlags();
            if (ImGui::Checkbox("##FrontBypass", &flags))
            {
                track->setFlags(flags);
                SetUnsavedChanges(true);
            }

            ImGui::SetItemTooltip("%s", messages::common::cFrontBypass);
            break;
        }

        case TrackParam::MainSend:
        {
            u8 mainSend = track->getMainSend();
            SetNextKnobReset(cSoundMainSendDefault);
            if (KnobU8("##Knob", &mainSend, 0, 127, 0, knobSize))
            {
                track->setMainSend(mainSend);
                SetUnsavedChanges(true);
            }

            KnobTooltip(messages::stream::cTrackMainSendFormat, static_cast<f32>(mainSend) / 127.0f);
            break;
        }

        case TrackParam::FxSend0:
        case TrackParam::FxSend1:
        case TrackParam::FxSend2:
        {
            const u32 fx = static_cast<u32>(param) - static_cast<u32>(TrackParam::FxSend0);

            u8 fxSend = track->getFxSend(fx);
            SetNextKnobReset(0.0);
            if (KnobU8("##Knob", &fxSend, 0, 127, 0, knobSize))
            {
                track->setFxSend(fx, fxSend);
                SetUnsavedChanges(true);
            }

            KnobTooltip(messages::stream::cTrackFxSendFormat, static_cast<char>('A' + fx), static_cast<f32>(fxSend) / 127.0f);
            break;
        }

        case TrackParam::Lpf:
        {
            u8 lpfFreq = track->getLpfFreq();
            SetNextKnobReset(cSoundLpfMax);
            if (KnobU8("##Knob", &lpfFreq, 0, 64, 0, knobSize))
            {
                track->setLpfFreq(lpfFreq);
                SetUnsavedChanges(true);
            }

            KnobTooltip(messages::stream::cTrackLpfFormat, (static_cast<f32>(lpfFreq) / 64.0f) - 1.0f);
            break;
        }

        case TrackParam::BiquadType:
        {
            u8 biquadType = track->getBiquadType();
            if (DrawBiquadTypeCombo(&biquadType, width))
            {
                track->setBiquadType(biquadType);
                SetUnsavedChanges(true);
            }

            break;
        }

        case TrackParam::BiquadValue:
        {
            u8 biquadValue = track->getBiquadValue();
            SetNextKnobReset(0.0);
            if (KnobU8("##Knob", &biquadValue, 0, 127, 0, knobSize))
            {
                track->setBiquadValue(biquadValue);
                SetUnsavedChanges(true);
            }

            KnobTooltip(messages::stream::cTrackBiquadValueFormat, static_cast<f32>(biquadValue) / 127.0f);
            break;
        }
    }

    ImGui::PopID();
}

void DrawTrackSlotGroup(const TrackContext& ctx, s32 index, TrackSlots& slots, const TrackParam* params, f32 rowHeight)
{
    slots.begin();

    for (s32 i = 0; i < slots.num; i++)
    {
        const ImVec2 size = CalcTrackParamSize(ctx, params[i], slots.widths[i]);

        slots.next(size, rowHeight);
        DrawTrackParam(ctx, index, params[i], size.x);
    }
}
