#include <ui/stream/StreamTracks.h>

#include <ui/widgets/ContextMenu.h>
#include <messages/Messages.h>
#include <ui/project/FileLists.h>
#include <ui/sound/SoundPanels.h>
#include <ui/stream/StreamTrackAdd.h>
#include <ui/stream/StreamTrackImport.h>
#include <ui/app/UI.h>
#include <ui/settings/UiColors.h>
#include <ui/widgets/Widgets.h>

static constexpr f32 cTrackKnobEm         = 2.0f;
static constexpr f32 cTrackSlotGapEm      = 0.3f;
static constexpr f32 cTrackCellPadYEm     = 0.2f;
static constexpr u8  cTrackPanCenter      = 64;

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

        ImGui::GetWindowDrawList()->AddText(ImVec2(x + ImFloor((widths[i] - labelWidth) * 0.5f), y), ImGui::GetColorU32(ImGuiCol_TextDisabled), labels[i]);
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

static TrackSlots MakeTrackSlots(const char* const* labels, s32 labelNum)
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

static void AddTrackGroup(TrackTableLayout* layout, TrackGroup group, const TrackSlots& slots)
{
    layout->groups[layout->groupNum] = group;
    layout->groupWidths[layout->groupNum] = slots.calcWidth() + layout->columnExtra;
    layout->groupNum++;
}

TrackTableLayout CalcTrackTableLayout(const TrackContext& ctx)
{
    const bool hasWave = ctx.source.sound != nullptr;

    TrackTableLayout layout = {};

    layout.mixSlots = MakeTrackSlots(cTrackMixLabels, cTrackBypassIndex);
    layout.bypassSlots = MakeTrackSlots(cTrackMixLabels + cTrackBypassIndex, IM_ARRAYSIZE(cTrackMixLabels) - cTrackBypassIndex);
    layout.sendSlots = MakeTrackSlots(cTrackSendLabels, IM_ARRAYSIZE(cTrackSendLabels));
    layout.filterSlots = MakeTrackSlots(cTrackFilterLabels, IM_ARRAYSIZE(cTrackFilterLabels));
    layout.filterSlots.widths[1] = sead::Mathf::max(layout.filterSlots.widths[1], ctx.biquadWidth);

    layout.cellPadding = CalcTrackCellPadding();
    layout.leadNum = hasWave ? 3 : 2;
    layout.columnNum = layout.leadNum + 2 + (ctx.source.hasTrackMix ? 2 : 0) + (ctx.source.enableSend ? 1 : 0) + (ctx.source.enableFilter ? 1 : 0);
    layout.indexWidth = ImCeil(ImGui::GetFrameHeight());
    layout.menuWidth = layout.indexWidth;
    layout.channelWidth = ImCeil(sead::Mathf::max(ImGui::CalcTextSize(messages::common::cChannels).x, ImGui::CalcTextSize(messages::common::cStereo).x));

    layout.columnExtra = layout.cellPadding.x * 2.0f;
    layout.leadWidth = layout.indexWidth + layout.channelWidth + layout.menuWidth + layout.columnExtra * static_cast<f32>(layout.leadNum + 2);

    if (ctx.source.hasTrackMix)
    {
        AddTrackGroup(&layout, TrackGroup::Mix, layout.mixSlots);
        AddTrackGroup(&layout, TrackGroup::Bypass, layout.bypassSlots);
    }

    if (ctx.source.enableSend)
        AddTrackGroup(&layout, TrackGroup::Send, layout.sendSlots);

    if (ctx.source.enableFilter)
        AddTrackGroup(&layout, TrackGroup::Filter, layout.filterSlots);

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

    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlappedByItem);

    PlaceTrackMenu();

    if (BeginItemContextMenu(cTrackMenuId))
    {
        HighlightStreamTrack(track);

        WaveFile* wave = static_cast<WaveFile*>(track->getWaveFileRef().getItem());

        if (wave)
            DrawWaveFileMenuItems(wave, { wave }, false);
        else if (ContextMenuItem(ICON_LC_IMPORT, messages::common::cImportAudio))
            RequestStreamTrackImport(ctx.source, track);

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

void DrawTrackListMenu(const TrackContext& ctx)
{
    if (!BeginWindowContextMenu("TrackListMenu"))
        return;

    DrawTrackAddMenu(ctx.source);
    EndContextMenu();
}

void DrawTrackWaveSelector(const TrackContext& ctx, Sound::StreamSoundInfo::Track* track, f32 width)
{
    Item* waveFile = track->getWaveFileRef().getItem();

    ImGui::SetNextItemWidth(width);
    if (ItemSelector("##WaveFile", ctx.source.waveOwner->getStreamWaveList(), &waveFile, false, true))
    {
        track->getWaveFileRef().attach(waveFile);
        SetUnsavedChanges(true);
    }
}

static ImVec2 CalcTrackParamSize(const TrackContext& ctx, TrackParam param, f32 width)
{
    const f32 frameHeight = ImGui::GetFrameHeight();
    const f32 knobSize = CalcTrackKnobSize();

    switch (param)
    {
        case TrackParam::FrontBypass:
            return ImVec2(frameHeight, frameHeight);

        case TrackParam::BiquadType:
            return ImVec2(sead::Mathf::min(width, ctx.biquadWidth), frameHeight);

        default:
            return ImVec2(knobSize, knobSize);
    }
}

static void DrawTrackParam(const TrackContext& ctx, s32 index, TrackParam param, f32 width)
{
    Sound::StreamSoundInfo::Track* track = ctx.tracks[index];
    const f32 size = CalcTrackKnobSize();
    const ImVec2 knobSize(size, size);

    ImGui::PushID(static_cast<s32>(param));

    switch (param)
    {
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
