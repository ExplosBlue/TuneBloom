#include <ui/file/UnsavedChanges.h>

#include <ui/backup/Backup.h>
#include <ui/widgets/Dialogs.h>
#include <ui/file/File.h>
#include <ui/file/FileFlow.h>
#include <ui/items/ItemColumns.h>
#include <ui/Messages.h>
#include <ui/items/Selection.h>
#include <ui/settings/UiColors.h>
#include <ui/app/Workspace.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_custom.h>
#include <imgui/imgui_internal.h>

#include <app/Utilll.h>

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

static constexpr f32 cWidthEm     = 44.0f;
static constexpr f32 cHeightEm    = 26.0f;
static constexpr f32 cMinWidthEm  = 30.0f;
static constexpr f32 cMinHeightEm = 12.0f;
static constexpr f32 cFooterGapEm = 0.6f;
static constexpr f32 cColumnGap   = 8.0f;

static const char* cPopupName = "###UnsavedChanges";

namespace
{

enum class ChangeKind
{
    Added,
    Modified,
    Removed
};

enum class ChangeColumn : ImGuiID
{
    Id,
    Name,
    Type,
    Change
};

struct SavedItem
{
    u64 uid;
    Item::ItemType itemType;
    u32 id;
    std::string name;
};

struct ChangeRow
{
    Item* item;
    Item::ItemType itemType;
    u32 id;
    std::string name;
    ChangeKind kind;
    bool isProjectInfo;
};

}

static const ItemColumn cColumns[] = {
    { "Type",   messages::common::cType,   messages::common::cSequenceFile, nullptr, nullptr, cItemColumnFlag_Text, nullptr },
    { "Change", messages::unsaved::cColumnChange, messages::common::cModified,      nullptr, nullptr, cItemColumnFlag_Text, nullptr }
};

static std::vector<SavedItem> sSavedItems;
static std::vector<ChangeRow> sRows;
static std::vector<u32> sOrder;
static s32 sSelectedRow = -1;
static bool sWantsOpen = false;
static bool sSortDirty = false;

template <typename Func>
static void ForEachTrackedItem(Func func)
{
    Item::List* lists[] = {
        &sBfsar.getSoundList(), &sBfsar.getSoundSetList(), &sBfsar.getBankList(), &sBfsar.getWaveArchiveList(),
        &sBfsar.getGroupList(), &sBfsar.getPlayerList(),
        &sBfsar.getWaveFileList(), &sBfsar.getSequenceFileList(), &sBfsar.getBankFileList()
    };

    for (Item::List* list : lists)
    {
        for (Item* item : *list)
        {
            if (item->isStateTracked())
                func(item);
        }
    }
}

static std::string GetListName(const Item* item)
{
    sead::FixedSafeString<256> name;
    FormatItemListName(item, &name);

    return name.cstr();
}

void CaptureSavedState()
{
    sBfsar.captureItemBaselines();

    sSavedItems.clear();

    ForEachTrackedItem([](Item* item)
    {
        sSavedItems.push_back({ item->getUid(), item->getItemType(), GetItemListId(item), GetListName(item) });
    });
}

static void CollectChanges()
{
    sRows.clear();

    if (!sBfsar.isOpen())
        return;

    if (sBfsar.isInfoModified())
        sRows.push_back({ nullptr, Item::ItemType::Invalid, 0, messages::common::cProjectInfo, ChangeKind::Modified, true });

    std::unordered_set<u64> present;

    ForEachTrackedItem([&present](Item* item)
    {
        present.insert(item->getUid());

        if (!item->isModifiedSinceBaseline())
            return;

        const ChangeKind kind = item->hasBaseline() ? ChangeKind::Modified : ChangeKind::Added;

        sRows.push_back({ item, item->getItemType(), GetItemListId(item), GetListName(item), kind, false });
    });

    for (const SavedItem& saved : sSavedItems)
    {
        if (present.count(saved.uid) == 0)
            sRows.push_back({ nullptr, saved.itemType, saved.id, saved.name, ChangeKind::Removed, false });
    }
}

static const char* GetKindText(ChangeKind kind)
{
    switch (kind)
    {
        case ChangeKind::Added:
            return messages::unsaved::cAdded;

        case ChangeKind::Removed:
            return messages::unsaved::cRemoved;

        default:
            break;
    }

    return messages::common::cModified;
}

static const char* GetTypeText(const ChangeRow& row)
{
    if (row.isProjectInfo)
        return messages::common::cProject;

    return GetItemTypeName(row.itemType);
}

static s32 CompareRows(const ChangeRow& a, const ChangeRow& b, ChangeColumn column)
{
    switch (column)
    {
        case ChangeColumn::Id:
            if (a.isProjectInfo != b.isProjectInfo)
                return a.isProjectInfo ? -1 : 1;

            return a.id < b.id ? -1 : (a.id > b.id ? 1 : 0);

        case ChangeColumn::Name:
            return a.name.compare(b.name);

        case ChangeColumn::Type:
            return static_cast<s32>(a.itemType) - static_cast<s32>(b.itemType);

        case ChangeColumn::Change:
            return static_cast<s32>(a.kind) - static_cast<s32>(b.kind);
    }

    return 0;
}

static void SortRows(ImGuiTableSortSpecs* sortSpecs)
{
    if (!sSortDirty && !(sortSpecs && sortSpecs->SpecsDirty))
        return;

    sOrder.resize(sRows.size());

    for (u32 i = 0; i < sOrder.size(); i++)
        sOrder[i] = i;

    if (sortSpecs && sortSpecs->SpecsCount > 0)
    {
        const ChangeColumn column = static_cast<ChangeColumn>(sortSpecs->Specs[0].ColumnUserID);
        const bool descending = sortSpecs->Specs[0].SortDirection == ImGuiSortDirection_Descending;

        std::stable_sort(sOrder.begin(), sOrder.end(), [column, descending](u32 a, u32 b)
        {
            const s32 result = CompareRows(sRows[a], sRows[b], column);

            return descending ? result > 0 : result < 0;
        });
    }

    if (sortSpecs)
        sortSpecs->SpecsDirty = false;

    sSortDirty = false;
}

static bool GoToRow(const ChangeRow& row)
{
    if (row.isProjectInfo)
    {
        SetUITab(UIType::ProjectInfo);
        FocusInfoWindow();
        return true;
    }

    if (!row.item)
        return false;

    SelectItem(row.item);
    return true;
}

static void DrawCellText(const char* text)
{
    ImVec2 pos = ImGui::GetCursorScreenPos();
    pos.y += ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset;

    const f32 width = ImGui::GetContentRegionAvail().x - cColumnGap;

    pos.x += cColumnGap;
    ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), pos, ImVec2(pos.x + width, pos.y + ImGui::GetFontSize()), pos.x + width, pos.x + width, text, nullptr, nullptr);
}

static bool DrawRows()
{
    const ImVec4 valueColor = GetItemValueColor();

    const ImU32 rowColor        = ImGui::GetColorU32(ImGuiCol_Header);
    const ImU32 rowHoveredColor = ImGui::GetColorU32(ImGuiCol_HeaderHovered);
    const ImU32 rowActiveColor  = ImGui::GetColorU32(ImGuiCol_HeaderActive);

    bool goTo = false;

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<s32>(sOrder.size()));

    while (clipper.Step())
    {
        for (s32 i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
        {
            const s32 rowIndex = static_cast<s32>(sOrder[i]);
            const ChangeRow& row = sRows[rowIndex];
            const bool selected = sSelectedRow == rowIndex;

            ImGui::PushID(rowIndex);
            ImGui::TableNextRow();

            // ID
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();

            if (row.isProjectInfo)
                ImGui::TextColored(valueColor, "-");
            else
                ImGui::TextColored(valueColor, "%u", row.id);

            // Name
            ImGui::TableSetColumnIndex(1);
            ImGui::AlignTextToFramePadding();

            ImVec2 namePos = ImGui::GetCursorScreenPos();
            namePos.y += ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset;
            const f32 nameRight = namePos.x + ImGui::GetContentRegionAvail().x;

            ImGui::PushStyleColor(ImGuiCol_Header,        uicolors::cTransparentU32);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, uicolors::cTransparentU32);
            ImGui::PushStyleColor(ImGuiCol_HeaderActive,  uicolors::cTransparentU32);

            if (ImGui::Selectable("##Row", selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_DontClosePopups))
            {
                sSelectedRow = rowIndex;

                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    goTo = true;
            }

            ImGui::PopStyleColor(3);

            const bool rowHovered = ImGui::IsItemHovered();

            const ImGuiID rowFadeId = ImGui::GetItemID() + 1;
            const f32 rowHoverRatio = ImGui::GetHoverRatio(rowFadeId, rowHovered);

            if (rowHoverRatio > 0.0f || selected)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, rowHovered && ImGui::IsItemActive() ? rowActiveColor : selected ? rowColor : ImGui::GetHoverColorU32(rowFadeId, rowHovered, 0, rowHoveredColor));

            const ImGuiCol nameColor = row.kind == ChangeKind::Removed ? ImGuiCol_TextDisabled : ImGuiCol_Text;

            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(nameColor));
            ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), namePos, ImVec2(nameRight, namePos.y + ImGui::GetFontSize()), nameRight, nameRight, row.name.c_str(), nullptr, nullptr);
            ImGui::PopStyleColor();

            ImGui::PushStyleColor(ImGuiCol_Text, valueColor);

            // Type
            if (ImGui::TableSetColumnIndex(2))
                DrawCellText(GetTypeText(row));

            // Change
            if (ImGui::TableSetColumnIndex(3))
                DrawCellText(GetKindText(row.kind));

            ImGui::PopStyleColor();
            ImGui::PopID();
        }
    }

    return goTo;
}

void OpenUnsavedChangesDialog()
{
    sWantsOpen = true;
}

void DrawUnsavedChangesDialog()
{
    if (sWantsOpen)
    {
        sWantsOpen = false;

        CollectChanges();

        sSelectedRow = -1;
        sSortDirty = true;

        ImGui::OpenPopup(cPopupName);
    }

    const f32 fontSize = ImGui::GetFontSize();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    const f32 maxWidth = viewport->WorkSize.x * 0.85f;
    const f32 maxHeight = viewport->WorkSize.y * 0.85f;

    const ImVec2 windowSize(sead::Mathf::min(fontSize * cWidthEm, maxWidth), sead::Mathf::min(fontSize * cHeightEm, maxHeight));

    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(sead::Mathf::min(fontSize * cMinWidthEm, maxWidth), sead::Mathf::min(fontSize * cMinHeightEm, maxHeight)), ImVec2(FLT_MAX, FLT_MAX));

    if (!BeginDialog(sead::FormatFixedSafeString<96>(ICON_LC_FILE_DIFF " %s%s", messages::unsaved::cTitle, cPopupName).cstr(), nullptr, ImGuiWindowFlags_NoScrollbar))
        return;

    bool goTo = false;

    const ImGuiTableFlags tableFlags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_SizingFixedFit
        | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate | ImGuiTableFlags_Resizable;

    const f32 footerGap = ImFloor(fontSize * cFooterGapEm);

    PushItemTablePadding();

    if (ImGui::BeginTable("UnsavedTable", 4, tableFlags, ImVec2(0.0f, -(CalcDialogFooterHeight() + footerGap))))
    {
        ImGui::TableSetupColumn(messages::item::cColumnId, ImGuiTableColumnFlags_WidthFixed, GetItemIdColumnWidth(Item::ItemType::Sound), static_cast<ImGuiID>(ChangeColumn::Id));
        ImGui::TableSetupColumn(messages::common::cName, ImGuiTableColumnFlags_WidthStretch, 0.0f, static_cast<ImGuiID>(ChangeColumn::Name));
        ImGui::TableSetupColumn(cColumns[0].title, ImGuiTableColumnFlags_WidthFixed, GetItemColumnWidth(cColumns[0]), static_cast<ImGuiID>(ChangeColumn::Type));
        ImGui::TableSetupColumn(cColumns[1].title, ImGuiTableColumnFlags_WidthFixed, GetItemColumnWidth(cColumns[1]), static_cast<ImGuiID>(ChangeColumn::Change));
        ImGui::TableSetupScrollFreeze(0, 1);

        DrawItemHeadersRow(cColumns, 2);

        SortRows(ImGui::TableGetSortSpecs());

        goTo = DrawRows();

        if (sRows.empty())
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(1);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", messages::unsaved::cNoChanges);
        }

        ImGui::EndTable();
    }

    ImGui::PopStyleVar();

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + footerGap - ImGui::GetStyle().ItemSpacing.y);

    const s32 rowNum = static_cast<s32>(sRows.size());
    const bool hasSelection = sSelectedRow >= 0 && sSelectedRow < rowNum;
    const bool canGoTo = hasSelection && sRows[sSelectedRow].kind != ChangeKind::Removed;

    ImGui::AlignTextToFramePadding();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetStyle().CellPadding.x);
    ImGui::TextDisabled(rowNum == 1 ? messages::unsaved::cCountOneFormat : messages::unsaved::cCountFormat, rowNum);
    ImGui::SameLine();

    const sead::FormatFixedSafeString<32> goToLabel("%s %s", ICON_LC_EXTERNAL_LINK, messages::common::cGoTo);

    BeginDialogFooter({messages::common::cSave, goToLabel.cstr(), messages::common::cClose}, false);

    bool close = false;

    if (DialogButton(messages::common::cSave))
    {
        RequestSaveFile();
        close = true;
    }

    ImGui::BeginDisabled(!canGoTo);

    if (DialogButton(goToLabel.cstr()))
        goTo = true;

    ImGui::EndDisabled();

    if (!canGoTo)
        ImGui::SetItemTooltip("%s", hasSelection ? messages::unsaved::cRemovedReason : messages::unsaved::cNoRowReason);

    if (DialogButton(messages::common::cClose) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        close = true;

    if (goTo && hasSelection && GoToRow(sRows[sSelectedRow]))
        close = true;

    if (close)
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

bool gUnsavedChanges = false;

static u64 sEditGeneration = 0;

u64 GetEditGeneration()
{
    return sEditGeneration;
}

void SetUnsavedChanges(bool dirty)
{
    if (dirty)
        sEditGeneration++;
    else
    {
        CaptureSavedState();
        backup::OnClean();
    }

    if (gUnsavedChanges != dirty)
    {
        gUnsavedChanges = dirty;
        util::refreshTitleDirty(dirty);
    }
}
