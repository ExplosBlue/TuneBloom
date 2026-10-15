#include <ui/stream/StreamTracks.h>

#include <ui/Messages.h>
#include <ui/sound/SoundProperties.h>
#include <ui/app/UI.h>
#include <ui/settings/UiColors.h>

#include <imgui/imgui_custom.h>

static constexpr f32 cTrackWaveMinWidthEm  = 5.0f;
static constexpr f32 cTrackWaveLinkWidthEm = 6.5f;
static constexpr f32 cTrackWaveFullWidthEm = 9.5f;
static constexpr f32 cTrackWaveWideWidthEm = 13.0f;
static constexpr f32 cTrackChannelsMinEm   = 20.0f;
static constexpr f32 cTrackHoveredAlpha    = 0.05f;

static const char* cTrackChannelWidest = messages::common::cStereo;

static const TrackParam cTrackMuteParams[] = { TrackParam::Mute };
static const TrackParam cTrackSoloParams[] = { TrackParam::Solo };

struct TrackTableView
{
    TrackTableLayout layout;
    f32 rowHeight;
    f32 channelWidth;
    bool compactWave;
    bool showWaveLink;
    bool showChannels;
};

static f32 CalcTrackWaveWidth(const TrackTableLayout& layout, f32 availWidth, f32 minWidth, bool* allInView)
{
    const f32 scrollbarWidth = ImGui::GetCurrentWindow()->ScrollbarY ? ImGui::GetStyle().ScrollbarSize : 0.0f;
    const f32 freeWidth = availWidth - layout.fixedWidth - scrollbarWidth;

    *allInView = freeWidth >= minWidth;

    if (*allInView)
        return ImFloor(freeWidth);

    f32 usedWidth = layout.leadWidth + scrollbarWidth;

    for (s32 i = 0; i < layout.groupNum; i++)
    {
        if (usedWidth + layout.groupWidths[i] + minWidth > availWidth)
            break;

        usedWidth += layout.groupWidths[i];
    }

    return ImFloor(sead::Mathf::max(minWidth, availWidth - usedWidth));
}

static void DimTrackCell(bool toTableEdge)
{
    ImGuiTable* table = ImGui::GetCurrentTable();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    ImRect rect = ImGui::TableGetCellBgRect(table, table->CurrentColumn);
    ImRect clip(drawList->GetClipRectMin(), drawList->GetClipRectMax());

    rect.Min.y += 1.0f;

    if (toTableEdge)
    {
        rect.Max.x = table->InnerClipRect.Max.x;
        clip.Max.x = table->InnerClipRect.Max.x;
    }

    drawList->PushClipRect(clip.Min, clip.Max);
    drawList->AddRectFilled(rect.Min, rect.Max, ImGui::GetColorU32(ImGuiCol_WindowBg, cTrackSilentAlpha));
    drawList->PopClipRect();
}

static void SetupTrackTableColumns(const TrackContext& ctx, const TrackTableLayout& layout, f32 waveWidth)
{
    ImGui::TableSetupScrollFreeze(3, 1);
    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, layout.indexWidth);
    ImGui::TableSetupColumn(messages::stream::cTrackMute, ImGuiTableColumnFlags_WidthFixed, layout.muteWidth);
    ImGui::TableSetupColumn(messages::stream::cTrackSolo, ImGuiTableColumnFlags_WidthFixed, layout.soloWidth);
    ImGui::TableSetupColumn(messages::common::cWaveFile, ImGuiTableColumnFlags_WidthFixed, waveWidth);
    ImGui::TableSetupColumn(messages::stream::cTrackMix, ImGuiTableColumnFlags_WidthFixed, layout.mixWidth);
    ImGui::TableSetupColumn(messages::stream::cTrackBypass, ImGuiTableColumnFlags_WidthFixed, layout.bypassWidth);

    if (ctx.enableSend)
        ImGui::TableSetupColumn(messages::panel::cSend, ImGuiTableColumnFlags_WidthFixed, layout.sendWidth);

    if (ctx.enableFilter)
        ImGui::TableSetupColumn(messages::common::cFilter, ImGuiTableColumnFlags_WidthFixed, layout.filterWidth);
}

static void DrawTrackTableTitles(const TrackContext& ctx, const TrackTableLayout& layout)
{
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);

    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("#");

    ImGui::TableNextColumn();
    layout.muteSlots.drawLabels();

    ImGui::TableNextColumn();
    layout.soloSlots.drawLabels();

    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(cTrackWaveLabel);

    ImGui::TableNextColumn();
    layout.mixSlots.drawLabels();

    ImGui::TableNextColumn();
    layout.bypassSlots.drawLabels();

    if (ctx.enableSend)
    {
        ImGui::TableNextColumn();
        layout.sendSlots.drawLabels();
    }

    if (ctx.enableFilter)
    {
        ImGui::TableNextColumn();
        layout.filterSlots.drawLabels();
    }
}

static void DrawTrackIndexCell(const TrackContext& ctx, s32 index, f32 rowHeight, TrackEdit* edit)
{
    Sound::StreamSoundInfo::Track* track = ctx.tracks[index];

    ImGui::TableNextColumn();

    const ImVec2 pos = ImGui::GetCursorPos();
    const bool hovered = DrawTrackHandle(ctx, track, ImVec2(0.0f, rowHeight), ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap, edit);
    const bool selected = track == GetHighlightedStreamTrack();
    const f32 hoverRatio = ImGui::GetHoverRatio(ImGui::GetID("###RowFade"), hovered && !selected);

    if (selected)
    {
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32(ImGuiCol_CheckMark, uicolors::Pick(cTrackSelectedAlpha)));
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(ImGuiCol_CheckMark, uicolors::Pick(cTrackIndexAlpha)));
    }
    else if (hoverRatio > 0.0f)
    {
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32(ImGuiCol_Text, cTrackHoveredAlpha * hoverRatio));
    }

    ImGui::SetCursorPos(ImVec2(pos.x, pos.y + ImFloor((rowHeight - ImGui::GetFontSize()) * 0.5f)));
    ImGui::Text("%d", index);
}

static void DrawTrackChannelCount(u16 channelCount)
{
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();

    if (channelCount == 1)
        ImGui::TextDisabled("%s", messages::common::cMono);
    else if (channelCount == 2)
        ImGui::TextDisabled("%s", messages::common::cStereo);
    else
        ImGui::TextDisabled(messages::stream::cTrackChannelsFormat, channelCount);
}

static void DrawTrackWaveCell(const TrackContext& ctx, s32 index, const TrackTableView& view, bool audible)
{
    Sound::StreamSoundInfo::Track* track = ctx.tracks[index];
    const ImGuiStyle& style = ImGui::GetStyle();

    ImGui::TableNextColumn();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImFloor((view.rowHeight - ImGui::GetFrameHeight()) * 0.5f));

    DrawTrackWaveSelector(track, ImGui::GetContentRegionAvail().x - (view.showWaveLink ? TrailingButtonsWidth(1) : 0.0f) - (view.showChannels ? style.ItemSpacing.x + view.channelWidth : 0.0f), view.compactWave);

    Item* waveFile = track->getWaveFileRef().getItem();

    if (view.showWaveLink)
    {
        ImGui::BeginDisabled(!waveFile);

        if (TrailingButton(ICON_LC_EXTERNAL_LINK "###GoWave"))
            SelectItem(waveFile);

        ImGui::EndDisabled();

        if (!waveFile)
            ImGui::SetItemTooltip("%s", messages::reference::cNoWaveFile);
    }

    if (view.showChannels)
        DrawTrackChannelCount(track->getChannelCount(ctx.sound->getStreamSoundInfo().getStreamType()));

    if (!audible)
        DimTrackCell(false);
}

static void DrawTrackGroupCell(const TrackContext& ctx, s32 index, TrackSlots& slots, const TrackParam* params, const TrackTableView& view, bool audible)
{
    ImGui::TableNextColumn();
    DrawTrackSlotGroup(ctx, index, slots, params, view.rowHeight);

    if (!audible)
        DimTrackCell(ImGui::TableGetColumnIndex() == view.layout.columnNum - 1);
}

static void DrawTrackTableRow(const TrackContext& ctx, s32 index, TrackTableView& view, TrackEdit* edit)
{
    TrackTableLayout& layout = view.layout;
    const bool audible = ctx.sound->getStreamSoundInfo().isTrackAudible(static_cast<u32>(index));

    ImGui::PushID(ctx.tracks[index]);
    ImGui::TableNextRow();

    DrawTrackIndexCell(ctx, index, view.rowHeight, edit);

    // Mute
    ImGui::TableNextColumn();
    DrawTrackSlotGroup(ctx, index, layout.muteSlots, cTrackMuteParams, view.rowHeight);

    // Solo
    ImGui::TableNextColumn();
    DrawTrackSlotGroup(ctx, index, layout.soloSlots, cTrackSoloParams, view.rowHeight);

    DrawTrackWaveCell(ctx, index, view, audible);
    DrawTrackGroupCell(ctx, index, layout.mixSlots, cTrackMixParams, view, audible);
    DrawTrackGroupCell(ctx, index, layout.bypassSlots, cTrackMixParams + cTrackBypassIndex, view, audible);

    if (ctx.enableSend)
        DrawTrackGroupCell(ctx, index, layout.sendSlots, cTrackSendParams, view, audible);

    if (ctx.enableFilter)
        DrawTrackGroupCell(ctx, index, layout.filterSlots, cTrackFilterParams, view, audible);

    ImGui::PopID();
}

// One row for each track
void DrawTrackTable(const TrackContext& ctx, TrackEdit* edit)
{
    const f32 em = ImGui::GetFontSize();

    TrackTableView view = {};
    view.layout = CalcTrackTableLayout(ctx);
    view.rowHeight = CalcTrackKnobSize();
    view.channelWidth = ImGui::CalcTextSize(cTrackChannelWidest).x;

    const f32 availWidth = ImGui::GetContentRegionAvail().x;

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, view.layout.cellPadding);
    const bool tableOpen = ImGui::BeginTable("TrackTable", view.layout.columnNum, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY);

    if (!tableOpen)
    {
        ImGui::PopStyleVar();
        return;
    }

    bool allInView = false;
    const f32 waveWidth = CalcTrackWaveWidth(view.layout, availWidth, ImFloor(em * cTrackWaveMinWidthEm), &allInView);

    view.compactWave = waveWidth < em * (allInView ? cTrackWaveFullWidthEm : cTrackWaveWideWidthEm);
    view.showWaveLink = waveWidth >= em * cTrackWaveLinkWidthEm;
    view.showChannels = waveWidth >= em * cTrackChannelsMinEm;

    SetupTrackTableColumns(ctx, view.layout, waveWidth);
    DrawTrackTableTitles(ctx, view.layout);

    if (ctx.tracks.empty())
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(3);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", messages::stream::cTrackNone);
    }

    for (s32 i = 0; i < static_cast<s32>(ctx.tracks.size()); i++)
        DrawTrackTableRow(ctx, i, view, edit);

    DrawTrackListMenu(ctx, edit);

    ImGui::EndTable();
    ImGui::PopStyleVar();
}
