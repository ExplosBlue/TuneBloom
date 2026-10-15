#include <ui/stream/StreamTracks.h>

#include <ui/Messages.h>
#include <ui/player/OutputMeter.h>
#include <ui/sound/SoundPanels.h>
#include <ui/app/UI.h>
#include <ui/settings/UiColors.h>

static constexpr f32 cTrackStripGapEm        = 0.3f;
static constexpr f32 cTrackStripSectionGapEm = 0.7f;
static constexpr f32 cTrackStripPadYEm       = 0.4f;
static constexpr f32 cTrackFaderWidthEm      = 1.1f;
static constexpr f32 cTrackFaderMinHeightEm  = 3.5f;
static constexpr f32 cTrackStripAlpha        = 0.03f;
static constexpr f32 cTrackStripHoveredAlpha = 0.07f;
static constexpr f32 cTrackLabelBrightness   = 0.45f;
static constexpr s32 cTrackStripSlotMax      = 16;
static constexpr f32 cTrackStripMinWidthEm   = 5.0f;
static constexpr f32 cTrackShortStripWidthEm = 4.5f;
static constexpr f32 cTrackDetailMinWidthEm  = 16.0f;
static constexpr s32 cTrackPanIndex          = 1;
static constexpr s32 cTrackSurroundFirst     = 2;
static constexpr f32 cTrackMeterBarEm        = 0.25f;
static constexpr f32 cTrackMeterGapEm        = 0.2f;
static constexpr f32 cTrackMeterBarGap       = 2.0f;

static constexpr const char* cTrackSurroundLabels[] = { messages::common::cPan, messages::stream::cTrackBypass };

static const TrackParam cTrackWaveParams[] = { TrackParam::WaveFile };

static OutputMeter::ChannelState sTrackMeters[cStrmTrackNum][snd::cWaveChannelMax];

struct TrackStripSlot
{
    TrackParam param;
    const char* label;
    f32 center;
    f32 y;
    f32 height;
};

struct TrackStripLayout
{
    f32 em;
    f32 frameHeight;
    f32 knobSize;
    f32 gap;
    f32 sectionGap;
    f32 faderMinHeight;
    f32 faderWidth;
    f32 barWidth;
    f32 meterGap;
    f32 meterWidth;
    f32 levelWidth;
    f32 stateWidth;
    ImVec2 padding;

    s32 trackNum;
    s32 stripNum;
    s32 columnNum;
    bool isShort;
    f32 labelWidth;
    f32 stripWidth;
    f32 tableWidth;
    f32 innerHeight;

    TrackStripSlot slots[cTrackStripSlotMax];
    s32 slotNum;
    f32 slotEndY;

    f32 valueCenter;
    f32 valueY;
    f32 faderX;
    f32 faderY;
    f32 faderHeight;
    f32 stateX;
    f32 stateY;
    f32 numberY;
    f32 numberWidth;
    f32 stripHeight;
};

struct TrackStripInput
{
    s32 hoveredColumn;
    bool clicked;
    bool playing;
};

static void DrawTrackVolumeFader(Sound::StreamSoundInfo::Track* track, const ImVec2& size)
{
    const u8 min = 0;
    const u8 max = 255;

    u8 volume = track->getVolume();
    bool changed = ImGui::VSliderScalar("##VolumeFader", size, ImGuiDataType_U8, &volume, &min, &max, "");
    changed |= ResetItemOnRightClick(&volume, cSoundVolumeDefault);

    if (changed)
    {
        track->setVolume(volume);
        SetUnsavedChanges(true);
    }

    KnobTooltip(messages::stream::cTrackVolumeFormat, static_cast<f32>(volume) / 127.0f);
}

static f32 CalcTrackDetailWidth(const TrackContext& ctx)
{
    const TrackTableLayout layout = CalcTrackTableLayout(ctx);
    const s32 columnNum = 1 + (ctx.enableSend ? 1 : 0) + (ctx.enableFilter ? 1 : 0);

    const f32 width = layout.filterWidth + layout.sendWidth + MakeTrackSlots(cTrackSurroundLabels, IM_ARRAYSIZE(cTrackSurroundLabels)).calcWidth() + (layout.cellPadding.x * 2.0f + 1.0f) * columnNum + 1.0f;

    return sead::Mathf::max(width, ImFloor(ImGui::GetFontSize() * cTrackDetailMinWidthEm));
}

static void DrawTrackDetailPanel(const TrackContext& ctx, s32 index, const char* title, TrackSlots& slots, const TrackParam* params)
{
    const f32 rowHeight = CalcTrackKnobSize();

    ImGui::TableNextColumn();
    RegionHeader(title);

    const ImVec2 pos = ImGui::GetCursorPos();

    DrawTrackSlotGroup(ctx, index, slots, params, rowHeight);

    ImGui::SetCursorPos(ImVec2(pos.x, pos.y + rowHeight));
    slots.drawLabels();
}

static void DrawTrackDetailTitle(const TrackContext& ctx, s32 index, f32 padX)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 bandHeight = ImGui::GetFrameHeight();

    const sead::FormatFixedSafeString<32> title(messages::stream::cTrackNameFormat, index);
    const Item* waveFile = ctx.tracks[index]->getWaveFileRef().getItem();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + sead::Mathf::max(ImGui::GetContentRegionAvail().x, CalcTrackDetailWidth(ctx)), min.y + bandHeight);
    const ImVec2 textPos(min.x + padX, min.y + style.FramePadding.y);
    const f32 nameX = textPos.x + ImGui::CalcTextSize(title.cstr()).x + style.ItemSpacing.x;
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    drawList->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_CheckMark, uicolors::Pick(cTrackIndexAlpha)));
    drawList->AddText(textPos, ImGui::GetColorU32(ImGuiCol_Text), title.cstr());

    if (waveFile)
        ImGui::RenderTextEllipsis(drawList, ImVec2(nameX, textPos.y), ImVec2(max.x - padX, max.y), max.x - padX, max.x - padX, waveFile->getFormattedName().cstr(), nullptr, nullptr);

    ImGui::Dummy(ImVec2(0.0f, bandHeight));
}

static void DrawTrackDetail(const TrackContext& ctx, s32 index, f32 width)
{
    Sound::StreamSoundInfo::Track* track = ctx.tracks[index];

    TrackTableLayout layout = CalcTrackTableLayout(ctx);
    TrackSlots surroundSlots = MakeTrackSlots(cTrackSurroundLabels, IM_ARRAYSIZE(cTrackSurroundLabels));
    const s32 columnNum = 1 + (ctx.enableSend ? 1 : 0) + (ctx.enableFilter ? 1 : 0);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("TrackDetail", ImVec2(width, 0.0f), ImGuiChildFlags_Border, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PopStyleVar();

    DrawTrackDetailTitle(ctx, index, layout.cellPadding.x);

    ImGui::SetCursorPosY(ImGui::GetFrameHeight());

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(layout.cellPadding.x, layout.cellPadding.x));

    if (ImGui::BeginTable("TrackDetailPanels", columnNum, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_NoHostExtendX))
    {
        if (ctx.enableFilter)
            ImGui::TableSetupColumn(messages::common::cFilter, ImGuiTableColumnFlags_WidthFixed, layout.filterWidth);

        if (ctx.enableSend)
            ImGui::TableSetupColumn(messages::panel::cSend, ImGuiTableColumnFlags_WidthFixed, layout.sendWidth);

        ImGui::TableSetupColumn(messages::common::cSurround, ImGuiTableColumnFlags_WidthFixed, surroundSlots.calcWidth());

        ImGui::PushID(track);
        ImGui::TableNextRow(ImGuiTableRowFlags_None, ImGui::GetContentRegionAvail().y - layout.cellPadding.x * 2.0f - 1.0f);

        if (ctx.enableFilter)
            DrawTrackDetailPanel(ctx, index, messages::common::cFilter, layout.filterSlots, cTrackFilterParams);

        if (ctx.enableSend)
            DrawTrackDetailPanel(ctx, index, messages::panel::cSend, layout.sendSlots, cTrackSendParams);

        DrawTrackDetailPanel(ctx, index, messages::common::cSurround, surroundSlots, cTrackMixParams + cTrackSurroundFirst);

        ImGui::PopID();
        ImGui::EndTable();
    }

    ImGui::PopStyleVar();
    ImGui::EndChild();
}

static void CalcTrackStripSizes(const TrackContext& ctx, TrackStripLayout* layout)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 em = ImGui::GetFontSize();
    const f32 frameHeight = ImGui::GetFrameHeight();
    const f32 knobSize = CalcTrackKnobSize();
    const f32 gap = ImFloor(em * cTrackStripGapEm);
    const f32 sectionGap = ImFloor(em * cTrackStripSectionGapEm);
    const f32 faderMinHeight = ImFloor(em * cTrackFaderMinHeightEm);
    const f32 faderWidth = ImFloor(em * cTrackFaderWidthEm);
    const f32 barWidth = ImFloor(em * cTrackMeterBarEm);
    const f32 meterGap = ImFloor(em * cTrackMeterGapEm);
    const f32 meterWidth = barWidth * 2.0f + cTrackMeterBarGap;
    const f32 levelWidth = faderWidth + meterGap + meterWidth;
    const f32 stateWidth = frameHeight * 2.0f + style.ItemInnerSpacing.x;
    const ImVec2 padding(ImFloor(em * cTrackCellPadXEm), ImFloor(em * cTrackStripPadYEm));
    const ImVec2 avail = ImGui::GetContentRegionAvail();

    const s32 trackNum = static_cast<s32>(ctx.tracks.size());
    const s32 stripNum = trackNum > 0 ? trackNum : 1;
    const s32 columnNum = 1 + stripNum;

    f32 labelWidth = ImGui::CalcTextSize(cTrackWaveLabel).x;
    for (const char* label : cTrackMixLabels)
        labelWidth = sead::Mathf::max(labelWidth, ImGui::CalcTextSize(label).x);

    labelWidth = ImCeil(labelWidth);

    const f32 frameExtra = padding.y * 2.0f + 2.0f;
    const f32 columnExtra = padding.x * 2.0f + 1.0f;
    const f32 minWidth = sead::Mathf::max(ImFloor(em * cTrackStripMinWidthEm), stateWidth);

    f32 fullHeight = frameHeight + sectionGap + knobSize * 2.0f + gap * 2.0f + frameHeight + sectionGap + em + gap + faderMinHeight + gap + frameHeight + gap + frameHeight;

    if (ctx.enableFilter)
        fullHeight += knobSize * 2.0f + frameHeight + gap * 2.0f + sectionGap;

    if (ctx.enableSend)
        fullHeight += knobSize * 4.0f + gap * 3.0f + sectionGap;

    const bool isShort = trackNum > 0 && fullHeight + frameExtra > avail.y;

    f32 tableWidth = 0.0f;
    f32 stripWidth = minWidth;

    const auto calcFreeWidth = [&](f32 reservedWidth)
    {
        return avail.x - reservedWidth - labelWidth - columnExtra * columnNum - 2.0f;
    };
    const auto calcContentWidth = [&](s32 strips)
    {
        return labelWidth + stripWidth * strips + columnExtra * (strips + 1) + 2.0f;
    };
    const auto fitStrips = [&](f32 freeWidth, f32 stripMin)
    {
        stripWidth = ImFloor(sead::Mathf::max(stripMin, freeWidth / stripNum));
        labelWidth += sead::Mathf::max(0.0f, ImFloor(freeWidth - stripWidth * stripNum));
    };

    if (isShort)
    {
        const f32 detailWidth = CalcTrackDetailWidth(ctx) + style.ItemSpacing.x;

        fitStrips(calcFreeWidth(detailWidth), sead::Mathf::max(ImFloor(em * cTrackShortStripWidthEm), stateWidth));
        tableWidth = ImFloor(sead::Mathf::min(calcContentWidth(stripNum), sead::Mathf::max(avail.x - detailWidth, calcContentWidth(1))));
    }
    else
    {
        fitStrips(calcFreeWidth(0.0f), minWidth);
        tableWidth = avail.x;
    }

    const bool scrollX = calcContentWidth(stripNum) > tableWidth;

    layout->em = em;
    layout->frameHeight = frameHeight;
    layout->knobSize = knobSize;
    layout->gap = gap;
    layout->sectionGap = sectionGap;
    layout->faderMinHeight = faderMinHeight;
    layout->faderWidth = faderWidth;
    layout->barWidth = barWidth;
    layout->meterGap = meterGap;
    layout->meterWidth = meterWidth;
    layout->levelWidth = levelWidth;
    layout->stateWidth = stateWidth;
    layout->padding = padding;
    layout->trackNum = trackNum;
    layout->stripNum = stripNum;
    layout->columnNum = columnNum;
    layout->isShort = isShort;
    layout->labelWidth = labelWidth;
    layout->stripWidth = stripWidth;
    layout->tableWidth = tableWidth;
    layout->innerHeight = avail.y - (scrollX ? style.ScrollbarSize : 0.0f) - frameExtra;
}

static void AddTrackStripSlots(const TrackContext& ctx, TrackStripLayout* layout, const char* const* labels, const TrackParam* params, s32 num)
{
    if (layout->slotNum > 0)
        layout->slotEndY += layout->sectionGap - layout->gap;

    for (s32 i = 0; i < num; i++)
    {
        SEAD_ASSERT(layout->slotNum < cTrackStripSlotMax);

        const f32 height = CalcTrackParamSize(ctx, params[i], 0.0f).y;

        layout->slots[layout->slotNum++] = { params[i], labels[i], layout->stripWidth * 0.5f, layout->slotEndY, height };
        layout->slotEndY += height + layout->gap;
    }
}

// The places of the controls
static void PlaceTrackStripControls(const TrackContext& ctx, TrackStripLayout* layout)
{
    const f32 em = layout->em;
    const f32 frameHeight = layout->frameHeight;
    const f32 knobSize = layout->knobSize;
    const f32 gap = layout->gap;
    const f32 stripWidth = layout->stripWidth;

    layout->slotNum = 0;
    layout->slotEndY = 0.0f;

    layout->valueCenter = stripWidth * 0.5f;
    layout->valueY = 0.0f;
    layout->faderX = ImFloor((stripWidth - layout->levelWidth) * 0.5f);
    layout->faderY = 0.0f;
    layout->faderHeight = 0.0f;
    layout->stateX = ImFloor((stripWidth - layout->stateWidth) * 0.5f);
    layout->stateY = 0.0f;
    layout->numberY = 0.0f;
    layout->numberWidth = stripWidth;

    AddTrackStripSlots(ctx, layout, &cTrackWaveLabel, cTrackWaveParams, IM_ARRAYSIZE(cTrackWaveParams));

    if (layout->isShort)
    {
        const f32 pairX = ImFloor((stripWidth - knobSize - gap - layout->levelWidth) * 0.5f);
        const f32 bodyY = frameHeight + gap;
        const f32 bodyHeight = ImFloor(sead::Mathf::max(knobSize + gap + em, layout->innerHeight - bodyY - gap - frameHeight));

        layout->slots[layout->slotNum++] = { TrackParam::Pan, cTrackMixLabels[cTrackPanIndex], pairX + knobSize * 0.5f, bodyY, knobSize };

        layout->valueCenter = pairX + knobSize * 0.5f;
        layout->valueY = bodyY + knobSize + gap;
        layout->faderX = pairX + knobSize + gap;
        layout->faderY = bodyY;
        layout->faderHeight = bodyHeight;
        layout->stateX = stripWidth - layout->stateWidth;
        layout->stateY = bodyY + bodyHeight + gap;
        layout->numberY = layout->stateY;
        layout->numberWidth = stripWidth - layout->stateWidth - ImGui::GetStyle().ItemInnerSpacing.x;
    }
    else
    {
        if (ctx.enableFilter)
            AddTrackStripSlots(ctx, layout, cTrackFilterLabels, cTrackFilterParams, IM_ARRAYSIZE(cTrackFilterParams));

        if (ctx.enableSend)
            AddTrackStripSlots(ctx, layout, cTrackSendLabels, cTrackSendParams, IM_ARRAYSIZE(cTrackSendParams));

        AddTrackStripSlots(ctx, layout, cTrackMixLabels + cTrackPanIndex, cTrackMixParams + cTrackPanIndex, IM_ARRAYSIZE(cTrackMixParams) - cTrackPanIndex);

        layout->valueY = layout->slotEndY + layout->sectionGap - gap;
        layout->faderY = layout->valueY + em + gap;
        layout->faderHeight = ImFloor(sead::Mathf::max(layout->faderMinHeight, layout->innerHeight - layout->faderY - gap - frameHeight - gap - frameHeight));
        layout->stateY = layout->faderY + layout->faderHeight + gap;
        layout->numberY = layout->stateY + frameHeight + gap;
    }

    layout->stripHeight = layout->numberY + frameHeight;
}

static void DrawTrackStripLabels(const TrackStripLayout& layout)
{
    const ImVec4 labelColor = ImLerp(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), ImGui::GetStyleColorVec4(ImGuiCol_Text), cTrackLabelBrightness);
    const ImVec2 pos = ImGui::GetCursorPos();
    const ImVec2 screenPos = ImGui::GetCursorScreenPos();
    const ImU32 color = ImGui::GetColorU32(labelColor);

    for (s32 i = 0; i < layout.slotNum; i++)
    {
        const TrackStripSlot& slot = layout.slots[i];
        const f32 x = screenPos.x + layout.labelWidth - ImGui::CalcTextSize(slot.label).x;

        ImGui::GetWindowDrawList()->AddText(ImVec2(x, screenPos.y + slot.y + ImFloor((slot.height - layout.em) * 0.5f)), color, slot.label);
    }

    ImGui::GetWindowDrawList()->AddText(ImVec2(screenPos.x + layout.labelWidth - ImGui::CalcTextSize(cTrackMixLabels[0]).x, screenPos.y + layout.valueY), color, cTrackMixLabels[0]);

    ImGui::SetCursorPos(pos);
    ImGui::Dummy(ImVec2(layout.labelWidth, layout.stripHeight));
}

static void DrawTrackStripVolume(Sound::StreamSoundInfo::Track* track, const TrackStripLayout& layout, const ImVec2& pos, const ImVec2& screenPos)
{
    const sead::FormatFixedSafeString<16> value("%u", track->getVolume());

    ImGui::GetWindowDrawList()->AddText(ImVec2(screenPos.x + ImFloor(layout.valueCenter - ImGui::CalcTextSize(value.cstr()).x * 0.5f), screenPos.y + layout.valueY), ImGui::GetColorU32(ImGuiCol_Text), value.cstr());

    ImGui::SetCursorPos(ImVec2(pos.x + layout.faderX, pos.y + layout.faderY));
    DrawTrackVolumeFader(track, ImVec2(layout.faderWidth, layout.faderHeight));
}

static void DrawTrackStripLevel(const TrackContext& ctx, s32 trackIdx, const TrackStripLayout& layout, const ImVec2& screenPos, bool playing)
{
    const s32 channelNum = ctx.tracks[trackIdx]->getChannelCount(ctx.sound->getStreamSoundInfo().getStreamType()) >= 2 ? 2 : 1;
    const f32 width = channelNum == 2 ? layout.barWidth : layout.meterWidth;

    for (s32 ch = 0; ch < channelNum && trackIdx < static_cast<s32>(cStrmTrackNum); ch++)
    {
        OutputMeter::ChannelState& meter = sTrackMeters[trackIdx][ch];
        const f32 peak = playing ? sSoundPlayer.getStreamPlayer().fetchTrackPeak(trackIdx, ch) : 0.0f;
        const ImVec2 min(screenPos.x + layout.faderX + layout.faderWidth + layout.meterGap + (layout.barWidth + cTrackMeterBarGap) * ch, screenPos.y + layout.faderY);

        OutputMeter::updateChannel(meter, peak, ImGui::GetIO().DeltaTime);
        DrawOutputMeterBar(meter, min, ImVec2(min.x + width, min.y + layout.faderHeight), false);
    }
}

static void DrawTrackStripNumber(const TrackContext& ctx, s32 trackIdx, const TrackStripLayout& layout, const ImVec2& pos, const ImVec2& screenPos, bool selected, TrackEdit* edit)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const sead::FormatFixedSafeString<16> number("%d", trackIdx);
    const ImVec2 min(screenPos.x, screenPos.y + layout.numberY);
    const ImVec2 max(min.x + layout.numberWidth, min.y + layout.frameHeight);

    ImGui::GetWindowDrawList()->AddRectFilled(min, max, selected ? ImGui::GetColorU32(ImGuiCol_CheckMark, uicolors::Pick(cTrackIndexAlpha)) : ImGui::GetColorU32(ImGuiCol_TableHeaderBg), style.FrameRounding);

    ImGui::SetCursorPos(ImVec2(pos.x, pos.y + layout.numberY));
    DrawTrackHandle(ctx, ctx.tracks[trackIdx], ImVec2(layout.numberWidth, layout.frameHeight), ImGuiSelectableFlags_None, edit);

    ImGui::GetWindowDrawList()->AddText(ImVec2(min.x + ImFloor((layout.numberWidth - ImGui::CalcTextSize(number.cstr()).x) * 0.5f), min.y + style.FramePadding.y), ImGui::GetColorU32(ImGuiCol_Text), number.cstr());
}

static void DrawTrackStrip(const TrackContext& ctx, s32 trackIdx, const TrackStripLayout& layout, const TrackStripInput& input, TrackEdit* edit)
{
    Sound::StreamSoundInfo::Track* track = ctx.tracks[trackIdx];
    const bool hovered = input.hoveredColumn == trackIdx + 1;

    ImGui::TableNextColumn();
    ImGui::PushID(track);

    if (input.clicked && hovered)
        HighlightStreamTrack(track);

    const bool selected = track == GetHighlightedStreamTrack();

    if (selected)
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(ImGuiCol_CheckMark, uicolors::Pick(cTrackSelectedAlpha)));
    else
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(ImGuiCol_Text, hovered ? cTrackStripHoveredAlpha : cTrackStripAlpha));

    const ImVec2 pos = ImGui::GetCursorPos();
    const ImVec2 screenPos = ImGui::GetCursorScreenPos();

    for (s32 i = 0; i < layout.slotNum; i++)
    {
        const TrackStripSlot& slot = layout.slots[i];
        const ImVec2 size = CalcTrackParamSize(ctx, slot.param, layout.stripWidth);

        ImGui::SetCursorPos(ImVec2(pos.x + ImFloor(slot.center - size.x * 0.5f), pos.y + slot.y));
        DrawTrackParam(ctx, trackIdx, slot.param, size.x);
    }

    DrawTrackStripVolume(track, layout, pos, screenPos);
    DrawTrackStripLevel(ctx, trackIdx, layout, screenPos, input.playing);

    if (!ctx.sound->getStreamSoundInfo().isTrackAudible(static_cast<u32>(trackIdx)))
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(screenPos.x - layout.padding.x, screenPos.y - layout.padding.y), ImVec2(screenPos.x + layout.stripWidth + layout.padding.x, screenPos.y + layout.faderY + layout.faderHeight + ImFloor(layout.gap * 0.5f)), ImGui::GetColorU32(ImGuiCol_WindowBg, cTrackSilentAlpha));

    // Mute and Solo
    ImGui::SetCursorPos(ImVec2(pos.x + layout.stateX, pos.y + layout.stateY));
    DrawTrackParam(ctx, trackIdx, TrackParam::Mute, layout.frameHeight);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    DrawTrackParam(ctx, trackIdx, TrackParam::Solo, layout.frameHeight);

    DrawTrackStripNumber(ctx, trackIdx, layout, pos, screenPos, selected, edit);

    ImGui::PopID();
}

static s32 FindSelectedTrackStrip(const TrackContext& ctx, bool select)
{
    s32 selectedIdx = 0;
    for (s32 i = 0; i < static_cast<s32>(ctx.tracks.size()); i++)
    {
        if (ctx.tracks[i] == GetHighlightedStreamTrack())
            selectedIdx = i;
    }

    if (select && ctx.tracks[selectedIdx] != GetHighlightedStreamTrack())
        HighlightStreamTrack(ctx.tracks[selectedIdx]);

    return selectedIdx;
}

void DrawTrackStrips(const TrackContext& ctx, TrackEdit* edit)
{
    TrackStripLayout layout = {};
    CalcTrackStripSizes(ctx, &layout);
    PlaceTrackStripControls(ctx, &layout);

    const s32 selectedIdx = FindSelectedTrackStrip(ctx, layout.isShort);

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, layout.padding);
    const bool tableOpen = ImGui::BeginTable("TrackStrips", layout.columnNum, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY, ImVec2(layout.tableWidth, 0.0f));

    if (!tableOpen)
    {
        ImGui::PopStyleVar();
        return;
    }

    ImGui::TableSetupScrollFreeze(1, 0);
    ImGui::TableSetupColumn("Labels", ImGuiTableColumnFlags_WidthFixed, layout.labelWidth);

    for (s32 i = 0; i < layout.stripNum; i++)
        ImGui::TableSetupColumn("Track", ImGuiTableColumnFlags_WidthFixed, layout.stripWidth);

    ImGui::TableNextRow();

    // Labels
    ImGui::TableNextColumn();

    if (layout.trackNum == 0)
    {
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", messages::stream::cTrackNone);
    }
    else
    {
        DrawTrackStripLabels(layout);
    }

    TrackStripInput input = {};
    input.hoveredColumn = ImGui::TableGetHoveredColumn();
    input.clicked = ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    input.playing = sSoundPlayer.getPlayingSound() == ctx.sound;

    for (s32 trackIdx = 0; trackIdx < layout.trackNum; trackIdx++)
        DrawTrackStrip(ctx, trackIdx, layout, input, edit);

    DrawTrackListMenu(ctx, edit);

    ImGui::EndTable();
    ImGui::PopStyleVar();

    if (!layout.isShort)
        return;

    ImGui::SameLine();

    const f32 detailX = ImGui::GetCursorPosX();
    const f32 detailWidth = sead::Mathf::min(CalcTrackDetailWidth(ctx) + 2.0f, ImGui::GetContentRegionAvail().x);

    ImGui::SetCursorPosX(detailX + ImGui::GetContentRegionAvail().x - detailWidth);
    DrawTrackDetail(ctx, selectedIdx, detailWidth);
}
