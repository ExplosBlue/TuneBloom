#include <ui/resource/AudioAssetLabels.h>

#include <aal/AudioAsset.h>
#include <aal/AudioResource.h>
#include <ui/Messages.h>
#include <ui/app/Notifications.h>
#include <ui/app/PopupMgr.h>
#include <ui/file/Document.h>
#include <ui/file/UnsavedChanges.h>
#include <ui/items/ItemSelector.h>
#include <ui/widgets/ContextMenu.h>
#include <ui/widgets/Knob.h>
#include <ui/widgets/Properties.h>
#include <ui/widgets/RangeCell.h>
#include <ui/widgets/RowRename.h>
#include <ui/widgets/TabRowTools.h>
#include <ui/widgets/Widgets.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_internal.h>

#include <math/seadMathCalcCommon.h>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <utility>

static constexpr f32 cMarkerNameWeight    = 1.0f;
static constexpr f32 cMarkerRangeWeight   = 1.2f;
static constexpr f32 cMarkerRangeTableEm  = 24.0f;
static constexpr f32 cMarkerRangeHeight   = 10.0f;
static constexpr f32 cMarkerIdleAlpha     = 0.65f;
static constexpr s32 cMarkerToolNum       = 1;
static constexpr s32 cMarkerVisibleRows   = 8;
static constexpr u32 cExtDecimalMax       = 0xFFFF;
static constexpr u64 cKeyValueMax         = 0xFFFFFFFF;
static constexpr s32 cKeyToolNum          = 1;
static constexpr char cNewKeyName[]       = "Key";
static constexpr char cMarkerWidestTime[] = "00:00.000";
static constexpr char cNewMarkerName[]    = "Marker";
static constexpr char cTagSeparator       = ',';
static constexpr char cTagJoin[]          = ", ";
static constexpr char cTagSpace[]         = " \t";

namespace
{

enum MarkerColumn
{
    cMarkerColumn_Name = 0,
    cMarkerColumn_Range,
    cMarkerColumn_Start,
    cMarkerColumn_Length,
    cMarkerColumn_Num
};

struct MarkerTable
{
    u32 sampleCount = 0;
    u32 sampleRate = 0;
    ImGuiID tableId = 0;
    bool canEdit = false;
};

struct RowEdit
{
    bool changed = false;
    s32 deleteIndex = -1;
};

}

static void FormatTags(const std::vector<std::string>& tags, sead::BufferedSafeString* out)
{
    out->clear();

    for (size_t i = 0; i < tags.size(); i++)
        out->appendWithFormat("%s%s", i == 0 ? "" : cTagJoin, tags[i].c_str());
}

static std::string TrimLabel(const std::string& text)
{
    const size_t first = text.find_first_not_of(cTagSpace);

    return first == std::string::npos ? std::string() : text.substr(first, text.find_last_not_of(cTagSpace) + 1 - first);
}

static std::vector<std::string> ParseTags(const std::string& text)
{
    std::vector<std::string> tags;
    size_t start = 0;

    while (start <= text.size())
    {
        const size_t end = std::min(text.find(cTagSeparator, start), text.size());
        std::string tag = TrimLabel(text.substr(start, end - start));

        if (!tag.empty())
            tags.push_back(std::move(tag));

        start = end + 1;
    }

    return tags;
}

static void FormatKeyValues(const std::vector<aal::AmtaExtEntry>& keys, std::vector<sead::FixedSafeString<256>>* out)
{
    out->resize(keys.size());

    for (size_t i = 0; i < keys.size(); i++)
    {
        if (keys[i].isText)
            (*out)[i] = keys[i].text.c_str();
        else
            (*out)[i].format(keys[i].value > cExtDecimalMax ? "0x%08X" : "%u", keys[i].value);
    }
}

static bool ParseKeyValue(const char* text, aal::AmtaExtEntry* key)
{
    if (key->isText)
    {
        key->text = text;
        return true;
    }

    char* end = nullptr;
    const u64 value = std::strtoull(text, &end, 0);

    if (end == text || *end != '\0' || value > cKeyValueMax)
        return false;

    key->value = static_cast<u32>(value);
    return true;
}

static void RenameKey(aal::AmtaExtEntry* key, const std::string& name, const char* shownValue)
{
    const bool isText = aal::IsAmtaTextKey(name);

    key->name = name;

    if (isText == key->isText)
        return;

    key->isText = isText;
    key->value = 0;
    key->text.clear();

    if (!ParseKeyValue(shownValue, key))
        key->value = 0;
}

static const char* FindKeyNameError(const std::vector<aal::AmtaExtEntry>& keys, s32 index, const std::string& name)
{
    if (name.empty())
        return messages::item::cNameEmpty;

    for (s32 other = 0; other < static_cast<s32>(keys.size()); other++)
    {
        if (other != index && keys[other].name == name)
            return messages::item::cNameDuplicated;
    }

    return nullptr;
}

static std::string MakeNewKeyName(const std::vector<aal::AmtaExtEntry>& keys)
{
    std::string name = cNewKeyName;

    for (u32 number = 2; FindKeyNameError(keys, -1, name); number++)
        name = sead::FormatFixedSafeString<32>("%s%u", cNewKeyName, number).cstr();

    return name;
}

enum class LabelRowMenu
{
    Closed,
    Open,
    Delete
};

static LabelRowMenu DrawLabelRowMenu(const char* id, void* row, ImGuiID renameId, const std::string& name, const char* deleteLabel, const char* editReason)
{
    if (!BeginItemContextMenu(id))
        return LabelRowMenu::Closed;

    LabelRowMenu result = LabelRowMenu::Open;

    if (ContextMenuItem(ICON_LC_TEXT_CURSOR_INPUT, messages::common::cRename, editReason == nullptr, editReason))
        BeginRowRename(row, renameId, name.c_str());

    ContextMenuSeparator();

    ImGui::BeginDisabled(editReason != nullptr);

    if (ContextMenuDangerItem(ICON_LC_TRASH_2, deleteLabel))
        result = LabelRowMenu::Delete;

    ImGui::EndDisabled();

    EndContextMenu();

    return result;
}

static const char* GetMarkerName(const aal::AmtaMarker& marker)
{
    return marker.name.empty() ? messages::innerfile::cNoNameHint : marker.name.c_str();
}

static void CommitLabels(AssetLabelView* view, AudioAsset* asset)
{
    const char* error = nullptr;

    view->meta.clear();

    if (!ActiveAudioResource().setLabels(asset, view->labels, &error))
    {
        PopupMgr::instance()->addPopup({ error, asset });
        return;
    }

    SetUnsavedChanges(true);
}

void RefreshAssetLabels(AssetLabelView* view, AudioAsset* asset)
{
    const std::vector<u8>& meta = asset->getMeta();

    if (view->asset == asset && view->meta == meta)
        return;

    if (view->asset != asset)
    {
        view->selectedMarker = -1;
        view->renameKey = -1;
    }

    const AudioResource& resource = ActiveAudioResource();
    const char* markerReason = nullptr;
    const char* labelReason = nullptr;

    view->asset = asset;
    view->meta = meta;
    view->isCueMoved = false;
    view->isReadable = aal::ReadAmtaLabels(meta, &view->labels);
    view->hasTags = aal::GetAmtaVersion(meta) >= aal::cAmtaVersion5;
    view->canAddKeys = aal::CanAddAmtaKeys(meta);
    view->editReason = resource.canEditMarkers(*asset, &markerReason) ? nullptr : markerReason;
    view->labelEditReason = resource.canEditLabels(*asset, &labelReason) ? nullptr : labelReason;
    view->selectedMarker = sead::Mathi::min(view->selectedMarker, static_cast<s32>(view->labels.markers.size()) - 1);

    FormatTags(view->labels.tags, &view->tagsText);
    FormatKeyValues(view->labels.keys, &view->keyTexts);
}

void SetAssetLabelCues(const AssetLabelView& view, ImGui::LoopWaveformState* waveform)
{
    const std::vector<aal::AmtaMarker>& markers = view.labels.markers;

    waveform->cues.resize(markers.size());

    for (size_t i = 0; i < markers.size(); i++)
        waveform->cues[i] = { markers[i].startFrame, markers[i].frameCount, GetMarkerName(markers[i]) };

    waveform->cuesEditable = view.isReadable && !view.editReason;
    waveform->cueSelected = view.selectedMarker;

    if (waveform->cueDragIndex >= static_cast<int>(markers.size()))
        waveform->cueDragIndex = -1;
}

void ApplyAssetLabelCues(AssetLabelView* view, AudioAsset* asset, const ImGui::LoopWaveformState& waveform)
{
    std::vector<aal::AmtaMarker>& markers = view->labels.markers;
    const s32 index = waveform.cueDragIndex;

    view->selectedMarker = waveform.cueSelected;

    if (index >= 0 && index < static_cast<s32>(markers.size()) && waveform.cues[index].start != markers[index].startFrame)
    {
        markers[index].startFrame = waveform.cues[index].start;
        view->isCueMoved = true;
    }

    if (index >= 0 || !view->isCueMoved)
        return;

    view->isCueMoved = false;
    CommitLabels(view, asset);
}

static void SetupMarkerColumns()
{
    const f32 fieldWidth = ImGui::CalcTextSize(cMarkerWidestTime).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    const bool showRange = ImGui::GetCurrentTable()->OuterRect.GetWidth() >= ImGui::GetFontSize() * cMarkerRangeTableEm;

    ImGui::TableSetupColumn(messages::common::cName, ImGuiTableColumnFlags_WidthStretch, cMarkerNameWeight);
    ImGui::TableSetupColumn(messages::common::cRange, ImGuiTableColumnFlags_WidthStretch | (showRange ? 0 : ImGuiTableColumnFlags_Disabled), cMarkerRangeWeight);
    ImGui::TableSetupColumn(messages::common::cStart, ImGuiTableColumnFlags_WidthFixed, fieldWidth);
    ImGui::TableSetupColumn(messages::common::cLength, ImGuiTableColumnFlags_WidthFixed, fieldWidth);
}

static void DrawMarkerTitles()
{
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);

    for (s32 column = 0; column < cMarkerColumn_Num; column++)
    {
        if (!ImGui::TableSetColumnIndex(column))
            continue;

        const char* title = ImGui::TableGetColumnName(column);

        if (column >= cMarkerColumn_Start)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImFloor((ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(title).x) * 0.5f));

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(title);
    }
}

static void DrawMarkerRowMenu(AssetLabelView* view, const MarkerTable& table, s32 index, RowEdit* edit)
{
    aal::AmtaMarker& marker = view->labels.markers[index];
    const LabelRowMenu menu = DrawLabelRowMenu("MarkerMenu", &marker, table.tableId, marker.name, messages::menu::cDeleteMarker, view->editReason);

    if (menu != LabelRowMenu::Closed)
        view->selectedMarker = index;

    if (menu == LabelRowMenu::Delete)
        edit->deleteIndex = index;
}

static void DrawMarkerName(AssetLabelView* view, const MarkerTable& table, s32 index, RowEdit* edit)
{
    aal::AmtaMarker& marker = view->labels.markers[index];

    ImGui::TableSetColumnIndex(cMarkerColumn_Name);
    ImGui::AlignTextToFramePadding();

    bool rowHovered = false;

    ImGui::SetNextItemAllowOverlap();

    if (DrawTableRowSelectable("###Row", view->selectedMarker == index, &rowHovered))
        view->selectedMarker = index;

    const ImGuiID rowId = ImGui::GetItemID();

    DrawMarkerRowMenu(view, table, index, edit);

    ImGui::SameLine(0.0f, 0.0f);

    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const f32 top = cursor.y + ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset;
    const f32 right = cursor.x + ImGui::GetContentRegionAvail().x;

    if (GetRenamedRow(table.tableId) == &marker)
    {
        sead::FixedSafeString<256> name;

        if (DrawRowRenameFieldAt(cursor, right - cursor.x, &name))
        {
            marker.name = name.cstr();
            edit->changed = true;
        }

        return;
    }

    if (marker.name.empty())
        ImGui::GetWindowDrawList()->AddText(ImVec2(cursor.x, top), ImGui::GetColorU32(ImGuiCol_TextDisabled), GetMarkerName(marker));
    else
        DrawSelectorName(rowId, marker.name.c_str(), ImVec2(cursor.x, top), ImVec2(right, top + ImGui::GetFontSize()), rowHovered);

    if (rowHovered && table.canEdit && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        BeginRowRename(&marker, table.tableId, marker.name.c_str());
}

static void DrawMarkerRange(const AssetLabelView& view, const MarkerTable& table, s32 index)
{
    if (!ImGui::TableSetColumnIndex(cMarkerColumn_Range))
        return;

    const aal::AmtaMarker& marker = view.labels.markers[index];
    const RangeCell cell = BeginRangeCell(cMarkerRangeHeight);
    const f32 alpha = view.selectedMarker == index ? 1.0f : cMarkerIdleAlpha;

    DrawRangeMark(cell.draw, cell.min, cell.max, marker.startFrame, marker.startFrame + marker.frameCount, table.sampleCount, ImGui::GetColorU32(ImGuiCol_CheckMark, alpha));
}

static void DrawMarkerRow(AssetLabelView* view, const MarkerTable& table, s32 index, RowEdit* edit)
{
    aal::AmtaMarker& marker = view->labels.markers[index];

    ImGui::PushID(index);
    ImGui::TableNextRow();

    DrawMarkerName(view, table, index, edit);
    DrawMarkerRange(*view, table, index);

    const u32 startMax = table.sampleCount > marker.frameCount ? table.sampleCount - marker.frameCount : 0;
    const u32 lengthMax = table.sampleCount > marker.startFrame ? table.sampleCount - marker.startFrame : 0;

    if (ImGui::TableSetColumnIndex(cMarkerColumn_Start))
        edit->changed |= IsItemEditCommitted(DrawCellTimeField("##Start", &marker.startFrame, 0, startMax, table.sampleCount, table.sampleRate, !table.canEdit));

    if (ImGui::TableSetColumnIndex(cMarkerColumn_Length))
        edit->changed |= IsItemEditCommitted(DrawCellTimeField("##Length", &marker.frameCount, 0, lengthMax, table.sampleCount, table.sampleRate, !table.canEdit));

    ImGui::PopID();
}

static void DrawMarkerTable(AssetLabelView* view, MarkerTable* table, RowEdit* edit)
{
    const s32 markerNum = static_cast<s32>(view->labels.markers.size());
    const bool scrolls = markerNum > cMarkerVisibleRows;
    const ImGuiTableFlags flags = ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_NoBordersInBody |
                                  ImGuiTableFlags_PadOuterX | (scrolls ? ImGuiTableFlags_ScrollY : 0);
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 cellPaddingY = ImFloor(style.ItemSpacing.y * 0.5f);
    const f32 rowHeight = ImGui::GetFrameHeight() + cellPaddingY * 2.0f;
    const f32 height = scrolls ? rowHeight * (cMarkerVisibleRows + 1) : 0.0f;

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(style.CellPadding.x, cellPaddingY));

    if (!ImGui::BeginTable("MarkerTable", cMarkerColumn_Num, flags, ImVec2(0.0f, height)))
    {
        ImGui::PopStyleVar();
        return;
    }

    table->tableId = ImGui::GetCurrentTable()->ID;

    ImGui::TableSetupScrollFreeze(0, 1);
    SetupMarkerColumns();
    DrawMarkerTitles();

    for (s32 index = 0; index < markerNum; index++)
        DrawMarkerRow(view, *table, index, edit);

    ImGui::EndTable();
    ImGui::PopStyleVar();
}

template <typename T>
static bool EraseEditedRow(std::vector<T>* rows, const RowEdit& edit)
{
    if (edit.deleteIndex < 0 || edit.deleteIndex >= static_cast<s32>(rows->size()))
        return false;

    EndRowRenameOf(&(*rows)[edit.deleteIndex]);
    rows->erase(rows->begin() + edit.deleteIndex);

    return true;
}

static void ApplyRowEdit(AssetLabelView* view, AudioAsset* asset, const RowEdit& edit)
{
    if (EraseEditedRow(&view->labels.markers, edit))
        view->selectedMarker = -1;
    else if (!edit.changed)
        return;

    CommitLabels(view, asset);
}

void DrawAssetMarkersBox(AssetLabelView* view, AudioAsset* asset, u32 sampleCount, u32 sampleRate, u32 addFrame)
{
    std::vector<aal::AmtaMarker>& markers = view->labels.markers;

    if (!view->isReadable || (markers.empty() && view->editReason))
        return;

    if (!BeginPropertyBox("AssetMarkers"))
        return;

    MarkerTable table;
    table.sampleCount = sampleCount;
    table.sampleRate = sampleRate;
    table.canEdit = view->editReason == nullptr;

    RowEdit edit;

    BeginTitleRowTools(messages::resource::cMarkers, CalcTabRowToolsWidth(cMarkerToolNum));

    if (DrawTabRowCommand(ICON_LC_PLUS "###AddMarker", messages::resource::cAddMarker, view->editReason, false))
    {
        markers.push_back({ cNewMarkerName, sead::Mathu::min(addFrame, sampleCount), 0 });
        view->selectedMarker = static_cast<s32>(markers.size()) - 1;
        edit.changed = true;
    }

    ImGui::NewLine();

    if (markers.empty())
        ImGui::TextDisabled("%s", messages::resource::cNoMarkers);
    else
        DrawMarkerTable(view, &table, &edit);

    EndPropertyBox();

    ApplyRowEdit(view, asset, edit);
}

void DrawAssetTagsRow(AssetLabelView* view, AudioAsset* asset)
{
    if (!view->hasTags || !view->isReadable || (view->labels.tags.empty() && view->labelEditReason))
        return;

    PropertyRow(messages::resource::cTags);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::BeginDisabled(view->labelEditReason != nullptr);
    ImGui::InputTextWithHint("##Tags", messages::common::cNone, view->tagsText.getBuffer(), view->tagsText.getBufferSize());

    const bool commit = ImGui::IsItemDeactivatedAfterEdit();

    ImGui::EndDisabled();

    if (view->labelEditReason)
        ImGui::SetItemTooltip("%s", view->labelEditReason);

    if (!commit)
        return;

    view->labels.tags = ParseTags(view->tagsText.cstr());
    CommitLabels(view, asset);
}

void DrawAssetMetadataTitle(AssetLabelView* view, AudioAsset* asset)
{
    if (!view->isReadable || !view->canAddKeys)
    {
        RegionHeader(messages::resource::cMetadataTitle);
        return;
    }

    BeginTitleRowTools(messages::resource::cMetadataTitle, CalcTabRowToolsWidth(cKeyToolNum));

    if (DrawTabRowCommand(ICON_LC_PLUS "###AddKey", messages::resource::cAddKey, view->labelEditReason, false))
    {
        aal::AmtaExtEntry key;
        key.name = MakeNewKeyName(view->labels.keys);

        view->labels.keys.push_back(key);
        view->renameKey = static_cast<s32>(view->labels.keys.size()) - 1;
        FormatKeyValues(view->labels.keys, &view->keyTexts);
        CommitLabels(view, asset);
    }

    ImGui::NewLine();
}

static void ApplyKeyRename(AssetLabelView* view, s32 index, const char* typedName, RowEdit* edit)
{
    const std::string name = TrimLabel(typedName);
    const char* error = FindKeyNameError(view->labels.keys, index, name);

    if (error)
    {
        notify::Add(notify::Kind::Warning, error);
        return;
    }

    RenameKey(&view->labels.keys[index], name, view->keyTexts[index].cstr());
    edit->changed = true;
}

static void DrawKeyName(AssetLabelView* view, s32 index, ImGuiID renameId, RowEdit* edit)
{
    aal::AmtaExtEntry& key = view->labels.keys[index];
    const ImGuiWindow* window = ImGui::GetCurrentWindow();
    const f32 labelEnd = CalcPropertyLabelWidth() - window->DC.ColumnsOffset.x - window->DC.GroupOffset.x;
    const f32 width = sead::Mathf::max(1.0f, labelEnd - ImGui::GetCursorPosX() - ImGui::GetStyle().ItemSpacing.x);

    if (view->renameKey == index && !view->meta.empty())
    {
        BeginRowRename(&key, renameId, key.name.c_str());
        view->renameKey = -1;
    }

    const ImVec2 pos = ImGui::GetCursorScreenPos();

    if (GetRenamedRow(renameId) == &key)
    {
        sead::FixedSafeString<256> name;

        if (DrawRowRenameFieldAt(pos, width, &name))
            ApplyKeyRename(view, index, name.cstr(), edit);

        return;
    }

    const ImVec2 textMin(pos.x, pos.y + ImGui::GetStyle().FramePadding.y);
    const ImVec2 textMax(pos.x + width, textMin.y + ImGui::GetFontSize());

    ImGui::InvisibleButton("##Name", ImVec2(width, ImGui::GetFrameHeight()));

    const bool isHovered = ImGui::IsItemHovered();

    if (key.name.empty())
        ImGui::GetWindowDrawList()->AddText(textMin, ImGui::GetColorU32(ImGuiCol_TextDisabled), messages::innerfile::cNoNameHint);
    else
        DrawSelectorName(ImGui::GetItemID(), key.name.c_str(), textMin, textMax, isHovered);

    if (isHovered && !view->labelEditReason && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        BeginRowRename(&key, renameId, key.name.c_str());

    if (DrawLabelRowMenu("KeyMenu", &key, renameId, key.name, messages::menu::cDeleteKey, view->labelEditReason) == LabelRowMenu::Delete)
        edit->deleteIndex = index;
}

static void DrawKeyValue(AssetLabelView* view, s32 index, RowEdit* edit)
{
    sead::FixedSafeString<256>& text = view->keyTexts[index];

    ImGui::BeginDisabled(view->labelEditReason != nullptr);
    ImGui::InputTextWithHint("##Value", messages::common::cNone, text.getBuffer(), text.getBufferSize());

    const bool commit = ImGui::IsItemDeactivatedAfterEdit();

    ImGui::EndDisabled();

    if (view->labelEditReason)
        ImGui::SetItemTooltip("%s", view->labelEditReason);

    if (!commit)
        return;

    if (ParseKeyValue(text.cstr(), &view->labels.keys[index]))
        edit->changed = true;
    else
        FormatKeyValues(view->labels.keys, &view->keyTexts);
}

void DrawAssetKeyRows(AssetLabelView* view, AudioAsset* asset)
{
    std::vector<aal::AmtaExtEntry>& keys = view->labels.keys;
    const ImGuiID renameId = ImGui::GetID("KeyRows");
    RowEdit edit;

    for (s32 index = 0; index < static_cast<s32>(keys.size()); index++)
    {
        ImGui::PushID(index);
        DrawKeyName(view, index, renameId, &edit);
        PropertyValueColumn();
        DrawKeyValue(view, index, &edit);
        ImGui::PopID();
    }

    if (!EraseEditedRow(&keys, edit) && !edit.changed)
        return;

    CommitLabels(view, asset);
}
