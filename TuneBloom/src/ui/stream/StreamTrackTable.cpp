#include <ui/stream/StreamTracks.h>

#include <messages/Messages.h>
#include <ui/items/ItemColumns.h>
#include <ui/sound/SoundProperties.h>
#include <ui/app/UI.h>
#include <ui/widgets/LaneRow.h>
#include <ui/widgets/Widgets.h>

#include <imgui/imgui_custom.h>

static constexpr f32 cTrackLaneMinWidthEm   = 5.0f;
static constexpr f32 cTrackLaneMaxRowEm     = 4.0f;
static constexpr f32 cTrackKnobLaneMaxRowEm = 2.7f;
static constexpr f32 cTrackWaveMinWidthEm   = 6.5f;
static constexpr f32 cTrackWaveMaxWidthEm   = 13.0f;
static constexpr f32 cTrackWaveShare        = 0.4f;

struct TrackTableView
{
    TrackTableLayout layout;
    f32 rowHeight;
    f32 waveWidth;
    f32 laneWidth;
    s32 menuSlot;
};

struct TrackGroupInfo
{
    const char* title;
    TrackSlots* slots;
    const TrackParam* params;
};

static f32 CalcTrackFlexWidth(const TrackTableLayout& layout, f32 availWidth, f32 minWidth, f32 scrollbarWidth, s32* fitGroupNum)
{
    const f32 freeWidth = availWidth - layout.fixedWidth - scrollbarWidth;

    *fitGroupNum = layout.groupNum;

    if (freeWidth >= minWidth)
        return ImFloor(freeWidth);

    f32 usedWidth = layout.leadWidth + scrollbarWidth;

    for (s32 i = 0; i < layout.groupNum; i++)
    {
        if (usedWidth + layout.groupWidths[i] + minWidth > availWidth)
        {
            *fitGroupNum = i;
            break;
        }

        usedWidth += layout.groupWidths[i];
    }

    return ImFloor(sead::Mathf::max(minWidth, availWidth - usedWidth));
}

static TrackGroupInfo GetTrackGroupInfo(TrackTableLayout& layout, TrackGroup group)
{
    switch (group)
    {
        case TrackGroup::Mix:
            return { messages::stream::cTrackMix, &layout.mixSlots, cTrackMixParams };
        case TrackGroup::Bypass:
            return { messages::stream::cTrackBypass, &layout.bypassSlots, cTrackMixParams + cTrackBypassIndex };
        case TrackGroup::Send:
            return { messages::panel::cSend, &layout.sendSlots, cTrackSendParams };
        case TrackGroup::Filter:
            return { messages::common::cFilter, &layout.filterSlots, cTrackFilterParams };
    }

    return {};
}

static void SplitTrackFlexWidth(const TrackContext& ctx, f32 flexWidth, TrackTableView* view)
{
    const f32 em = ImGui::GetFontSize();

    view->waveWidth = 0.0f;

    if (ctx.source.sound)
        view->waveWidth = ImFloor(ImClamp(flexWidth * cTrackWaveShare, em * cTrackWaveMinWidthEm, em * cTrackWaveMaxWidthEm));

    view->laneWidth = ImFloor(sead::Mathf::max(em * cTrackLaneMinWidthEm, flexWidth - view->waveWidth));
}

static f32 CalcTrackRowExtra(const TrackTableLayout& layout)
{
    return layout.cellPadding.y * 2.0f;
}

static f32 CalcTrackMaxRowHeight(const TrackTableLayout& layout)
{
    return ImFloor(ImGui::GetFontSize() * (layout.groupNum > 0 ? cTrackKnobLaneMaxRowEm : cTrackLaneMaxRowEm));
}

static f32 CalcTrackFlexMinWidth(const TrackContext& ctx)
{
    return ImFloor(ImGui::GetFontSize() * (cTrackLaneMinWidthEm + (ctx.source.sound ? cTrackWaveMinWidthEm : 0.0f)));
}

static f32 CalcTrackTitleHeight(const TrackTableLayout& layout, bool scrollX)
{
    return ImGui::GetFrameHeight() + CalcTrackRowExtra(layout) + 1.0f + (scrollX ? ImGui::GetStyle().ScrollbarSize : 0.0f);
}

static f32 CalcTrackRowHeight(const TrackContext& ctx, const TrackTableLayout& layout, f32 availHeight, bool scrollX)
{
    const s32 rowNum = sead::Mathf::max(1, static_cast<s32>(ctx.tracks.size()));
    const f32 freeHeight = availHeight - CalcTrackTitleHeight(layout, scrollX);
    const f32 fitHeight = ImFloor(freeHeight / static_cast<f32>(rowNum) - CalcTrackRowExtra(layout));

    return ImClamp(fitHeight, CalcTrackKnobSize(), CalcTrackMaxRowHeight(layout));
}

f32 CalcTrackTableFitHeight(const TrackContext& ctx)
{
    const TrackTableLayout layout = CalcTrackTableLayout(ctx);

    s32 fitGroupNum = 0;
    CalcTrackFlexWidth(layout, ImGui::GetContentRegionAvail().x, CalcTrackFlexMinWidth(ctx), 0.0f, &fitGroupNum);

    const f32 titleHeight = CalcTrackTitleHeight(layout, fitGroupNum < layout.groupNum);

    if (ctx.tracks.empty())
        return titleHeight + ImGui::GetFrameHeight() + CalcTrackRowExtra(layout);

    return titleHeight + static_cast<f32>(ctx.tracks.size()) * (CalcTrackMaxRowHeight(layout) + CalcTrackRowExtra(layout));
}

static void SetupTrackTableColumns(const TrackContext& ctx, TrackTableView& view)
{
    TrackTableLayout& layout = view.layout;

    ImGui::TableSetupScrollFreeze(layout.leadNum, 1);
    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, layout.indexWidth);

    if (ctx.source.sound)
        ImGui::TableSetupColumn(cTrackWaveLabel, ImGuiTableColumnFlags_WidthFixed, view.waveWidth);

    ImGui::TableSetupColumn(messages::common::cChannels, ImGuiTableColumnFlags_WidthFixed, layout.channelWidth);
    ImGui::TableSetupColumn("###Lane", ImGuiTableColumnFlags_WidthFixed, view.laneWidth);

    for (s32 i = 0; i <= layout.groupNum; i++)
    {
        if (i == view.menuSlot)
            ImGui::TableSetupColumn("###Menu", ImGuiTableColumnFlags_WidthFixed, layout.menuWidth);

        if (i < layout.groupNum)
            ImGui::TableSetupColumn(GetTrackGroupInfo(layout, layout.groups[i]).title, ImGuiTableColumnFlags_WidthFixed, layout.groupWidths[i] - layout.columnExtra);
    }
}

static void DrawTrackTitle(const char* text, bool centered = false)
{
    ImGui::TableNextColumn();

    if (centered)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImFloor((ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(text).x) * 0.5f));

    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", text);
}

static void DrawTrackTableTitles(const TrackContext& ctx, TrackTableView& view)
{
    TrackTableLayout& layout = view.layout;

    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);

    DrawTrackTitle("#", true);

    if (ctx.source.sound)
        DrawTrackTitle(cTrackWaveLabel);

    DrawTrackTitle(messages::common::cChannels);

    ImGui::TableNextColumn();
    DrawTrackLaneRuler(ctx);

    for (s32 i = 0; i <= layout.groupNum; i++)
    {
        if (i == view.menuSlot)
            ImGui::TableNextColumn();

        if (i >= layout.groupNum)
            continue;

        ImGui::TableNextColumn();
        GetTrackGroupInfo(layout, layout.groups[i]).slots->drawLabels();
    }
}

static bool DrawTrackIndexCell(const TrackContext& ctx, s32 index, f32 rowHeight, TrackEdit* edit)
{
    Sound::StreamSoundInfo::Track* track = ctx.tracks[index];

    ImGui::TableNextColumn();

    const ImVec2 cellMin = ImGui::GetCursorScreenPos();
    const f32 cellWidth = ImGui::GetContentRegionAvail().x;
    const bool hovered = DrawTrackHandle(ctx, track, rowHeight, edit);

    DrawLaneIndexBadge(index, cellMin, ImVec2(cellWidth, rowHeight), track == GetHighlightedStreamTrack());

    return hovered;
}

static void CenterTrackCellFrame(f32 rowHeight)
{
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImFloor((rowHeight - ImGui::GetFrameHeight()) * 0.5f));
}

static void DrawTrackWaveCell(const TrackContext& ctx, s32 index, f32 rowHeight)
{
    Sound::StreamSoundInfo::Track* track = ctx.tracks[index];
    Item* waveFile = track->getWaveFileRef().getItem();

    ImGui::TableNextColumn();
    CenterTrackCellFrame(rowHeight);

    DrawTrackWaveSelector(ctx, track, ImGui::GetContentRegionAvail().x - TrailingButtonsWidth(1));

    if (TrailingGoToButton(ICON_LC_EXTERNAL_LINK "###GoWave", waveFile, messages::sound::cWaveFileGoTo, messages::reference::cNoWaveFile))
        SelectItem(waveFile);
}

static void DrawTrackChannelCell(const TrackContext& ctx, s32 index, f32 rowHeight)
{
    sead::FixedSafeString<16> text;
    FormatChannelCount(&text, ctx.tracks[index]->getChannelCount(ctx.source.stream->getStreamType()));

    ImGui::TableNextColumn();
    CenterTrackCellFrame(rowHeight);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", text.cstr());
}

static void DrawTrackLaneCell(const TrackContext& ctx, s32 index, f32 rowHeight)
{
    ImGui::TableNextColumn();
    DrawTrackLane(ctx, index, rowHeight);
}

static void DrawTrackGroupCell(const TrackContext& ctx, s32 index, TrackTableLayout& layout, TrackGroup group, f32 rowHeight)
{
    const TrackGroupInfo info = GetTrackGroupInfo(layout, group);

    ImGui::TableNextColumn();
    DrawTrackSlotGroup(ctx, index, *info.slots, info.params, rowHeight);
}

static void DrawTrackMenuCell(f32 rowHeight, bool rowHovered)
{
    ImGui::TableNextColumn();
    CenterTrackCellFrame(rowHeight);
    DrawLaneRowMenuButton(cTrackMenuId, rowHovered, messages::stream::cTrackMenuTip);
}

static void DrawTrackTableRow(const TrackContext& ctx, s32 index, TrackTableView& view, TrackEdit* edit)
{
    TrackTableLayout& layout = view.layout;

    ImGui::PushID(ctx.tracks[index]);
    ImGui::TableNextRow();

    const bool rowHovered = DrawTrackIndexCell(ctx, index, view.rowHeight, edit);

    if (ctx.source.sound)
        DrawTrackWaveCell(ctx, index, view.rowHeight);

    DrawTrackChannelCell(ctx, index, view.rowHeight);
    DrawTrackLaneCell(ctx, index, view.rowHeight);

    for (s32 i = 0; i <= layout.groupNum; i++)
    {
        if (i == view.menuSlot)
            DrawTrackMenuCell(view.rowHeight, rowHovered);

        if (i < layout.groupNum)
            DrawTrackGroupCell(ctx, index, layout, layout.groups[i], view.rowHeight);
    }

    ImGui::PopID();
}

// One row for each track
void DrawTrackTable(const TrackContext& ctx, TrackEdit* edit)
{
    const ImVec2 avail = ImGui::GetContentRegionAvail();

    TrackTableView view = {};
    view.layout = CalcTrackTableLayout(ctx);

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, view.layout.cellPadding);
    const bool tableOpen = ImGui::BeginTable("TrackTable", view.layout.columnNum, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY);

    if (!tableOpen)
    {
        ImGui::PopStyleVar();
        return;
    }

    const f32 scrollbarWidth = ImGui::GetCurrentWindow()->ScrollbarY ? ImGui::GetStyle().ScrollbarSize : 0.0f;

    SplitTrackFlexWidth(ctx, CalcTrackFlexWidth(view.layout, avail.x, CalcTrackFlexMinWidth(ctx), scrollbarWidth, &view.menuSlot), &view);
    view.rowHeight = CalcTrackRowHeight(ctx, view.layout, avail.y, view.menuSlot < view.layout.groupNum);
    SetupTrackTableColumns(ctx, view);
    DrawTrackTableTitles(ctx, view);

    if (ctx.tracks.empty())
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(view.layout.leadNum);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", messages::stream::cTrackNone);
    }

    for (s32 i = 0; i < static_cast<s32>(ctx.tracks.size()); i++)
        DrawTrackTableRow(ctx, i, view, edit);

    DrawTrackListMenu(ctx);

    ImGui::EndTable();
    ImGui::PopStyleVar();
}
