#include <ui/file/UnsavedChanges.h>

#include <ui/backup/Backup.h>
#include <ui/widgets/Dialogs.h>
#include <ui/file/DocumentMgr.h>
#include <ui/file/DocumentState.h>
#include <ui/file/File.h>
#include <ui/file/FileFlow.h>
#include <ui/browser/FilesPanel.h>
#include <ui/items/ItemColumns.h>
#include <ui/Messages.h>
#include <ui/items/Selection.h>
#include <ui/app/Shortcuts.h>
#include <ui/settings/UiColors.h>
#include <ui/app/Workspace.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_custom.h>
#include <imgui/imgui_internal.h>

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
    Id = static_cast<ImGuiID>(ItemColumnId::Id),
    Name = static_cast<ImGuiID>(ItemColumnId::Name),
    Type = static_cast<ImGuiID>(ItemColumnId::First),
    Change
};

struct ChangeRow
{
    Document* document;
    s32 documentIndex;
    Item* item;
    Item::ItemType itemType;
    u32 id;
    std::string name;
    ChangeKind kind;
    bool isProjectInfo;
    bool isDocumentHeader;
    s32 changeCount;
};

}

static const ItemColumn cColumns[] = {
    { "Type",   messages::common::cType,   messages::common::cSequenceFile, nullptr, nullptr, cItemColumnFlag_Text, nullptr },
    { "Change", messages::unsaved::cColumnChange, messages::common::cModified,      nullptr, nullptr, cItemColumnFlag_Text, nullptr }
};

static std::vector<ChangeRow> sRows;
static std::vector<u32> sOrder;
static s32 sSelectedRow = -1;
static bool sWantsOpen = false;
static bool sIsExitPrompt = false;
static bool sSortDirty = false;

template <typename Func>
static void ForEachTrackedItem(Document& document, Func func)
{
    document.forEachItemList([&func](Item::List& list)
    {
        for (Item* item : list)
        {
            if (item->isStateTracked())
                func(item);
        }
    });
}

static std::string GetListName(const Item* item)
{
    sead::FixedSafeString<256> name;
    FormatItemListName(item, &name);

    return name.cstr();
}

void CaptureSavedState()
{
    std::vector<DocumentState::SavedItem>& savedItems = ActiveDocumentState().savedItems;
    savedItems.clear();

    Document* document = ActiveDocument();

    if (!document)
        return;

    document->captureItemBaselines();

    ForEachTrackedItem(*document, [&savedItems](Item* item)
    {
        savedItems.push_back({ item->getUid(), item->getItemType(), GetItemListId(item), GetListName(item) });
    });
}

static void CollectDocumentChanges(Document& document, s32 documentIndex)
{
    DocumentFocus focus(&document);

    const size_t headerIndex = sRows.size();

    sRows.push_back({ &document, documentIndex, nullptr, Item::ItemType::Invalid, 0, document.displayName(), ChangeKind::Modified, false, true, 0 });

    if (document.isInfoModified())
        sRows.push_back({ &document, documentIndex, nullptr, Item::ItemType::Invalid, 0, messages::common::cProjectInfo, ChangeKind::Modified, true, false, 0 });

    std::unordered_set<u64> present;

    ForEachTrackedItem(document, [&present, &document, documentIndex](Item* item)
    {
        present.insert(item->getUid());

        if (!item->isModifiedSinceBaseline())
            return;

        const ChangeKind kind = item->hasBaseline() ? ChangeKind::Modified : ChangeKind::Added;

        sRows.push_back({ &document, documentIndex, item, item->getItemType(), GetItemListId(item), GetListName(item), kind, false, false, 0 });
    });

    for (const DocumentState::SavedItem& saved : document.state().savedItems)
    {
        if (present.count(saved.uid) == 0)
            sRows.push_back({ &document, documentIndex, nullptr, saved.itemType, saved.id, saved.name, ChangeKind::Removed, false, false, 0 });
    }

    sRows[headerIndex].changeCount = static_cast<s32>(sRows.size() - headerIndex - 1);
}

static void CollectChanges()
{
    sRows.clear();

    const std::vector<Document*>& documents = GetOpenDocuments();

    for (size_t i = 0; i < documents.size(); i++)
    {
        if (documents[i]->state().unsaved && documents[i]->isLoaded())
            CollectDocumentChanges(*documents[i], static_cast<s32>(i));
    }
}

static s32 CountChangeRows()
{
    s32 count = 0;

    for (const ChangeRow& row : sRows)
    {
        if (!row.isDocumentHeader)
            count++;
    }

    return count;
}

static s32 CountChangedDocuments()
{
    return static_cast<s32>(sRows.size()) - CountChangeRows();
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

static bool IsRowBefore(const ChangeRow& a, const ChangeRow& b, const ImGuiTableColumnSortSpecs* spec)
{
    if (a.documentIndex != b.documentIndex)
        return a.documentIndex < b.documentIndex;

    if (a.isDocumentHeader != b.isDocumentHeader)
        return a.isDocumentHeader;

    if (!spec)
        return false;

    const s32 result = CompareRows(a, b, static_cast<ChangeColumn>(spec->ColumnUserID));

    return spec->SortDirection == ImGuiSortDirection_Descending ? result > 0 : result < 0;
}

static void SortRows(ImGuiTableSortSpecs* sortSpecs)
{
    if (!sSortDirty && !(sortSpecs && sortSpecs->SpecsDirty))
        return;

    sOrder.resize(sRows.size());

    for (u32 i = 0; i < sOrder.size(); i++)
        sOrder[i] = i;

    const ImGuiTableColumnSortSpecs* spec = sortSpecs && sortSpecs->SpecsCount > 0 ? &sortSpecs->Specs[0] : nullptr;

    std::stable_sort(sOrder.begin(), sOrder.end(), [spec](u32 a, u32 b)
    {
        return IsRowBefore(sRows[a], sRows[b], spec);
    });

    if (sortSpecs)
        sortSpecs->SpecsDirty = false;

    sSortDirty = false;
}

static bool GoToRow(const ChangeRow& row)
{
    if (!ActivateDocument(row.document))
        return false;

    if (row.isProjectInfo)
        SetUITab(UIType::ProjectInfo);

    if (row.isProjectInfo || row.isDocumentHeader)
    {
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

static void DrawDocumentHeaderCells(const ChangeRow& row, const ImVec2& namePos, f32 nameRight)
{
    const char* icon = GetFileKindIcon(row.document->fileKind());
    const f32 nameX = namePos.x + ImGui::CalcTextSize(icon).x + ImGui::GetStyle().ItemInnerSpacing.x;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddText(namePos, ImGui::GetColorU32(GetFileKindColor(row.document->fileKind())), icon);
    ImGui::RenderTextEllipsis(drawList, ImVec2(nameX, namePos.y), ImVec2(nameRight, namePos.y + ImGui::GetFontSize()), nameRight, nameRight, row.name.c_str(), nullptr, nullptr);

    ImGui::PushStyleColor(ImGuiCol_Text, GetItemValueColor());

    if (ImGui::TableSetColumnIndex(3))
        DrawCellText(sead::FormatFixedSafeString<32>(row.changeCount == 1 ? messages::unsaved::cCountOneFormat : messages::unsaved::cCountFormat, row.changeCount).cstr());

    ImGui::PopStyleColor();
}

static void DrawChangeCells(const ChangeRow& row, const ImVec2& namePos, f32 nameRight)
{
    const f32 nameX = namePos.x + ImGui::GetFontSize();
    const ImGuiCol nameColor = row.kind == ChangeKind::Removed ? ImGuiCol_TextDisabled : ImGuiCol_Text;

    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(nameColor));
    ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), ImVec2(nameX, namePos.y), ImVec2(nameRight, namePos.y + ImGui::GetFontSize()), nameRight, nameRight, row.name.c_str(), nullptr, nullptr);
    ImGui::PopStyleColor();

    ImGui::PushStyleColor(ImGuiCol_Text, GetItemValueColor());

    if (ImGui::TableSetColumnIndex(2))
        DrawCellText(GetTypeText(row));

    if (ImGui::TableSetColumnIndex(3))
        DrawCellText(GetKindText(row.kind));

    ImGui::PopStyleColor();
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

            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();

            if (row.isProjectInfo)
                ImGui::TextColored(valueColor, "-");
            else if (!row.isDocumentHeader)
                ImGui::TextColored(valueColor, "%u", row.id);

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

            if (row.isDocumentHeader)
                DrawDocumentHeaderCells(row, namePos, nameRight);
            else
                DrawChangeCells(row, namePos, nameRight);

            ImGui::PopID();
        }
    }

    return goTo;
}

void OpenUnsavedChangesDialog()
{
    sWantsOpen = true;
    sIsExitPrompt = false;
}

void OpenExitChangesDialog()
{
    sWantsOpen = true;
    sIsExitPrompt = true;
}

static void DrawExitHeading()
{
    const s32 documentNum = CountChangedDocuments();

    if (documentNum == 1)
        ImGui::TextUnformatted(messages::unsaved::cExitHeadingOne);
    else
        ImGui::Text(messages::unsaved::cExitHeadingFormat, documentNum);
}

static bool DrawExitFooter()
{
    BeginDialogFooter({messages::mainmenu::cSaveAll, messages::unsaved::cDontSave, messages::common::cCancel}, false);

    bool close = false;

    if (DialogButton(messages::mainmenu::cSaveAll))
    {
        RequestExitAfterSaveAll();
        close = true;
    }

    if (DialogButton(messages::unsaved::cDontSave))
    {
        RequestExitWithoutSaving();
        close = true;
    }

    if (DialogButton(messages::common::cCancel) || shortcuts::Pressed(shortcuts::Action::CloseDialog))
        close = true;

    return close;
}

static bool DrawReviewFooter(bool* outGoTo)
{
    const s32 rowNum = static_cast<s32>(sRows.size());
    const bool hasSelection = sSelectedRow >= 0 && sSelectedRow < rowNum;
    const bool canGoTo = hasSelection && sRows[sSelectedRow].kind != ChangeKind::Removed;

    const sead::FormatFixedSafeString<32> goToLabel("%s %s", ICON_LC_EXTERNAL_LINK, messages::common::cGoTo);

    BeginDialogFooter({messages::mainmenu::cSaveAll, goToLabel.cstr(), messages::common::cClose}, false);

    bool close = false;

    if (DialogButton(messages::mainmenu::cSaveAll))
    {
        RequestSaveAll();
        close = true;
    }

    ImGui::BeginDisabled(!canGoTo);

    if (DialogButton(goToLabel.cstr()))
        *outGoTo = true;

    ImGui::EndDisabled();

    if (!canGoTo)
        ImGui::SetItemTooltip("%s", hasSelection ? messages::unsaved::cRemovedReason : messages::unsaved::cNoRowReason);

    if (DialogButton(messages::common::cClose) || shortcuts::Pressed(shortcuts::Action::CloseDialog))
        close = true;

    return close;
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

    if (sIsExitPrompt)
        DrawExitHeading();

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

    const s32 changeNum = CountChangeRows();

    ImGui::AlignTextToFramePadding();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetStyle().CellPadding.x);
    ImGui::TextDisabled(changeNum == 1 ? messages::unsaved::cCountOneFormat : messages::unsaved::cCountFormat, changeNum);
    ImGui::SameLine();

    bool close = sIsExitPrompt ? DrawExitFooter() : DrawReviewFooter(&goTo);

    if (goTo && sSelectedRow >= 0 && sSelectedRow < static_cast<s32>(sRows.size()) && GoToRow(sRows[sSelectedRow]))
        close = true;

    if (close)
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

bool HasUnsavedChanges()
{
    return ActiveDocumentState().unsaved;
}

u64 GetEditGeneration()
{
    return ActiveDocumentState().editGeneration;
}

void SetUnsavedChanges(bool dirty)
{
    Document* document = ActiveDocument();

    if (!dirty)
    {
        CaptureSavedState();
        backup::OnClean();
    }

    if (!document)
        return;

    DocumentState& state = document->state();

    if (dirty)
    {
        state.editGeneration = NewEditGeneration();
        PinDocument(document);
    }

    if (state.unsaved != dirty)
    {
        state.unsaved = dirty;
        RefreshWindowTitle();
    }
}
