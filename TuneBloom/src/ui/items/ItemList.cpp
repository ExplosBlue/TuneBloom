#include <ui/items/ItemListInternal.h>

#include <ui/file/FileWindow.h>
#include <ui/items/ItemSelector.h>
#include <ui/widgets/ListFilter.h>
#include <ui/Messages.h>
#include <ui/player/PlayerViews.h>
#include <ui/widgets/RowRename.h>
#include <ui/app/Shortcuts.h>
#include <ui/widgets/Widgets.h>

#include <bfsar/files/WaveFile.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_custom.h>

#include <algorithm>

static constexpr const char* cItemListTableId = "ItemListValues";

static constexpr f32 cItemNameFreezeRatio = 0.6f;
static constexpr f32 cItemTextComfortEm   = 8.0f;
static constexpr f32 cItemNameComfortEm   = 12.0f;
static constexpr f32 cItemNameEndMinEm    = 4.5f;
static constexpr f32 cItemTextMinEm       = 5.0f;
static constexpr f32 cItemScrollMinRatio  = 2.0f;
static constexpr f32 cItemDimHeaderAlpha  = 0.55f;

struct ItemRowName
{
    sead::FixedSafeString<256> name;
    sead::FixedSafeString<64> lead;
    sead::FixedSafeString<512> text;
    ImVec2 cursor;
    f32 left = 0.0f;
    f32 top = 0.0f;
    f32 right = 0.0f;
    bool isModified = false;
};

static Item* sScrollItem = nullptr;
static const char* sNextListNote = nullptr;
static const ItemColumn* sNextListColumns = nullptr;
static u32 sNextListColumnCount = 0;
static bool sNextListHideAll = false;
static ItemRowActionCallback sNextListAction = nullptr;
static ItemRemoveText sNextListRemoveText;

void ScrollItemListTo(Item* item)
{
    sScrollItem = item;
}

Item* GetItemListScrollTarget()
{
    return sScrollItem;
}

void SetNextItemListRemoveText(const char* label, const char* manyFormat, bool removesData)
{
    sNextListRemoveText = { label, manyFormat, removesData };
}

void SetNextItemListNote(const char* note)
{
    sNextListNote = note;
}

void SetNextItemListColumns(const ItemColumn* columns, u32 columnCount, bool hideAll)
{
    sNextListColumns = columns;
    sNextListColumnCount = columnCount;
    sNextListHideAll = hideAll;
}

void SetNextItemListAction(ItemRowActionCallback callback)
{
    sNextListAction = callback;
}

static bool IsItemVisible(Item* item, ItemFilterCallback filterCallback)
{
    return item && (!filterCallback || filterCallback(item));
}

static void CollectItemListRows(ItemListContext& ctx)
{
    ctx.listNote = sNextListNote;
    sNextListNote = nullptr;

    ctx.removeText = sNextListRemoveText;
    sNextListRemoveText = ItemRemoveText();

    ctx.actionCallback = sNextListAction;
    sNextListAction = nullptr;

    ctx.listItems.reserve(ctx.list.size());

    for (auto it = ctx.list.robustBegin(); it != ctx.list.robustEnd(); ++it)
        if (it->val())
            ctx.listItems.push_back(it->val());

    PruneFilteredSelection(ctx.listItems, ctx.filterCallback);

    ctx.rows.reserve(ctx.listItems.size());

    for (Item *item : ctx.listItems)
        if (IsItemVisible(item, ctx.filterCallback))
            ctx.rows.push_back(item);

    if (sNextListColumns)
    {
        ctx.columns = sNextListColumns;
        ctx.columnCount = sNextListColumnCount;
        ctx.hideAll = sNextListHideAll;
        sNextListColumns = nullptr;
        sNextListHideAll = false;
    }
    else if (!ctx.listItems.empty())
    {
        ctx.columns = GetItemColumns(ctx.listItems.front()->getItemType(), &ctx.columnCount);
    }

    ctx.itemType = ctx.listItems.empty() ? Item::ItemType::Invalid : ctx.listItems.front()->getItemType();

    ctx.hasHeader = ctx.columns != nullptr;
}

static void BeginItemListChild(ItemListContext& ctx, const char* id)
{
    ImGuiChildFlags flags = 0;

    ImGuiWindow* currentWindow = ImGui::GetCurrentWindow();
    if (currentWindow == ImGui::FindWindowByName("###InfoWindow"))
    {
        flags = ImGuiChildFlags_AlwaysUseWindowPadding;
    }
    else if (currentWindow == ImGui::FindWindowByName("###SubInfoWindow"))
    {
        flags = ImGuiChildFlags_AlwaysUseWindowPadding;
        ctx.isSubWindow = true;
    }

    ctx.joined = flags != 0 && ImGui::GetCursorPosY() != ImGui::GetCursorStartPos().y;

    if (ctx.joined)
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ImGui::GetStyle().WindowPadding.x, 0.0f));

    ImGui::BeginChild(sead::FormatFixedSafeString<64>("%sInnerWindow", id).cstr(), ImVec2(0.0f, 0.0f), flags, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    if (ctx.joined)
        ImGui::PopStyleVar();
}

static void SetupItemListLayout(ItemListContext& ctx)
{
    if (ctx.listItems.empty() && ctx.joined)
        ctx.hasHeader = true;

    ctx.actionColumn = ctx.actionCallback ? 0 : -1;
    ctx.idColumn = ctx.hasHeader ? ctx.actionColumn + 1 : -1;
    ctx.nameColumn = ctx.idColumn + 1 + (ctx.hasHeader ? 0 : ctx.actionColumn + 1);

    if (ctx.filterCallback && ctx.hasHeader)
    {
        SetItemTextFilterEnabled(false);

        for (Item *item : ctx.listItems)
            if (ctx.filterCallback(item))
                ctx.allRows.push_back(item);

        SetItemTextFilterEnabled(true);
    }

    ctx.fitRows = ctx.filterCallback && ctx.hasHeader ? &ctx.allRows : &ctx.rows;
    ctx.selection = ctx.isSubWindow ? &sSubSelectedItem : &sSelectedItem;

    Item*& selectedItem = *ctx.selection;

    ctx.listFocused = !ImGui::GetIO().WantTextInput && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && selectedItem && &ctx.list == selectedItem->list();

    ctx.tableFlags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_SizingFixedFit;

    if (ctx.hasHeader)
        ctx.tableFlags |= ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate | ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable;

    ctx.footerHeight = (ctx.hasHeader ? ImGui::GetTextLineHeightWithSpacing() : 0.0f) + (ctx.joined ? ImGui::GetStyle().WindowPadding.y : 0.0f);

    ctx.tableId = ImGui::GetID(cItemListTableId);

    EndStaleRowRename();

    ctx.storage = ImGui::GetStateStorage();
    ctx.nameWidthId = ImGui::GetID("NameWidth");
    ctx.nameOffsetId = ImGui::GetID("NameOffset");
    ctx.nameWidth = ctx.storage->GetFloat(ctx.nameWidthId, 0.0f);
    ctx.nameOffset = ctx.storage->GetFloat(ctx.nameOffsetId, ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x);
}

static void CalcItemListWidths(ItemListContext& ctx)
{
    CalcItemColumnAutoWidths(ctx.columns, ctx.columnCount, ctx.listItems, ctx.autoWidths);

    if (ctx.columnCount > 0)
    {
        const ImGuiTable* table = ImGui::TableFindByID(ctx.tableId);
        const bool hasLayout = table && table->ColumnsCount == static_cast<s32>(ctx.nameColumn + 1 + ctx.columnCount);
        const f32 cellPadding = ImGui::GetStyle().CellPadding.x * 2.0f;
        const f32 availWidth = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize;
        const f32 textMin = ImGui::GetFontSize() * cItemTextMinEm;

        f32 idWidth = GetItemIdColumnWidth(ctx.itemType) + cellPadding;

        if (hasLayout)
            idWidth = table->Columns[ctx.idColumn].IsEnabled ? table->Columns[ctx.idColumn].WidthRequest + cellPadding : 0.0f;

        if (ctx.actionCallback)
            idWidth += GetItemActionColumnWidth() + cellPadding;

        const f32 nameMin = sead::Mathf::max(ImGui::GetFontSize() * cItemNameMinEm, ctx.nameWidth);

        f32 minWidth = nameMin + idWidth;
        f32 textWidth = 0.0f;
        f32 autoTextWidth = 0.0f;
        bool autoText[cItemColumnMax] = {};

        for (u32 i = 0; i < ctx.columnCount; i++)
        {
            if (hasLayout ? !table->Columns[ctx.nameColumn + 1 + i].IsEnabled : IsItemColumnHiddenAtFirst(ctx.columns[i], ctx.hideAll))
                continue;

            ctx.columnWidths[i] = hasLayout ? GetItemColumnLayoutWidth(table, ctx.nameColumn + 1 + i, ctx.autoWidths[i]) : ctx.autoWidths[i];
            minWidth += ctx.columnWidths[i] + cellPadding;

            if (!(ctx.columns[i].flags & cItemColumnFlag_Text))
                continue;

            textWidth += ctx.columnWidths[i];
            autoText[i] = !hasLayout || IsItemColumnAuto(table, ctx.nameColumn + 1 + i);

            if (autoText[i])
                autoTextWidth += ctx.columnWidths[i];
        }

        const f32 nameAuto = CalcItemNameAutoWidth(ctx.listItems) + ctx.nameOffset + ImGui::GetFontSize() + cellPadding;
        const f32 nameNeed = nameAuto - (availWidth - (minWidth - nameMin));

        if (ctx.nameWidth == 0.0f && nameNeed > 0.0f && autoTextWidth > 0.0f)
        {
            const f32 textComfort = ImGui::GetFontSize() * cItemTextComfortEm;

            for (u32 i = 0; i < ctx.columnCount; i++)
            {
                if (!autoText[i])
                    continue;

                const f32 width = sead::Mathf::max(ctx.columnWidths[i] - nameNeed * ctx.columnWidths[i] / autoTextWidth, sead::Mathf::min(textComfort, ctx.columnWidths[i]));

                minWidth -= ctx.columnWidths[i] - width;
                textWidth -= ctx.columnWidths[i] - width;
                ctx.columnWidths[i] = width;
            }
        }

        const f32 textRoom = textWidth - (minWidth + ImGui::GetFontSize() * (cItemNameComfortEm - cItemNameMinEm) - availWidth);

        if (textWidth > 0.0f && textRoom < textWidth)
        {
            for (u32 i = 0; i < ctx.columnCount; i++)
            {
                if (!(ctx.columns[i].flags & cItemColumnFlag_Text) || ctx.columnWidths[i] <= 0.0f)
                    continue;

                const f32 width = sead::Mathf::max(ctx.columnWidths[i] * textRoom / textWidth, sead::Mathf::min(textMin, ctx.columnWidths[i]));

                minWidth -= ctx.columnWidths[i] - width;
                ctx.columnWidths[i] = width;
            }
        }

        const f32 valueWidth = minWidth - nameMin - idWidth;
        const f32 scrollRoom = valueWidth <= textMin * cItemScrollMinRatio ? valueWidth : textMin;

        f32 nameKeep = nameMin;

        if (ctx.nameWidth == 0.0f)
        {
            const f32 nameComfort = sead::Mathf::min(nameAuto, ImGui::GetFontSize() * cItemNameComfortEm);

            nameKeep = sead::Mathf::max(nameMin, sead::Mathf::min(nameComfort, availWidth - idWidth - scrollRoom));
            minWidth += nameKeep - nameMin;
        }

        SetItemTableNameMin(nameKeep);

        if (availWidth < minWidth)
        {
            ctx.innerWidth = minWidth;
            ctx.freezeName = nameKeep + idWidth + scrollRoom <= availWidth || nameKeep + idWidth <= ImGui::GetContentRegionAvail().x * cItemNameFreezeRatio;
            ctx.tableFlags |= ImGuiTableFlags_ScrollX;
        }
    }
}

static void BeginItemListTable(ItemListContext& ctx)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32_BLACK_TRANS);
    ctx.tableOpen = ImGui::BeginTable(cItemListTableId,ctx.nameColumn + 1 + ctx.columnCount, ctx.tableFlags, ImVec2(0.0f, -ctx.footerHeight), ctx.innerWidth);
    ImGui::PopStyleColor();

    if (ctx.tableOpen)
    {
        if (ctx.actionCallback)
            SetupItemActionColumn();

        if (ctx.hasHeader)
            SetupItemIdColumn(ctx.itemType);

        ImGui::TableSetupColumn(messages::item::cColumnName, ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoHide, 0.0f, static_cast<ImGuiID>(ItemColumnId::Name));
        SetupItemColumns(ctx.columns, ctx.columnCount, ctx.hideAll);
    }

    if (ctx.tableOpen && ctx.hasHeader)
    {
        ImGui::TableSetupScrollFreeze(ctx.freezeName ? ctx.nameColumn + 1 : 0, 1);

        const ImGuiTable* table = ImGui::GetCurrentTable();

        for (u32 i = 0; table->IsInitializing && i < ctx.columnCount; i++)
            ctx.columnWidths[i] = table->Columns[ctx.nameColumn + 1 + i].IsUserEnabledNextFrame ? GetItemColumnLayoutWidth(table, ctx.nameColumn + 1 + i, ctx.autoWidths[i]) : 0.0f;

        if (!(ctx.tableFlags & ImGuiTableFlags_ScrollX) && table->InnerWindow && !table->InnerWindow->ScrollbarY)
        {
            for (s32 i = static_cast<s32>(ctx.columnCount) - 1; i >= 0; i--)
            {
                if (ctx.columnWidths[i] <= 0.0f)
                    continue;

                ctx.columnWidths[i] += ImGui::GetStyle().ScrollbarSize;
                SetItemTableEdge(i, ImGui::GetStyle().ScrollbarSize);
                break;
            }
        }

        DrawItemHeadersRow(ctx.columns, ctx.columnCount, ctx.columnWidths);

        ctx.sorted = SortItemRows(&ctx.rows, ImGui::TableGetSortSpecs(), ctx.columns, ctx.columnCount);

        const f32 nameColumnWidth = table->Columns[ctx.nameColumn].WidthGiven;
        ctx.showNameEnd = nameColumnWidth >= ImGui::GetFontSize() * cItemNameEndMinEm && nameColumnWidth < ImGui::GetFontSize() * cItemNameComfortEm;
    }

    ctx.rowNum = static_cast<s32>(ctx.rows.size());
}

static const char* GetPlayingMark(Item* item)
{
    const char* postFix = "";
    if ((item == sSoundPlayer.getPlayingSound() || (!sSoundPlayer.getPlayingSound() && item == sSoundPlayer.getPlayingWaveFile())) && sSoundPlayer.isActive())
    {
        static u32 sAnim = 0;
        static u32 sStep = 2;
        static const u32 cSteps = 3;

        static const char* cAnims[cSteps] = {
            " " ICON_LC_VOLUME,
            " " ICON_LC_VOLUME_1,
            " " ICON_LC_VOLUME_2
        };

        if (!sSoundPlayer.isPause())
        {
            sAnim++;

            if (sAnim >= 20)
            {
                sAnim = 0;
                sStep++;

                if (sStep >= cSteps)
                {
                    sStep = 0;
                }
            }
        }

        postFix = cAnims[sStep];
    }

    return postFix;
}

static void DrawLoopDirtyMark(Item* item, const sead::SafeString& selName, const ImVec2& cursor, f32 nameLeft, f32& nameRight)
{
    if (item->getItemType() != Item::ItemType::WaveFile)
        return;

    WaveFile *waveFile = static_cast<WaveFile *>(item);
    if (!waveFile->getIsLoopDirty())
        return;

    f32 fontSize = ImGui::GetFontSize();
    f32 xPadding = ImGui::GetStyle().FramePadding.x + fontSize * 0.5f;

    nameRight -= xPadding + fontSize * 0.5f;

    f32 textSize = sead::Mathf::min(ImGui::CalcTextSize(selName.cstr()).x, nameRight - nameLeft);
    f32 lineHeight = sead::Mathf::max(sead::Mathf::min(
        ImGui::GetCurrentWindow()->DC.CurrLineSize.y,
        fontSize + ImGui::GetStyle().FramePadding.y * 2.0f),
        fontSize
    );

    ImVec2 pos = ImVec2(nameLeft + textSize + xPadding, cursor.y + lineHeight * 0.67f);
    ImGui::RenderBullet(ImGui::GetWindowDrawList(), pos, ImGui::GetColorU32(ImGuiCol_Text));
}

static void HandleItemRowDoubleClick(ItemListContext& ctx, Item* item, const sead::SafeString& selName, f32 nameLeft, f32 nameRight)
{
    const bool opensFile = item->getItemType() == Item::ItemType::SequenceFile || item->getItemType() == Item::ItemType::BankFile;
    const f32 nameEnd = opensFile ? sead::Mathf::min(nameRight, nameLeft + ImGui::CalcTextSize(selName.cstr()).x) : nameRight;
    const f32 mouseX = ImGui::GetMousePos().x;
    const bool onName = mouseX >= nameLeft && mouseX < nameEnd;
    const bool modifier = shortcuts::Held(shortcuts::Modifier::ToggleSelection) || shortcuts::Held(shortcuts::Modifier::ExtendSelection);

    if (onName && !modifier && CanRenameItem(item))
        BeginItemRename(item, ctx.tableId);
    else if (opensFile)
        OpenFileWindow(item);
}

static void DrawItemRowDragDrop(ItemListContext& ctx, Item* item, const sead::SafeString& name, bool selected, bool isMultiSelected)
{
    if (selected && !ctx.sorted)
    {
        if (ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload("ITEM", &item, sizeof(item));

            if (isMultiSelected)
            {
                sead::FormatFixedSafeString<64> dragLabel(messages::item::cMovingItemsFormat,sMultiSelectedItems.size());
                ImGui::Text("%s", dragLabel.cstr());
            }
            else
            {
                ImGui::Text(messages::item::cMovingItemFormat,name.cstr());
            }

            ImGui::EndDragDropSource();
        }
    }

    if (!ctx.sorted)
    {
        ImGui::PushStyleColor(ImGuiCol_DragDropTarget, IM_COL32(0, 0, 0, 0));

        if (ImGui::BeginDragDropTarget())
        {
            ImVec2 itemTL = ImGui::GetItemRectMin();
            ImVec2 itemBR = ImGui::GetItemRectMax();
            float offset = ImGui::GetStyle().ItemSpacing.y * 0.5f;

            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(itemTL.x, itemBR.y + offset),
                ImVec2(itemBR.x, itemBR.y + offset + 3.0f),
                IM_COL32(255, 200, 0, 220));

            const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ITEM");
            if (payload)
            {
                Item* data = *((Item**)payload->Data);
                SEAD_ASSERT(data);
                SEAD_ASSERT(data != item);

                if (data->list() == item->list())
                {
                    ctx.reorder = true;
                    ctx.reorderSource = data;
                    ctx.reorderTarget = item;
                }
            }

            ImGui::EndDragDropTarget();
        }

        ImGui::PopStyleColor();
    }
}

static void BeginItemListRow(ItemListContext& ctx, Item* item)
{
    ImGui::TableNextRow();

    if (ctx.actionCallback && ImGui::TableSetColumnIndex(ctx.actionColumn))
    {
        BeginItemActionCell();
        ctx.actionCallback(item);
        EndItemActionCell();
    }

    if (ctx.hasHeader && ImGui::TableSetColumnIndex(ctx.idColumn))
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(GetItemValueColor(), "%u", GetItemListId(item));
    }

    ImGui::TableSetColumnIndex(ctx.nameColumn);

    ImGui::AlignTextToFramePadding();
}

static void CalcItemRowName(ItemListContext& ctx, Item* item, const char* namePrefix, ItemRowName* row)
{
    row->name = item->getFormattedName();

    if (ctx.hasHeader)
        FormatItemListName(item, &row->name);

    const char* playingMark = GetPlayingMark(item);

    row->isModified = item->isModifiedSinceBaseline();
    row->lead.format("%s%s", namePrefix, row->isModified ? ICON_LC_DOT : "");

    row->cursor = ImGui::GetCursorScreenPos();
    row->left = row->cursor.x + ImGui::CalcTextSize(row->lead.cstr()).x;
    ctx.nameOffset = row->left - ImGui::GetCurrentTable()->Columns[ctx.nameColumn].WorkMinX;
    row->top = row->cursor.y + ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset;
    row->right = row->cursor.x + ImGui::GetContentRegionAvail().x;

    row->text.format("%s%s", row->name.cstr(), playingMark);
}

static bool DrawItemRowSelectable(Item* item, bool selected, bool renaming, bool* outHovered)
{
    if (renaming)
        ImGui::SetNextItemAllowOverlap();

    return DrawTableRowSelectable(sead::FormatFixedSafeString<32>("###%p", item).cstr(), selected, outHovered);
}

static void DrawItemRowName(const ItemListContext& ctx, Item* item, const ItemRowName& row, bool rowHovered, bool renaming)
{
    const bool notLoaded = IsItemNotLoaded(item);

    if (notLoaded)
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

    ImGui::GetWindowDrawList()->AddText(ImVec2(row.cursor.x, row.top), ImGui::GetColorU32(ImGuiCol_Text), row.lead.cstr());

    if (!renaming)
        DrawSelectorName(ImGui::GetItemID(), row.text.cstr(), ImVec2(row.left, row.top), ImVec2(row.right, row.top + ImGui::GetFontSize()), rowHovered, ctx.showNameEnd);

    if (notLoaded)
        ImGui::PopStyleColor();
}

static void DrawItemListRow(ItemListContext& ctx, Item* item)
{
    Item*& selectedItem = *ctx.selection;

    BeginItemListRow(ctx, item);

    const char* namePrefix = ctx.nameCallback ? ctx.nameCallback(item) : nullptr;

    const bool highlightOnly = ctx.highlightItem && item == ctx.highlightItem && selectedItem != item;
    const bool dimHeader = highlightOnly || (!ctx.isSubWindow && sSelectedItemIsSubWindow);

    if (dimHeader)
    {
        ImVec4 headerColor = ImGui::GetStyleColorVec4(ImGuiCol_Header);
        headerColor.w *= cItemDimHeaderAlpha;
        ImGui::PushStyleColor(ImGuiCol_Header, headerColor);
    }

    ItemRowName row;
    CalcItemRowName(ctx, item, namePrefix ? namePrefix : "", &row);

    const bool isMultiSelected = IsItemMultiSelected(item);
    const bool selected = selectedItem == item || isMultiSelected || highlightOnly;
    const bool renaming = GetRenamedItem(ctx.tableId) == item;

    bool rowHovered = false;

    if (DrawItemRowSelectable(item, selected, renaming, &rowHovered))
        SelectItemListRow(ctx, item);

    DrawLoopDirtyMark(item, row.text, row.cursor, row.left, row.right);
    DrawItemRowName(ctx, item, row, rowHovered, renaming);

    if (dimHeader)
        ImGui::PopStyleColor();

    if (rowHovered && !renaming && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        HandleItemRowDoubleClick(ctx, item, row.text, row.left, row.right);

    const bool detailHovered = rowHovered && IsItemHoveredForDetail();

    if (detailHovered && row.isModified)
        ImGui::SetTooltip("%s", messages::item::cModifiedTooltip);

    if (sScrollItem == item)
    {
        sScrollItem = nullptr;
        ImGui::ScrollToItem(ImGuiScrollFlags_KeepVisibleCenterY);
    }

    if (DrawItemContextMenu(ctx, item))
        ctx.add = true;

    DrawItemRowDragDrop(ctx, item, row.name, selected, isMultiSelected);

    if (renaming)
    {
        const f32 framePadding = ImGui::GetStyle().FramePadding.x;

        ImGui::SetCursorScreenPos(ImVec2(row.left - framePadding, row.cursor.y));
        ImGui::SetNextItemWidth(row.right - row.left + framePadding);

        ctx.renameResult = DrawItemRenameField(item);
    }

    DrawItemColumnCells(item, ctx.columns, ctx.columnCount, detailHovered);
}

static void IncludeItemRow(ImGuiListClipper& clipper, const ItemListContext& ctx, const Item* item)
{
    if (!item)
        return;

    const auto it = std::find(ctx.rows.begin(), ctx.rows.end(), item);

    if (it != ctx.rows.end())
        clipper.IncludeItemByIndex(static_cast<s32>(it - ctx.rows.begin()));
}

static void DrawItemListRows(ItemListContext& ctx)
{
    if (!ctx.tableOpen)
        return;

    ImGuiListClipper clipper;

    clipper.Begin(ctx.rowNum);

    IncludeItemRow(clipper, ctx, sScrollItem);
    IncludeItemRow(clipper, ctx, GetRenamedItem(ctx.tableId));

    while (clipper.Step())
    {
        for (s32 row = clipper.DisplayStart; row < clipper.DisplayEnd; row++)
            DrawItemListRow(ctx, ctx.rows[row]);
    }
}

static void EndItemListTable(ItemListContext& ctx)
{
    if (!ctx.tableOpen)
        return;

    if (ctx.rowNum == 0 && ctx.list.size() > 0)
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(ctx.nameColumn);
        ImGui::TextDisabled(messages::item::cNoItemsFormat, ctx.listName);
    }

    if (EndItemTable(ctx.columns, ctx.columnCount, *ctx.fitRows, ctx.autoWidths, &ctx.nameWidth))
    {
        ctx.nameWidth = 0.0f;

        for (Item* item : ctx.rows)
        {
            const char* namePrefix = ctx.nameCallback ? ctx.nameCallback(item) : nullptr;
            sead::FixedSafeString<256> name;
            FormatItemListName(item, &name);

            const sead::FormatFixedSafeString<512> text("%s%s%s", namePrefix ? namePrefix : "", item->isModifiedSinceBaseline() ? ICON_LC_DOT : "", name.cstr());

            ctx.nameWidth = sead::Mathf::max(ctx.nameWidth, ImGui::CalcTextSize(text.cstr()).x);
        }

        ctx.nameWidth += ctx.nameOffset + ImGui::GetFontSize() + ImGui::GetStyle().CellPadding.x * 2.0f;
    }

    ctx.storage->SetFloat(ctx.nameWidthId, ctx.nameWidth);
    ctx.storage->SetFloat(ctx.nameOffsetId, ctx.nameOffset);
}

void DrawAllItemsUI(const char* id, const char *listName, Item::List &list, CreateItemCallback createCallback, ItemNamePrefixCallback nameCallback, ContextMenuCallback menuCallback, ItemFilterCallback filterCallback, bool disableAddWindow, ItemInsertCallback onInsert, ItemRemoveCallback onRemove, bool forceSubWindow, Item *highlightItem)
{
    ItemListContext ctx{list};
    ctx.listName = listName;
    ctx.createCallback = createCallback;
    ctx.nameCallback = nameCallback;
    ctx.menuCallback = menuCallback;
    ctx.filterCallback = filterCallback;
    ctx.onInsert = onInsert;
    ctx.onRemove = onRemove;
    ctx.highlightItem = highlightItem;
    ctx.disableAddWindow = disableAddWindow;
    ctx.isSubWindow = forceSubWindow;

    CollectItemListRows(ctx);
    BeginItemListChild(ctx, id);
    SetupItemListLayout(ctx);

    PushItemTablePadding();
    CalcItemListWidths(ctx);
    BeginItemListTable(ctx);
    HandleItemListKeys(ctx);

    if (ctx.tableOpen && DrawItemContextMenu(ctx, nullptr))
        ctx.add = true;

    DrawItemListRows(ctx);
    EndItemListTable(ctx);

    if (ctx.hasHeader)
        DrawItemCount(ctx.rowNum, static_cast<s32>(ctx.fitRows->size()), ctx.listName, ctx.listNote);

    ImGui::PopStyleVar();

    OpenRenamePopup(ctx.renameResult);
    DrawRenamePopups();

    ApplyItemListEdits(ctx);

    ImGui::EndChild();
}
