#include <ui/stream/StreamRegions.h>

#include <ui/widgets/ContextMenu.h>
#include <ui/file/File.h>
#include <ui/items/ItemColumns.h>
#include <ui/items/ItemList.h>
#include <ui/items/ItemSelector.h>
#include <ui/widgets/Knob.h>
#include <ui/Messages.h>
#include <ui/player/PlayerViews.h>
#include <ui/widgets/RowRename.h>
#include <ui/app/Shortcuts.h>
#include <ui/stream/StreamPanelFold.h>
#include <ui/widgets/TabRowTools.h>
#include <ui/settings/Theme.h>
#include <ui/wave/WaveFormatUI.h>
#include <ui/widgets/Widgets.h>
#include <ui/app/Workspace.h>

#include <bfsar/model/Sound.h>
#include <bfsar/files/StreamRegion.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_custom.h>
#include <imgui/imgui_internal.h>

static constexpr f32 cRegionPlayingAlpha = 0.20f;
static constexpr f32 cRegionNextGapEm    = 0.5f;
static constexpr f32 cRegionFieldPadY    = 2.0f;
static constexpr f32 cRegionDragRange    = 2000.0f;
static constexpr s32 cRegionToolNum      = 2;
static constexpr f32 cRegionNameWeight   = 1.0f;
static constexpr f32 cRegionRangeWeight  = 1.5f;
static constexpr f32 cRegionRangeHeight  = 12.0f;
static constexpr f32 cRegionRangeLine    = 2.0f;
static constexpr f32 cRegionRangeAlpha   = 0.35f;
static constexpr f32 cRegionRangeTableEm = 34.0f;

static const char* cRegionNumberWidest   = "000";
static const char* cRegionPositionWidest = "00:00.000";
static const char* cRegionDragType       = "REGION";

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
    cRegionColumn_Num
};

enum class RegionAlign
{
    Left,
    Center,
    Right
};

struct RegionTable
{
    Sound* sound;
    WaveFile* wave;
    StreamFileLayout* layout;
    u32 sampleRate;
    u32 sampleCount;
    u32 lengthMin;
    s32 playingNo;
    s32 nextNo;
    const char* playbackError;
    const char* editReason;
    const char* addReason;
    f32 edgeInset;
    ImGuiID tableId;
};

struct RegionEdit
{
    bool add = false;
    bool import = false;
    bool invalidName = false;
    s32 insertAfter = -1;
    s32 deleteNo = -1;
    s32 moveFrom = -1;
    s32 moveTo = -1;
};

}

static f32 CalcRegionEdgeInset()
{
    const ImGuiWindow* window = ImGui::GetCurrentTable()->InnerWindow;

    return window && !window->ScrollbarY ? ImGui::GetStyle().ScrollbarSize : 0.0f;
}

static void SetupRegionColumns(f32 edgeInset)
{
    const f32 positionWidth = ImGui::CalcTextSize(cRegionPositionWidest).x;
    const f32 fieldWidth = positionWidth + ImGui::GetStyle().FramePadding.x * 2.0f;
    const bool showRange = ImGui::GetCurrentTable()->OuterRect.GetWidth() >= ImGui::GetFontSize() * cRegionRangeTableEm;

    ImGui::TableSetupScrollFreeze(0, 1);
    SetupItemActionColumn();
    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize(cRegionNumberWidest).x);
    ImGui::TableSetupColumn(messages::common::cName, ImGuiTableColumnFlags_WidthStretch, cRegionNameWeight);
    ImGui::TableSetupColumn(messages::stream::cRegionRange, ImGuiTableColumnFlags_WidthStretch | (showRange ? 0 : ImGuiTableColumnFlags_Disabled), cRegionRangeWeight);
    ImGui::TableSetupColumn(messages::common::cStart, ImGuiTableColumnFlags_WidthFixed, fieldWidth);
    ImGui::TableSetupColumn(messages::common::cEnd, ImGuiTableColumnFlags_WidthFixed, fieldWidth);
    ImGui::TableSetupColumn(messages::common::cLength, ImGuiTableColumnFlags_WidthFixed, positionWidth + edgeInset);
}

static void DrawRegionCellText(const char* text, const ImVec4& color, RegionAlign align, f32 rightInset = 0.0f)
{
    const f32 free = sead::Mathf::max(0.0f, ImGui::GetContentRegionAvail().x - rightInset - ImGui::CalcTextSize(text).x);

    if (align == RegionAlign::Right)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + free);
    else if (align == RegionAlign::Center)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImFloor(free * 0.5f));

    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(color, "%s", text);
}

static void DrawRegionTitles(f32 edgeInset)
{
    static constexpr RegionAlign cTitleAligns[cRegionColumn_Num] = { RegionAlign::Left, RegionAlign::Left, RegionAlign::Left, RegionAlign::Left, RegionAlign::Center, RegionAlign::Center, RegionAlign::Right };

    const ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_Text);

    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);

    for (s32 column = cRegionColumn_Number; column < cRegionColumn_Num; column++)
    {
        if (ImGui::TableSetColumnIndex(column))
            DrawRegionCellText(ImGui::TableGetColumnName(column), color, cTitleAligns[column], column == cRegionColumn_Length ? edgeInset : 0.0f);
    }
}

static void DrawRegionField(const RegionTable& view, s32 regionNo, s32 column, const char* id, bool isStart)
{
    if (!ImGui::TableSetColumnIndex(column))
        return;

    const StreamRegion& region = view.layout->regions[regionNo];
    const ImGuiStyle& style = ImGui::GetStyle();

    u32 value = isStart ? region.start : region.end;
    u32 min = isStart ? 0 : region.start + 1;
    u32 max = isStart ? region.end - 1 : view.sampleCount;

    sead::FixedSafeString<24> time;
    FormatSampleTime(&time, value, view.sampleRate);

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + style.FramePadding.y - cRegionFieldPadY);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, cRegionFieldPadY));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::BeginDisabled(view.editReason != nullptr);

    SetNextKnobText(time.cstr());

    const bool changed = DragField(id, ImGuiDataType_U32, &value, sead::Mathf::max(1.0f, static_cast<f32>(view.sampleCount) / cRegionDragRange), &min, &max);
    const bool commit = ImGui::IsItemDeactivatedAfterEdit() || (changed && !ImGui::IsItemActive());

    ImGui::EndDisabled();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    KnobTooltip(messages::wavefile::cSamplesFormat, value);

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

    DrawRegionCellText(time.cstr(), isShort ? cWarningTextColor : GetItemValueColor(), RegionAlign::Right, view.edgeInset);

    if (isShort)
        ImGui::SetItemTooltip(messages::stream::cRegionShortTipFormat, view.lengthMin);
    else
        ImGui::SetItemTooltip(messages::wavefile::cSamplesFormat, length);
}

static void DrawRegionRange(const RegionTable& view, s32 regionNo)
{
    if (!ImGui::TableSetColumnIndex(cRegionColumn_Range))
        return;

    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const f32 right = cursor.x + ImGui::GetContentRegionAvail().x;
    const f32 centerY = ImFloor(cursor.y + ImGui::GetFrameHeight() * 0.5f);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    draw->AddRectFilled(ImVec2(cursor.x, centerY - cRegionRangeLine * 0.5f), ImVec2(right, centerY + cRegionRangeLine * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled, cRegionRangeAlpha), cRegionRangeLine * 0.5f);
    DrawStreamRegionMarks(draw, ImVec2(cursor.x, centerY - cRegionRangeHeight * 0.5f), ImVec2(right, centerY + cRegionRangeHeight * 0.5f), view.wave, regionNo);
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
    f32 right = cellRight;

    if (GetRenamedRow(view.tableId) == &region)
    {
        const f32 framePadding = ImGui::GetStyle().FramePadding.x;
        sead::FixedSafeString<256> name;

        ImGui::SetCursorScreenPos(ImVec2(cursor.x - framePadding, cursor.y));
        ImGui::SetNextItemWidth(cellRight - cursor.x + framePadding);

        if (DrawRowRenameField("", &name) && !RenameStreamRegion(view.wave, regionNo, name))
            edit->invalidName = true;

        return;
    }

    if (regionNo == view.nextNo)
    {
        const f32 nextWidth = ImGui::CalcTextSize(messages::stream::cRegionNext).x;

        ImGui::GetWindowDrawList()->AddText(ImVec2(right - nextWidth, top), ImGui::GetColorU32(GetItemValueColor()), messages::stream::cRegionNext);
        right -= nextWidth + ImFloor(ImGui::GetFontSize() * cRegionNextGapEm);
    }

    DrawSelectorName(rowId, region.name.cstr(), ImVec2(cursor.x, top), ImVec2(right, top + ImGui::GetFontSize()), rowHovered);

    const f32 mouseX = ImGui::GetMousePos().x;
    const bool onName = mouseX >= cursor.x && mouseX < cellRight;

    if (rowHovered && onName && !view.editReason && view.layout->canStoreRegionNames() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        BeginRowRename(&region, view.tableId, region.name.cstr());
}

static void DrawRegionRowMenu(const RegionTable& view, s32 regionNo, RegionEdit* edit)
{
    if (!BeginItemContextMenu("RegionMenu"))
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

static void DrawRegionRow(const RegionTable& view, s32 regionNo, RegionEdit* edit)
{
    const StreamRegion& region = view.layout->regions[regionNo];
    const bool selected = GetSelectedStreamRegion(view.wave) == regionNo;

    ImGui::PushID(regionNo);
    ImGui::TableNextRow();

    if (ImGui::TableSetColumnIndex(cRegionColumn_Action))
    {
        BeginItemActionCell();

        if (DrawRowPlayButton(static_cast<u32>(regionNo), view.playbackError, regionNo == view.playingNo))
            sSoundPlayer.playStreamRegion(view.sound, regionNo);

        EndItemActionCell();
    }

    ImGui::TableSetColumnIndex(cRegionColumn_Number);
    ImGui::AlignTextToFramePadding();

    bool rowHovered = false;

    ImGui::SetNextItemAllowOverlap();

    if (DrawTableRowSelectable("###Row", selected, &rowHovered))
        ClickStreamRegion(view.wave, regionNo);

    const ImGuiID rowId = ImGui::GetItemID();

    DrawRegionRowMenu(view, regionNo, edit);
    DrawRegionRowDragDrop(view, regionNo, edit);

    if (regionNo == view.playingNo)
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32(ImGuiCol_CheckMark, cRegionPlayingAlpha));

    ImGui::SameLine(0.0f, 0.0f);
    DrawRegionCellText(sead::FormatFixedSafeString<16>("%d", regionNo).cstr(), GetItemValueColor(), RegionAlign::Left);

    DrawRegionName(view, regionNo, rowId, rowHovered, edit);
    DrawRegionRange(view, regionNo);
    DrawRegionField(view, regionNo, cRegionColumn_Start, "##Start", true);
    DrawRegionField(view, regionNo, cRegionColumn_End, "##End", false);
    DrawRegionLength(view, region);

    ImGui::PopID();
}

static void DrawRegionListMenu(const RegionTable& view, RegionEdit* edit)
{
    if (!BeginWindowContextMenu("RegionListMenu"))
        return;

    if (ContextMenuItem(ICON_LC_PLUS, messages::common::cAdd, !view.addReason, view.addReason))
        edit->add = true;

    if (ContextMenuItem(ICON_LC_IMPORT, messages::stream::cRegionImport, !view.editReason, view.editReason))
        edit->import = true;

    EndContextMenu();
}

static void DrawRegionTable(RegionTable& view, RegionEdit* edit)
{
    const ImGuiTableFlags flags = ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
                                  ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_SizingFixedFit;

    if (!ImGui::BeginTable("RegionTable", cRegionColumn_Num, flags))
        return;

    view.edgeInset = CalcRegionEdgeInset();

    SetupRegionColumns(view.edgeInset);
    DrawRegionTitles(view.edgeInset);

    const s32 regionCount = view.layout ? static_cast<s32>(view.layout->regions.size()) : 0;

    if (regionCount == 0)
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(cRegionColumn_Name);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", view.sound ? messages::stream::cRegionNone : messages::stream::cTrackNoSound);
    }

    for (s32 regionNo = 0; regionNo < regionCount; regionNo++)
        DrawRegionRow(view, regionNo, edit);

    DrawRegionListMenu(view, edit);

    ImGui::EndTable();
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
    const bool deleteKey = !view.editReason && selectedNo != -1 && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput && shortcuts::Pressed(shortcuts::Action::DeleteSelection);

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

static bool HasRegionSupport(const Sound* sound)
{
    if (sBfsar.getFormat() != ArchiveFormat::BFSAR)
        return false;

    return !sound || sound->getStreamSoundInfo().getStreamType() == Sound::StreamSoundInfo::StreamType::NwStreamBinary;
}

static const char* FindRegionEditReason(const RegionTable& view, sead::BufferedSafeString* text)
{
    if (!HasRegionSupport(view.sound))
    {
        text->format(messages::stream::cRegionTypeUnsupportedFormat, GetInnerFileDisplayName(ArchiveFormat::BFSAR, InnerFileKind::Stream));
        return text->cstr();
    }

    if (!view.sound)
        return messages::stream::cTrackNoSound;

    return view.wave ? nullptr : messages::stream::cRegionNoWave;
}

static void SetupRegionTable(RegionTable* view, sead::BufferedSafeString* editReason, sead::BufferedSafeString* addReason, sead::BufferedSafeString* playbackError)
{
    view->sound = GetSelectedStreamSound();
    view->wave = view->sound ? view->sound->getStreamSoundInfo().getMainWave() : nullptr;
    view->playingNo = -1;
    view->nextNo = -1;
    view->tableId = ImGui::GetID("RegionTable");
    view->editReason = FindRegionEditReason(*view, editReason);
    view->addReason = view->editReason;

    if (view->sound && SoundPlayer::GetSoundPlaybackError(*view->sound, playbackError))
        view->playbackError = playbackError->cstr();

    if (!view->wave)
        return;

    view->layout = &view->wave->getStreamFileLayout();
    view->sampleRate = view->wave->getSampleRate();
    view->sampleCount = view->wave->getSampleCount();
    view->lengthMin = StreamFileLayout::GetRegionLengthMin(view->wave->getEncoding() == WaveFile::Encoding::DspAdpcm, view->wave->getEncoding() == WaveFile::Encoding::Pcm8);

    GetPlayingStreamRegions(view->wave, &view->playingNo, &view->nextNo);

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

static s32 GetSelectedRegionNum()
{
    const Sound* sound = GetSelectedStreamSound();
    const StreamFileLayout* layout = sound ? sound->getStreamSoundInfo().getFileLayout() : nullptr;

    return layout ? static_cast<s32>(layout->regions.size()) : -1;
}

void DrawStreamRegionsWindow()
{
    if (!IsStreamPanelShown())
        return;

    DockBesideTracksAtFirstUse();

    const TabRowTitle title = { ICON_LC_FLAG, messages::stream::cRegionsWindowTitle, "###StreamRegionsWindow", 2 };
    const s32 regionNum = HasRegionSupport(GetSelectedStreamSound()) ? GetSelectedRegionNum() : -1;

    sead::FixedSafeString<64> name;
    FormatTabRowTitle(&name, title, regionNum);

    const bool open = ImGui::Begin(name.cstr(), nullptr, ImGuiWindowFlags_NoFocusOnAppearing);

    DrawTabRowCount(title, regionNum);

    sead::FixedSafeString<SoundPlayer::cPlaybackErrorLength> playbackError;
    sead::FixedSafeString<96> editReason;
    sead::FixedSafeString<96> addReason;

    RegionTable view = {};
    SetupRegionTable(&view, &editReason, &addReason, &playbackError);

    SetTabRowDisabled(HasRegionSupport(view.sound) ? nullptr : view.editReason);

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

    WarningPopup("###RegionNameInvalid", messages::stream::cRegionNameInvalidDetail);
    DrawStreamRegionImportDialog();

    ImGui::End();
}
