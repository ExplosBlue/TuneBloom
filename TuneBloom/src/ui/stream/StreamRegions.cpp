#include <ui/stream/StreamRegions.h>

#include <ui/widgets/ContextMenu.h>
#include <ui/file/File.h>
#include <ui/items/ItemColumns.h>
#include <ui/items/ItemList.h>
#include <ui/items/ItemSelector.h>
#include <messages/Messages.h>
#include <ui/player/PlayerViews.h>
#include <ui/widgets/RangeCell.h>
#include <ui/widgets/RowRename.h>
#include <ui/app/Shortcuts.h>
#include <ui/stream/StreamPanelFold.h>
#include <ui/stream/StreamTracks.h>
#include <ui/widgets/TabRowTools.h>
#include <ui/settings/UiColors.h>
#include <ui/wave/LoopWaveformInternal.h>
#include <ui/wave/WaveformBox.h>
#include <ui/wave/WaveFormatUI.h>
#include <ui/widgets/LaneRow.h>
#include <ui/widgets/Widgets.h>
#include <ui/app/Workspace.h>

#include <bfsar/model/Sound.h>
#include <bfsar/files/StreamRegion.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_custom.h>
#include <imgui/imgui_internal.h>

static constexpr s32 cRegionToolNum      = 2;
static constexpr f32 cRegionNameShare    = 0.35f;
static constexpr f32 cRegionNameMinEm    = 5.0f;
static constexpr f32 cRegionNameMaxEm    = 10.0f;

static const char* cRegionNumberWidest   = "000";
static const char* cRegionPositionWidest = "00:00.000";
static const char* cRegionDragType       = "REGION";
static const char* cRegionMenuId         = "RegionMenu";
static const char* cRegionListMenuId     = "RegionListMenu";

namespace
{

enum RegionColumn
{
    cRegionColumn_Action = 0,
    cRegionColumn_Number,
    cRegionColumn_Name,
    cRegionColumn_Range,
    cRegionColumn_Start,
    cRegionColumn_End,
    cRegionColumn_Length,
    cRegionColumn_Menu,
    cRegionColumn_Num
};

enum class RegionAlign
{
    Left,
    Center
};

struct RegionTable
{
    StreamSource source;
    WaveFile* wave;
    StreamFileLayout* layout;
    u32 sampleRate;
    u32 sampleCount;
    u32 lengthMin;
    s32 playingNo;
    f32 playSample;
    const char* playbackError;
    const char* editReason;
    const char* addReason;
    ImGuiID tableId;
    ImGui::LoopWaveformState* laneView;
    f32 laneLeft;
    f32 laneWidth;
};

struct RegionLaneView
{
    const WaveFile* wave = nullptr;
    u32 sampleCount = 0;
    s32 frame = -1;
    ImGui::LoopWaveformState state;
};

struct RegionEdit
{
    bool add = false;
    bool import = false;
    bool invalidName = false;
    bool clearSelection = false;
    s32 insertAfter = -1;
    s32 deleteNo = -1;
    s32 moveFrom = -1;
    s32 moveTo = -1;
};

}

static RegionLaneView* sRegionLaneView = nullptr;

static ImGui::LoopWaveformState* GetRegionLaneView(const WaveFile* wave, u32 sampleCount)
{
    if (!sRegionLaneView)
        sRegionLaneView = new RegionLaneView();

    RegionLaneView& view = *sRegionLaneView;

    if (view.wave != wave || view.sampleCount != sampleCount)
    {
        view.state = ImGui::LoopWaveformState{};
        view.wave = wave;
        view.sampleCount = sampleCount;
    }

    ImGui::InitViewIfNeeded(view.state, sampleCount);

    if (view.frame != ImGui::GetFrameCount())
    {
        view.frame = ImGui::GetFrameCount();
        ImGui::AdvanceViewAnimation(view.state, sampleCount);
    }

    return &view.state;
}

static f32 CalcRegionEdgeInset()
{
    const ImGuiWindow* window = ImGui::GetCurrentTable()->InnerWindow;

    return window && !window->ScrollbarY ? ImGui::GetStyle().ScrollbarSize : 0.0f;
}

static f32 CalcRegionFreeWidth(f32 fixedColumnsWidth)
{
    const ImGuiTable* table = ImGui::GetCurrentTable();
    const f32 spacing = table->OuterPaddingX * 2.0f + (table->CellSpacingX1 + table->CellSpacingX2) * (cRegionColumn_Num - 1) + table->CellPaddingX * 2.0f * cRegionColumn_Num;

    return sead::Mathf::max(2.0f, ImFloor(table->WorkRect.GetWidth() - spacing - fixedColumnsWidth));
}

static void SetupRegionColumns(f32 edgeInset)
{
    const f32 em = ImGui::GetFontSize();
    const f32 positionWidth = ImGui::CalcTextSize(cRegionPositionWidest).x;
    const f32 fieldWidth = positionWidth + ImGui::GetStyle().FramePadding.x * 2.0f;
    const f32 numberWidth = CalcLaneIndexWidth(cRegionNumberWidest);
    const f32 menuWidth = ImCeil(ImGui::GetFrameHeight()) + edgeInset;
    const f32 freeWidth = CalcRegionFreeWidth(GetItemActionColumnWidth() + numberWidth + fieldWidth * 2.0f + positionWidth + menuWidth);
    const f32 nameWidth = ImFloor(sead::Mathf::clamp2(sead::Mathf::min(em * cRegionNameMinEm, freeWidth * 0.5f), freeWidth * cRegionNameShare, em * cRegionNameMaxEm));

    ImGui::TableSetupScrollFreeze(0, 1);
    SetupItemActionColumn();
    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, numberWidth);
    ImGui::TableSetupColumn(messages::common::cName, ImGuiTableColumnFlags_WidthFixed, nameWidth);
    ImGui::TableSetupColumn(messages::common::cRange, ImGuiTableColumnFlags_WidthFixed, freeWidth - nameWidth);

    ImGui::TableSetupColumn(messages::common::cStart, ImGuiTableColumnFlags_WidthFixed, fieldWidth);
    ImGui::TableSetupColumn(messages::common::cEnd, ImGuiTableColumnFlags_WidthFixed, fieldWidth);
    ImGui::TableSetupColumn(messages::common::cLength, ImGuiTableColumnFlags_WidthFixed, positionWidth);
    ImGui::TableSetupColumn("###Menu", ImGuiTableColumnFlags_WidthFixed, menuWidth);
}

static void DrawRegionCellText(const char* text, const ImVec4& color, RegionAlign align)
{
    const f32 free = sead::Mathf::max(0.0f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(text).x);

    if (align == RegionAlign::Center)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImFloor(free * 0.5f));

    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(color, "%s", text);
}

static void DrawRegionRuler(RegionTable& view)
{
    if (!view.laneView || view.layout->regions.empty())
    {
        DrawLaneRuler(0, view.sampleRate);
        return;
    }

    const bool showLoop = view.wave->hasValidLoop();
    const LaneSeekRuler ruler = DrawLaneSeekRuler(*view.laneView, view.sampleCount, view.sampleRate, view.playSample, showLoop, view.wave->getOriginalLoopStartFrame(), view.wave->getOriginalLoopEndFrame());

    view.laneLeft = ruler.laneLeft;
    view.laneWidth = ruler.laneWidth;

    SeekPlayerToSample(ruler.seekSample);
}

static void DrawRegionTitles(RegionTable& view)
{
    static constexpr RegionAlign cTitleAligns[cRegionColumn_Num] = { RegionAlign::Left, RegionAlign::Center, RegionAlign::Left, RegionAlign::Left, RegionAlign::Center, RegionAlign::Center, RegionAlign::Center, RegionAlign::Left };

    const ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);

    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);

    for (s32 column = cRegionColumn_Number; column < cRegionColumn_Menu; column++)
    {
        if (!ImGui::TableSetColumnIndex(column))
            continue;

        if (column == cRegionColumn_Range)
            DrawRegionRuler(view);
        else
            DrawRegionCellText(ImGui::TableGetColumnName(column), color, cTitleAligns[column]);
    }
}

static void DrawRegionField(const RegionTable& view, s32 regionNo, s32 column, const char* id, bool isStart)
{
    if (!ImGui::TableSetColumnIndex(column))
        return;

    const StreamRegion& region = view.layout->regions[regionNo];

    u32 value = isStart ? region.start : region.end;
    const u32 min = isStart ? 0 : region.start + 1;
    const u32 max = isStart ? region.end - 1 : view.sampleCount;

    const bool changed = DrawCellTimeField(id, &value, min, max, view.sampleCount, view.sampleRate, view.editReason != nullptr);
    const bool commit = IsItemEditCommitted(changed);

    if (changed)
        SetStreamRegionRange(view.wave, regionNo, isStart ? value : region.start, isStart ? region.end : value);

    if (commit)
        CommitStreamRegionRange(view.wave, regionNo);
}

static void DrawRegionLength(const RegionTable& view, const StreamRegion& region)
{
    if (!ImGui::TableSetColumnIndex(cRegionColumn_Length))
        return;

    const u32 length = region.end - region.start;
    const bool isShort = length < view.lengthMin;

    sead::FixedSafeString<24> time;
    FormatSampleTime(&time, length, view.sampleRate);

    DrawRegionCellText(time.cstr(), isShort ? uicolors::Warning() : GetItemValueColor(), RegionAlign::Center);

    if (isShort)
        ImGui::SetItemTooltip(messages::stream::cRegionShortTipFormat, view.lengthMin);
    else
        ImGui::SetItemTooltip(messages::wavefile::cSamplesFormat, length);
}

static void ClickRegionRow(const RegionTable& view, s32 regionNo, bool selected)
{
    if (selected && ImGui::GetIO().KeyCtrl)
        SelectStreamRegion(view.wave, -1);
    else
        SelectStreamRegion(view.wave, regionNo);
}

static void DrawRegionLane(const RegionTable& view, s32 regionNo, bool selected, f32 rowHeight)
{
    if (!ImGui::TableSetColumnIndex(cRegionColumn_Range))
        return;

    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max = min + ImVec2(ImGui::GetContentRegionAvail().x, rowHeight);
    const ImVec2 pad = CalcLanePad();

    DrawLaneFill(ImGui::GetWindowDrawList(), min, max, selected);

    if (!view.laneView)
        return;

    const RegionLaneClick click = DrawStreamRegionRowLane(view.wave, *view.laneView, regionNo, min, max, view.editReason == nullptr);

    if (click == RegionLaneClick::Select)
        ClickRegionRow(view, regionNo, selected);
    else if (click == RegionLaneClick::Menu)
        ImGui::OpenPopup(cRegionMenuId);

    DrawLaneRowPlayhead(min.x + pad.x, max.x - pad.x, rowHeight, CalcLaneViewRatio(*view.laneView, view.playSample));
}

static void DrawRegionName(const RegionTable& view, s32 regionNo, ImGuiID rowId, bool rowHovered, RegionEdit* edit)
{
    if (!ImGui::TableSetColumnIndex(cRegionColumn_Name))
        return;

    StreamRegion& region = view.layout->regions[regionNo];

    ImGui::AlignTextToFramePadding();

    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const f32 top = cursor.y + ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset;
    const f32 cellRight = cursor.x + ImGui::GetContentRegionAvail().x;

    if (GetRenamedRow(view.tableId) == &region)
    {
        sead::FixedSafeString<256> name;

        if (DrawRowRenameFieldAt(cursor, cellRight - cursor.x, &name) && !RenameStreamRegion(view.wave, regionNo, name))
            edit->invalidName = true;

        return;
    }

    DrawSelectorName(rowId, region.name.cstr(), ImVec2(cursor.x, top), ImVec2(cellRight, top + ImGui::GetFontSize()), rowHovered);

    const f32 mouseX = ImGui::GetMousePos().x;
    const bool onName = mouseX >= cursor.x && mouseX < cellRight;

    if (rowHovered && onName && !view.editReason && view.layout->canStoreRegionNames() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        BeginRowRename(&region, view.tableId, region.name.cstr());
}

static void DrawRegionRowMenu(const RegionTable& view, s32 regionNo, RegionEdit* edit)
{
    PlaceLaneRowMenu(cRegionMenuId);

    if (!BeginItemContextMenu(cRegionMenuId))
        return;

    StreamRegion& region = view.layout->regions[regionNo];
    const char* renameReason = view.editReason ? view.editReason : messages::stream::cRegionNamesUnsupported;

    SelectStreamRegion(view.wave, regionNo);

    if (ContextMenuItem(ICON_LC_LIST_PLUS, messages::menu::cInsertAfter, !view.addReason, view.addReason))
    {
        edit->add = true;
        edit->insertAfter = regionNo;
    }

    if (ContextMenuItem(ICON_LC_TEXT_CURSOR_INPUT, messages::common::cRename, !view.editReason && view.layout->canStoreRegionNames(), renameReason))
        BeginRowRename(&region, view.tableId, region.name.cstr());

    ContextMenuSeparator();

    ImGui::BeginDisabled(view.editReason != nullptr);

    if (ContextMenuDangerItem(ICON_LC_TRASH_2, messages::menu::cDeleteRegion))
        edit->deleteNo = regionNo;

    ImGui::EndDisabled();

    EndContextMenu();
}

static void DrawRegionRowDragDrop(const RegionTable& view, s32 regionNo, RegionEdit* edit)
{
    if (view.editReason)
        return;

    if (ImGui::BeginDragDropSource())
    {
        ImGui::SetDragDropPayload(cRegionDragType, &regionNo, sizeof(regionNo));
        ImGui::Text(messages::stream::cRegionMovingFormat, regionNo);
        ImGui::EndDragDropSource();
    }

    if (!ImGui::BeginDragDropTarget())
        return;

    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(cRegionDragType))
    {
        edit->moveFrom = *static_cast<const s32*>(payload->Data);
        edit->moveTo = regionNo;
    }

    ImGui::EndDragDropTarget();
}

static void DrawRegionPlayButton(const RegionTable& view, s32 regionNo)
{
    BeginItemActionCell();

    if (DrawRowPlayButton(static_cast<u32>(regionNo), view.playbackError, regionNo == view.playingNo))
        sSoundPlayer.playStreamRegion(view.source, regionNo);

    EndItemActionCell();
}

static void DrawRegionRow(const RegionTable& view, s32 regionNo, RegionEdit* edit)
{
    const StreamRegion& region = view.layout->regions[regionNo];
    const bool selected = GetSelectedStreamRegion(view.wave) == regionNo;
    const f32 rowHeight = ImGui::GetFrameHeight();

    ImGui::PushID(regionNo);
    ImGui::TableNextRow();

    if (ImGui::TableSetColumnIndex(cRegionColumn_Action))
        DrawRegionPlayButton(view, regionNo);

    ImGui::TableSetColumnIndex(cRegionColumn_Number);

    const ImVec2 numberMin = ImGui::GetCursorScreenPos();
    const f32 numberWidth = ImGui::GetContentRegionAvail().x;

    bool rowHovered = false;

    if (DrawLaneRowSelectable("###Row", selected, regionNo == view.playingNo, rowHeight, &rowHovered))
        ClickRegionRow(view, regionNo, selected);

    const ImGuiID rowId = ImGui::GetItemID();

    DrawRegionRowMenu(view, regionNo, edit);
    DrawRegionRowDragDrop(view, regionNo, edit);

    DrawLaneIndexBadge(regionNo, numberMin, ImVec2(numberWidth, rowHeight), selected);

    DrawRegionName(view, regionNo, rowId, rowHovered, edit);
    DrawRegionLane(view, regionNo, selected, rowHeight);
    DrawRegionField(view, regionNo, cRegionColumn_Start, "##Start", true);
    DrawRegionField(view, regionNo, cRegionColumn_End, "##End", false);
    DrawRegionLength(view, region);

    if (ImGui::TableSetColumnIndex(cRegionColumn_Menu))
        DrawLaneRowMenuButton(cRegionMenuId, rowHovered, messages::stream::cRegionMenuTip);

    ImGui::PopID();
}

static void DrawRegionListMenu(const RegionTable& view, RegionEdit* edit)
{
    if (!BeginWindowContextMenu(cRegionListMenuId))
        return;

    if (ContextMenuItem(ICON_LC_PLUS, messages::common::cAdd, !view.addReason, view.addReason))
        edit->add = true;

    if (ContextMenuItem(ICON_LC_IMPORT, messages::stream::cRegionImport, !view.editReason, view.editReason))
        edit->import = true;

    EndContextMenu();
}

static bool IsRegionEmptyClick(bool inEmptyArea)
{
    static s32 sFrame = -1;
    static bool sPopupWasOpen = false;
    static bool sClick = false;

    if (sFrame != ImGui::GetFrameCount())
    {
        sFrame = ImGui::GetFrameCount();
        sClick = !sPopupWasOpen && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        sPopupWasOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    }

    return sClick && inEmptyArea && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered();
}

static void HandleBelowRegionTable(const ImRect& tableRect, ImGuiID listMenuId, RegionEdit* edit)
{
    const ImVec2 mouse = ImGui::GetMousePos();
    const bool below = mouse.y >= tableRect.Max.y && mouse.x >= tableRect.Min.x && mouse.x < tableRect.Max.x;

    edit->clearSelection |= IsRegionEmptyClick(below);

    if (below && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        ImGui::OpenPopupEx(listMenuId);
}

static f32 CalcRegionTableFitHeight(s32 regionCount)
{
    const f32 rowHeight = ImGui::GetFrameHeight() + ImGui::GetStyle().CellPadding.y * 2.0f;

    return rowHeight * static_cast<f32>(regionCount + 1) + 1.0f;
}

static f32 CalcRegionTableHeight(s32 regionCount)
{
    const f32 freeHeight = ImGui::GetContentRegionAvail().y - CalcLaneZoomBarHeight();

    return sead::Mathf::max(CalcRegionTableFitHeight(1), sead::Mathf::min(CalcRegionTableFitHeight(regionCount), freeHeight));
}

static void PlaceRegionsSectionAtBottom(const RegionTable& view, f32 titleHeight)
{
    if (!view.laneView || view.layout->regions.empty() || titleHeight <= 0.0f)
        return;

    const f32 sectionHeight = titleHeight + CalcRegionTableFitHeight(static_cast<s32>(view.layout->regions.size())) + CalcLaneZoomBarHeight();
    const f32 freeHeight = ImGui::GetContentRegionAvail().y - sectionHeight;

    if (freeHeight > 0.0f)
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImFloor(freeHeight));
}

static void DrawRegionTable(RegionTable& view, RegionEdit* edit)
{
    const ImGuiTableFlags flags = ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_SizingFixedFit;
    const bool hasLanes = view.laneView && !view.layout->regions.empty();
    const f32 height = hasLanes ? CalcRegionTableHeight(static_cast<s32>(view.layout->regions.size())) : 0.0f;

    if (!ImGui::BeginTable("RegionTable", cRegionColumn_Num, flags, ImVec2(0.0f, height)))
        return;

    SetupRegionColumns(CalcRegionEdgeInset());
    DrawRegionTitles(view);

    const s32 regionCount = view.layout ? static_cast<s32>(view.layout->regions.size()) : 0;

    if (regionCount == 0)
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(cRegionColumn_Name);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", view.source.isValid() ? messages::stream::cRegionNone : messages::stream::cTrackNoSound);
    }

    for (s32 regionNo = 0; regionNo < regionCount; regionNo++)
        DrawRegionRow(view, regionNo, edit);

    edit->clearSelection = IsRegionEmptyClick(ImGui::TableGetHoveredRow() != 0);
    NoteStreamRegionSelectionShown();

    const ImGuiID listMenuId = ImGui::GetID(cRegionListMenuId);

    DrawRegionListMenu(view, edit);

    ImGui::EndTable();

    const ImRect tableRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

    if (hasLanes && view.laneWidth > 0.0f)
        DrawLaneZoomBar(*view.laneView, view.laneLeft, view.laneWidth, view.sampleCount);

    HandleBelowRegionTable(tableRect, listMenuId, edit);
}

static void DrawRegionTools(const RegionTable& view, bool inTabBar, RegionEdit* edit)
{
    if (DrawTabRowCommand(ICON_LC_PLUS "###AddRegion", messages::stream::cRegionAddTip, view.addReason, inTabBar))
        edit->add = true;

    if (DrawTabRowCommand(ICON_LC_IMPORT "###ImportRegions", messages::stream::cRegionImport, view.editReason, inTabBar))
        edit->import = true;

    if (!inTabBar)
        ImGui::NewLine();
}

static bool DrawRegionTabRow(const RegionTable& view, RegionEdit* edit, bool* toggleFold)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImGuiDockNode* node = window->DockNode;

    const bool canFold = CanFoldStreamPanel();

    if (!node || node->VisibleWindow != window || !BeginTabRowTools(node, CalcTabRowToolsWidth(canFold ? cRegionToolNum + 1 : cRegionToolNum)))
        return false;

    DrawRegionTools(view, true, edit);

    if (canFold)
        *toggleFold = DrawStreamPanelFoldTool();

    EndTabRowTools();

    return true;
}

static void ApplyRegionEdit(const RegionTable& view, const RegionEdit& edit)
{
    if (!view.wave)
        return;

    const s32 selectedNo = GetSelectedStreamRegion(view.wave);
    const bool keysOn = selectedNo != -1 && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput;
    const bool deleteKey = keysOn && !view.editReason && shortcuts::Pressed(shortcuts::Action::DeleteSelection);

    if (edit.clearSelection || (keysOn && shortcuts::Pressed(shortcuts::Action::ClearSelection)))
        SelectStreamRegion(view.wave, -1);

    if (edit.add)
        AddStreamRegion(view.wave, edit.insertAfter != -1 ? edit.insertAfter : selectedNo);

    if (edit.deleteNo != -1)
        DeleteStreamRegion(view.wave, edit.deleteNo);
    else if (deleteKey)
        DeleteStreamRegion(view.wave, selectedNo);

    if (edit.moveFrom != -1)
        MoveStreamRegion(view.wave, edit.moveFrom, edit.moveTo);

    if (edit.import)
        RequestStreamRegionImport(view.wave);

    if (edit.invalidName)
        ImGui::OpenPopup("###RegionNameInvalid");
}

static const char* FindRegionEditReason(const RegionTable& view, sead::BufferedSafeString* text)
{
    if (!view.source.hasRegionSupport())
    {
        text->format(messages::stream::cRegionTypeUnsupportedFormat, GetInnerFileDisplayName(ArchiveFormat::BFSAR, InnerFileKind::Stream));
        return text->cstr();
    }

    if (!view.source.isValid())
        return messages::stream::cTrackNoSound;

    return view.wave ? nullptr : messages::stream::cRegionNoWave;
}

static void SetupRegionTable(RegionTable* view, sead::BufferedSafeString* editReason, sead::BufferedSafeString* addReason, sead::BufferedSafeString* playbackError)
{
    view->source = GetActiveStreamSource();
    view->wave = view->source.isValid() ? view->source.stream->getMainWave() : nullptr;
    view->playingNo = -1;
    view->playSample = -1.0f;
    view->tableId = ImGui::GetID("RegionTable");
    view->editReason = FindRegionEditReason(*view, editReason);
    view->addReason = view->editReason;

    if (view->source.isValid() && SoundPlayer::GetStreamPlaybackError(view->source, playbackError))
        view->playbackError = playbackError->cstr();

    if (!view->wave)
        return;

    view->layout = &view->wave->getStreamFileLayout();
    view->sampleRate = view->wave->getSampleRate();
    view->sampleCount = view->wave->getSampleCount();
    view->lengthMin = StreamFileLayout::GetRegionLengthMin(view->wave->getEncoding() == WaveFile::Encoding::DspAdpcm, view->wave->getEncoding() == WaveFile::Encoding::Pcm8);

    const f32 playRatio = CalcStreamPlayRatio(view->source, view->sampleCount);

    view->playSample = playRatio >= 0.0f ? playRatio * static_cast<f32>(view->sampleCount) : -1.0f;
    view->laneView = view->sampleCount != 0 ? GetRegionLaneView(view->wave, view->sampleCount) : nullptr;

    view->playingNo = GetPlayingStreamRegion(view->wave);

    if (!view->addReason && !view->layout->canAddRegion())
    {
        addReason->format(messages::stream::cRegionLimitFormat, StreamFileLayout::cRegionCountMax);
        view->addReason = addReason->cstr();
    }
}

static void DockBesideTracksAtFirstUse()
{
    const ImGuiWindow* tracksWindow = ImGui::FindWindowByName("###SubInfoWindow");

    if (tracksWindow && tracksWindow->DockId != 0)
        ImGui::SetNextWindowDockID(tracksWindow->DockId, ImGuiCond_FirstUseEver);
}

static s32 GetRegionNum(const StreamSource& source)
{
    const StreamFileLayout* layout = source.isValid() && source.hasRegionSupport() ? source.stream->getFileLayout() : nullptr;

    return layout ? static_cast<s32>(layout->regions.size()) : -1;
}

static void DrawRegionPopups()
{
    WarningPopup("###RegionNameInvalid", messages::stream::cRegionNameInvalidDetail);
    DrawStreamRegionImportDialog();
}

void DrawStreamRegionsWindow()
{
    if (!IsStreamPanelShown())
        return;

    DockBesideTracksAtFirstUse();

    const TabRowTitle title = { ICON_LC_FLAG, messages::common::cRegions, "###StreamRegionsWindow", 2 };
    const s32 regionNum = GetRegionNum(GetActiveStreamSource());

    sead::FixedSafeString<64> name;
    FormatTabRowTitle(&name, title, regionNum);

    const bool open = ImGui::Begin(name.cstr(), nullptr, ImGuiWindowFlags_NoFocusOnAppearing);

    DrawTabRowCount(title, regionNum);

    sead::FixedSafeString<SoundPlayer::cPlaybackErrorLength> playbackError;
    sead::FixedSafeString<96> editReason;
    sead::FixedSafeString<96> addReason;

    RegionTable view = {};
    SetupRegionTable(&view, &editReason, &addReason, &playbackError);

    SetTabRowDisabled(view.source.hasRegionSupport() ? nullptr : view.editReason);

    RegionEdit edit;
    bool toggleFold = false;
    const bool toolsInTabRow = DrawRegionTabRow(view, &edit, &toggleFold);
    const bool folded = UpdateStreamPanelFold(toggleFold);

    if (open && !folded)
    {
        if (!toolsInTabRow)
            DrawRegionTools(view, false, &edit);

        DrawRegionTable(view, &edit);
    }

    ApplyRegionEdit(view, edit);
    DrawRegionPopups();

    ImGui::End();
}

void DrawStreamRegionsSection()
{
    sead::FixedSafeString<SoundPlayer::cPlaybackErrorLength> playbackError;
    sead::FixedSafeString<96> editReason;
    sead::FixedSafeString<96> addReason;

    RegionTable view = {};
    SetupRegionTable(&view, &editReason, &addReason, &playbackError);

    RegionEdit edit;

    static f32 sTitleHeight = 0.0f;

    PlaceRegionsSectionAtBottom(view, sTitleHeight > 0.0f ? sTitleHeight : ImGui::GetFrameHeightWithSpacing());

    const f32 titleTop = ImGui::GetCursorPosY();

    BeginTitleRowTools(messages::common::cRegions, CalcTabRowToolsWidth(cRegionToolNum));
    DrawRegionTools(view, false, &edit);

    sTitleHeight = ImGui::GetCursorPosY() - titleTop;

    DrawRegionTable(view, &edit);

    ApplyRegionEdit(view, edit);
    DrawRegionPopups();
}
