#include <ui/items/ItemColumns.h>

#include <ui/widgets/ContextMenu.h>
#include <ui/items/ItemColumnSettings.h>
#include <ui/Messages.h>
#include <ui/settings/Theme.h>
#include <ui/widgets/Widgets.h>

#include <bfsar/files/BankFile.h>
#include <bfsar/model/Sound.h>
#include <bfsar/player/SoundPlayer.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

static constexpr f32 cItemColumnGap       = 8.0f;
static constexpr f32 cItemValueBrightness = 0.45f;
static constexpr s32 cItemAutoWidthFrames = 60;
static constexpr f32 cItemNameAutoRatio   = 0.95f;
static constexpr f32 cItemCellPadXEm      = 0.75f;
static constexpr f32 cItemTextAutoEm      = 18.0f;
static constexpr const char* cItemTextEndMark = "...";

static constexpr const char* cItemIdColumnKey      = "ID";
static constexpr const char* cItemProgramColumnKey = "Program";

static constexpr ImVec4 cUnusedColor = cWarningTextColor;

static s32 sEdgeColumn = -1;
static f32 sEdgeInset  = 0.0f;
static f32 sNameMin    = 0.0f;

static const char* sIdColumnKey = nullptr;

static f32 GetItemSortMarkWidth()
{
    return ImGui::CalcTextSize(ICON_LC_CHEVRON_DOWN).x + ImGui::GetStyle().ItemInnerSpacing.x;
}

void RegisterItemColumnTexts()
{
    static const ImGuiLocEntry cEntries[] = {
        { ImGuiLocKey_TableSizeAllDefault,           messages::item::cSizeAllColumns },
        { ImGuiLocKey_DockingDragToUndockOrMoveNode, messages::app::cDockMoveTip },
        { ImGuiLocKey_DockingHoldShiftToDock,        messages::app::cDockShiftTip }
    };

    ImGui::LocalizeRegisterEntries(cEntries, IM_ARRAYSIZE(cEntries));
}

f32 GetItemColumnWidth(const ItemColumn& column)
{
    const f32 titleWidth  = ImGui::CalcTextSize(column.title).x + GetItemSortMarkWidth();
    const f32 sampleWidth = ImGui::CalcTextSize(column.widthSample).x;

    return sead::Mathf::max(titleWidth, sampleWidth) + cItemColumnGap;
}

u32 GetItemListId(const Item* item)
{
    if (item->getItemType() == Item::ItemType::BankFileInstrument)
        return static_cast<u32>(static_cast<const BankFile::Instrument*>(item)->getProgramNo());

    return item->getId();
}

static const char* GetItemIdColumnTitle(Item::ItemType itemType)
{
    return itemType == Item::ItemType::BankFileInstrument ? messages::item::cColumnProgram : messages::item::cColumnId;
}

f32 GetItemIdColumnWidth(Item::ItemType itemType)
{
    const f32 titleWidth = ImGui::CalcTextSize(GetItemIdColumnTitle(itemType)).x + GetItemSortMarkWidth();

    return sead::Mathf::max(ImGui::CalcTextSize("0000").x, titleWidth);
}

void SetupItemIdColumn(Item::ItemType itemType)
{
    ImGuiTable* table = ImGui::GetCurrentTable();
    ImGuiTableColumn& column = table->Columns[table->DeclColumnsCount];
    const f32 width = GetItemIdColumnWidth(itemType);

    ImGui::TableSetupColumn(GetItemIdColumnTitle(itemType), ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, width, static_cast<ImGuiID>(ItemColumnId::Id));

    column.WidthRequest = width;

    sIdColumnKey = itemType == Item::ItemType::BankFileInstrument ? cItemProgramColumnKey : cItemIdColumnKey;
    ApplyItemColumnHidden(column, sIdColumnKey);
}

f32 GetItemActionColumnWidth()
{
    return sead::Mathf::max(GetRowButtonSize() - ImGui::GetStyle().CellPadding.x, 1.0f);
}

void SetupItemActionColumn()
{
    ImGuiTable* table = ImGui::GetCurrentTable();
    ImGuiTableColumn& column = table->Columns[table->DeclColumnsCount];
    const f32 width = GetItemActionColumnWidth();

    ImGui::TableSetupColumn("##Action",ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_NoHeaderLabel, width, static_cast<ImGuiID>(ItemColumnId::Action));

    column.WidthRequest = width;
}

void BeginItemActionCell()
{
    const ImGuiTable* table = ImGui::GetCurrentTable();
    const ImGuiTableColumn& column = table->Columns[table->CurrentColumn];

    ImGui::SetCursorScreenPos(ImVec2(column.MinX, table->RowPosY1));
}

ImVec2 GetItemActionCellSize()
{
    const ImGuiTable* table = ImGui::GetCurrentTable();
    const ImGuiTableColumn& column = table->Columns[table->CurrentColumn];

    return ImVec2(column.MaxX - column.MinX, ImGui::GetFrameHeight() + table->RowCellPaddingY * 2.0f);
}

void EndItemActionCell()
{
    const ImGuiTable* table = ImGui::GetCurrentTable();
    const ImGuiTableColumn& column = table->Columns[table->CurrentColumn];

    ImGui::GetCurrentWindow()->DC.CursorMaxPos = ImVec2(column.WorkMaxX, table->RowPosY1 + table->RowCellPaddingY + ImGui::GetFrameHeight());
}

void FormatItemListName(const Item* item, sead::BufferedSafeString* out)
{
    if (item->getItemType() == Item::ItemType::BankFileInstrument)
    {
        out->copy(item->isNameValid() ? item->getName().cstr() : messages::item::cTypeInstrument);
        return;
    }

    if (item->getItemType() != Item::ItemType::GroupItemInfo)
    {
        out->copy(item->getNameOrNull().cstr());
        return;
    }

    const sead::FixedSafeString<256> name = item->getFormattedName();
    const char* text = strchr(name.cstr(), ' ');

    out->copy(text ? text + 1 : name.cstr());
}

bool IsItemColumnHiddenAtFirst(const ItemColumn& column, bool hideAll)
{
    return hideAll || (column.flags & cItemColumnFlag_Hidden);
}

void SetupItemColumns(const ItemColumn* columns, u32 columnCount, bool hideAll)
{
    SEAD_ASSERT(columnCount <= cItemColumnMax);

    ImGuiTable* table = ImGui::GetCurrentTable();

    for (u32 i = 0; i < columnCount; i++)
    {
        ImGuiTableColumnFlags flags = ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending;

        if (IsItemColumnHiddenAtFirst(columns[i], hideAll))
            flags |= ImGuiTableColumnFlags_DefaultHide;

        ImGuiTableColumn& column = table->Columns[table->DeclColumnsCount];
        const f32 width = GetItemColumnWidth(columns[i]);
        const f32 userEm = GetItemColumnUserWidth(columns[i].key);

        ImGui::TableSetupColumn(columns[i].title, flags, width, static_cast<ImGuiID>(ItemColumnId::First) + i);

        column.WidthRequest = userEm > 0.0f ? ImFloor(userEm * ImGui::GetFontSize()) : width;

        ApplyItemColumnHidden(column, columns[i].key);
    }
}

static f32 CalcItemColumnFitWidth(const ItemColumn& column, const std::vector<Item*>& rows)
{
    f32 textWidth = 0.0f;

    for (const Item* item : rows)
    {
        sead::FixedSafeString<256> text;
        column.format(item, &text);

        textWidth = sead::Mathf::max(textWidth, ImGui::CalcTextSize(text.cstr()).x);
    }

    const f32 titleWidth = ImGui::CalcTextSize(column.title).x + GetItemSortMarkWidth();

    return sead::Mathf::max(titleWidth, textWidth) + cItemColumnGap;
}

static bool UpdateItemWidthCache(ImGuiID id, const std::vector<Item*>& rows)
{
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const s32 frame = ImGui::GetFrameCount();

    const u64 key[] = {
        rows.empty() ? 0 : static_cast<u64>(reinterpret_cast<uintptr_t>(rows.front())),
        rows.empty() ? 0 : static_cast<u64>(reinterpret_cast<uintptr_t>(rows.back())),
        static_cast<u64>(rows.size())
    };
    const s32 hash = static_cast<s32>(ImHashData(key, sizeof(key), static_cast<ImGuiID>(ImGui::GetFontSize())));

    const bool sameRows = storage->GetInt(id, 0) == hash;
    const bool inView = storage->GetInt(id + 1, -1) >= frame - 1;
    const bool recent = frame - storage->GetInt(id + 2, 0) < cItemAutoWidthFrames;

    storage->SetInt(id + 1, frame);

    if (sameRows && inView && recent)
        return false;

    storage->SetInt(id, hash);
    storage->SetInt(id + 2, frame);

    return true;
}

void CalcItemColumnAutoWidths(const ItemColumn* columns, u32 columnCount, const std::vector<Item*>& rows, f32* outWidths)
{
    if (columnCount == 0)
        return;

    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID id = ImGui::GetID(static_cast<const void*>(columns));

    if (UpdateItemWidthCache(id, rows))
    {
        const f32 textMax = ImGui::GetFontSize() * cItemTextAutoEm;

        for (u32 i = 0; i < columnCount; i++)
        {
            f32 width = GetItemColumnWidth(columns[i]);

            if (columns[i].flags & cItemColumnFlag_Text)
                width = sead::Mathf::min(CalcItemColumnFitWidth(columns[i], rows), textMax);

            storage->SetFloat(id + 3 + i, width);
        }
    }

    for (u32 i = 0; i < columnCount; i++)
        outWidths[i] = storage->GetFloat(id + 3 + i, 0.0f);
}

f32 CalcItemNameAutoWidth(const std::vector<Item*>& rows)
{
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID id = ImGui::GetID("NameAutoWidth");

    if (UpdateItemWidthCache(id, rows))
    {
        static std::vector<f32> sWidths;
        sWidths.clear();

        for (const Item* item : rows)
        {
            sead::FixedSafeString<256> name;
            FormatItemListName(item, &name);

            sWidths.push_back(ImGui::CalcTextSize(name.cstr()).x);
        }

        f32 width = 0.0f;

        if (!sWidths.empty())
        {
            const size_t index = static_cast<size_t>(static_cast<f32>(sWidths.size() - 1) * cItemNameAutoRatio);

            std::nth_element(sWidths.begin(), sWidths.begin() + index, sWidths.end());
            width = sWidths[index];
        }

        storage->SetFloat(id + 3, width);
    }

    return storage->GetFloat(id + 3, 0.0f);
}

bool IsItemColumnAuto(const ImGuiTable* table, s32 index)
{
    const ImGuiTableColumn& column = table->Columns[index];

    return column.WidthRequest == column.InitStretchWeightOrWidth;
}

f32 GetItemColumnLayoutWidth(const ImGuiTable* table, s32 index, f32 autoWidth)
{
    return IsItemColumnAuto(table, index) ? autoWidth : table->Columns[index].WidthRequest;
}

void SetItemTableEdge(s32 column, f32 inset)
{
    sEdgeColumn = column;
    sEdgeInset = inset;
}

void SetItemTableNameMin(f32 width)
{
    sNameMin = width;
}

void DrawItemHeadersRow(const ItemColumn* columns, u32 columnCount, const f32* widths)
{
    ImGuiTable* table = ImGui::GetCurrentTable();
    const s32 first = table->ColumnsCount - static_cast<s32>(columnCount);
    const f32 markWidth = GetItemSortMarkWidth();
    const ImU32 markColor = ImGui::GetColorU32(GetItemValueColor());

    f32 userWidths[cItemColumnMax] = {};

    for (u32 i = 0; widths && i < columnCount; i++)
    {
        userWidths[i] = table->Columns[first + i].WidthRequest;

        if (widths[i] > 0.0f)
            table->Columns[first + i].WidthRequest = widths[i];
    }

    ImGui::TableNextRow(ImGuiTableRowFlags_Headers, ImGui::TableGetHeaderRowHeight());

    for (u32 i = 0; widths && i < columnCount; i++)
        table->Columns[first + i].WidthRequest = userWidths[i];

    for (s32 i = 0; i < table->ColumnsCount; i++)
    {
        if (!ImGui::TableSetColumnIndex(i))
            continue;

        const char* title = ImGui::TableGetColumnName(i);
        const ImGuiTableColumn& column = table->Columns[i];
        const bool sorted = column.SortOrder != -1;
        const bool alignLeft = i < first || (columns[i - first].flags & cItemColumnFlag_Text);
        const char* mark = column.SortDirection == ImGuiSortDirection_Ascending ? ICON_LC_CHEVRON_UP : ICON_LC_CHEVRON_DOWN;

        if (column.UserID == static_cast<ImGuiID>(ItemColumnId::Action))
            continue;

        ImGui::PushID(i);

        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const f32 left = i >= first && alignLeft ? pos.x + cItemColumnGap : pos.x;
        const f32 right = pos.x + ImGui::GetContentRegionAvail().x - (alignLeft ? 0.0f : cItemColumnGap) - (i - first == sEdgeColumn ? sEdgeInset : 0.0f);
        const f32 titleWidth = ImGui::CalcTextSize(title).x;
        const f32 bottom = pos.y + ImGui::GetFontSize();

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::TableHeader("");
        ImGui::PopStyleColor();

        ImDrawList* drawList = ImGui::GetWindowDrawList();

        if (alignLeft)
        {
            const f32 titleRight = sorted ? right - markWidth : right;
            ImGui::RenderTextEllipsis(drawList, ImVec2(left, pos.y), ImVec2(titleRight, bottom), titleRight, titleRight, title, nullptr, nullptr);

            if (sorted)
                drawList->AddText(ImVec2(sead::Mathf::min(left + titleWidth, titleRight) + ImGui::GetStyle().ItemInnerSpacing.x, pos.y), markColor, mark);
        }
        else
        {
            const f32 textX = sead::Mathf::max(right - titleWidth, left);
            ImGui::RenderTextEllipsis(drawList, ImVec2(textX, pos.y), ImVec2(right, bottom), right, right, title, nullptr, nullptr);

            if (sorted && textX - markWidth >= left)
                drawList->AddText(ImVec2(textX - markWidth, pos.y), markColor, mark);
        }

        ImGui::PopID();
    }
}

struct ItemTableResize
{
    ImGuiTable* table = nullptr;
    const ItemColumn* columns = nullptr;
    u32 columnCount = 0;
    s32 nameIndex = 0;
    bool scroll = false;
    f32 minWidth = 0.0f;
    f32 nameMin = 0.0f;
    f32 spare = 0.0f;
    f32* nameWidth = nullptr;
};

static void StoreItemColumnsHidden(const ItemTableResize& resize)
{
    ImGuiTable* table = resize.table;

    if (resize.nameIndex > 0 && sIdColumnKey)
        StoreItemColumnHidden(table->Columns[resize.nameIndex - 1], sIdColumnKey);

    for (u32 i = 0; i < resize.columnCount; i++)
        StoreItemColumnHidden(table->Columns[resize.nameIndex + 1 + i], resize.columns[i].key);

    sIdColumnKey = nullptr;
}

static void ApplyItemColumnFitAll(const ItemTableResize& resize)
{
    ImGuiTable* table = resize.table;

    for (s32 i = 0; i < table->ColumnsCount; i++)
    {
        ImGuiTableColumn& column = table->Columns[i];

        if (i == resize.nameIndex)
            continue;

        if (column.AutoFitQueue == 0)
            continue;

        column.AutoFitQueue = 0;
        column.WidthRequest = column.InitStretchWeightOrWidth;
        table->IsSettingsDirty = true;

        if (resize.nameWidth)
            *resize.nameWidth = 0.0f;
    }
}

static bool ApplyItemColumnFit(const ItemTableResize& resize, const std::vector<Item*>& rows, const f32* autoWidths)
{
    ImGuiTable* table = resize.table;
    const s32 nameIndex = resize.nameIndex;

    if (table->AutoFitSingleColumn < nameIndex)
        return false;

    s32 index = table->AutoFitSingleColumn;
    table->AutoFitSingleColumn = -1;

    if (!table->IsContextPopupOpen && !resize.scroll && table->Columns[index].NextEnabledColumn != -1)
        index = table->Columns[index].NextEnabledColumn;

    if (index > nameIndex)
    {
        ImGuiTableColumn& column = table->Columns[index];
        const f32 fitWidth = CalcItemColumnFitWidth(resize.columns[index - nameIndex - 1], rows);

        column.WidthRequest = fitWidth <= autoWidths[index - nameIndex - 1] ? column.InitStretchWeightOrWidth : sead::Mathf::max(fitWidth, resize.minWidth);
        table->IsSettingsDirty = true;
    }

    return index == nameIndex;
}

static void ApplyItemBorderDrag(const ItemTableResize& resize)
{
    static ImGuiID sScrollDragTable = 0;

    ImGuiTable* table = resize.table;
    const s32 nameIndex = resize.nameIndex;
    const bool scroll = resize.scroll;

    if (table->ResizedColumn == -1 && sScrollDragTable == table->ID)
        sScrollDragTable = 0;

    if (table->ResizedColumn < nameIndex || table->ResizedColumnNextWidth == FLT_MAX)
        return;

    ImGuiTableColumn& column = table->Columns[table->ResizedColumn];
    const f32 delta = table->ResizedColumnNextWidth - column.WidthGiven;

    table->ResizedColumnNextWidth = FLT_MAX;

    if (scroll)
        sScrollDragTable = table->ID;

    if (scroll && table->ResizedColumn == nameIndex && resize.nameWidth)
    {
        *resize.nameWidth = sead::Mathf::max(column.WidthGiven + delta, resize.nameMin);
    }
    else if (scroll && table->ResizedColumn > nameIndex)
    {
        column.WidthRequest = sead::Mathf::max(column.WidthGiven + delta, resize.minWidth);
        table->IsSettingsDirty = true;
    }
    else if (!scroll && sScrollDragTable != table->ID && column.NextEnabledColumn != -1)
    {
        ImGuiTableColumn& next = table->Columns[column.NextEnabledColumn];

        const f32 given = next.WidthGiven - (column.NextEnabledColumn == nameIndex + 1 + sEdgeColumn ? sEdgeInset : 0.0f);

        next.WidthRequest = sead::Mathf::clamp2(resize.minWidth, given - delta, given + resize.spare);
        table->IsSettingsDirty = true;

        if (resize.nameWidth)
            *resize.nameWidth = 0.0f;
    }
}

static void StoreItemColumnWidths(const ItemTableResize& resize, const f32* oldWidths)
{
    for (u32 i = 0; i < resize.columnCount; i++)
    {
        const ImGuiTableColumn& column = resize.table->Columns[resize.nameIndex + 1 + i];

        if (column.WidthRequest == oldWidths[i])
            continue;

        SetItemColumnUserWidth(resize.columns[i].key, column.WidthRequest == column.InitStretchWeightOrWidth ? 0.0f : column.WidthRequest / ImGui::GetFontSize());

        ImGui::MarkIniSettingsDirty();
    }
}

bool EndItemTable(const ItemColumn* columns, u32 columnCount, const std::vector<Item*>& rows, const f32* autoWidths, f32* nameWidth)
{
    ItemTableResize resize;
    resize.table = ImGui::GetCurrentTable();
    resize.columns = columns;
    resize.columnCount = columnCount;
    resize.nameWidth = nameWidth;

    DrawTableContextMenu();

    const ImGuiTable* table = resize.table;

    resize.nameIndex = table->ColumnsCount - static_cast<s32>(columnCount) - 1;

    StoreItemColumnsHidden(resize);

    ImGui::EndTable();

    resize.scroll = (table->Flags & ImGuiTableFlags_ScrollX) != 0;
    resize.minWidth = table->MinColumnWidth;
    resize.nameMin = ImGui::GetFontSize() * cItemNameMinEm;
    resize.spare = resize.scroll ? FLT_MAX : sead::Mathf::max(table->Columns[resize.nameIndex].WidthGiven - sead::Mathf::max(resize.nameMin, sNameMin), 0.0f);

    f32 oldWidths[cItemColumnMax] = {};

    for (u32 i = 0; i < columnCount; i++)
        oldWidths[i] = table->Columns[resize.nameIndex + 1 + i].WidthRequest;

    ApplyItemColumnFitAll(resize);

    const bool fitName = ApplyItemColumnFit(resize, rows, autoWidths);

    ApplyItemBorderDrag(resize);
    StoreItemColumnWidths(resize, oldWidths);

    SetItemTableEdge(-1, 0.0f);
    SetItemTableNameMin(0.0f);

    return fitName;
}

void PushItemTablePadding()
{
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ImFloor(ImGui::GetFontSize() * cItemCellPadXEm), ImGui::GetStyle().CellPadding.y));
}

ImVec4 GetItemValueColor()
{
    return ImLerp(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), ImGui::GetStyleColorVec4(ImGuiCol_Text), cItemValueBrightness);
}

void DrawTextEnd(ImDrawList* drawList, const ImVec2& pos, f32 maxX, const char* text, ImU32 color, bool alignRight)
{
    const f32 markWidth = ImGui::CalcTextSize(cItemTextEndMark).x;
    const f32 endWidth = maxX - pos.x - markWidth;
    const char* end = text;

    while (*end != '\0' && ImGui::CalcTextSize(end).x > endWidth)
    {
        end++;

        while ((*end & 0xC0) == 0x80)
            end++;
    }

    const f32 endX = alignRight ? ImFloor(maxX - ImGui::CalcTextSize(end).x) : pos.x + markWidth;

    drawList->AddText(pos, color, cItemTextEndMark);
    drawList->AddText(ImVec2(endX, pos.y), color, end);
}

void DrawItemValueCell(const char* text, u32 flags, s32 index, bool unused)
{
    ImVec2 pos = ImGui::GetCursorScreenPos();
    pos.y += ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset;

    const f32 width = ImGui::GetContentRegionAvail().x - cItemColumnGap - (index == sEdgeColumn ? sEdgeInset : 0.0f);
    const f32 textWidth = ImGui::CalcTextSize(text).x;

    ImGui::PushStyleColor(ImGuiCol_Text, unused ? cUnusedColor : GetItemValueColor());

    if (flags & cItemColumnFlag_Text)
    {
        pos.x += cItemColumnGap;

        if ((flags & cItemColumnFlag_TextEnd) && textWidth > width)
            DrawTextEnd(ImGui::GetWindowDrawList(), pos, pos.x + width, text, ImGui::GetColorU32(ImGuiCol_Text));
        else
            ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), pos, ImVec2(pos.x + width, pos.y + ImGui::GetFontSize()), pos.x + width, pos.x + width, text, nullptr, nullptr);

        ImGui::Dummy(ImVec2(sead::Mathf::min(textWidth, width), ImGui::GetFontSize()));
    }
    else
    {
        if (textWidth < width)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + width - textWidth);

        ImGui::TextUnformatted(text);
    }

    ImGui::PopStyleColor();
}

void DrawItemColumnCells(const Item* item, const ItemColumn* columns, u32 columnCount, bool rowHovered)
{
    for (u32 i = 0; i < columnCount; i++)
    {
        if (!ImGui::TableNextColumn())
            continue;

        sead::FixedSafeString<256> text;
        columns[i].format(item, &text);

        const bool unused = (columns[i].flags & cItemColumnFlag_WarnZero) && columns[i].sortKey(item) == 0;

        DrawItemValueCell(text.cstr(), columns[i].flags, static_cast<s32>(i), unused);

        if (rowHovered && columns[i].tooltip && ImGui::TableGetHoveredColumn() == ImGui::TableGetColumnIndex())
            columns[i].tooltip(item);
    }
}

bool IsItemNotLoaded(const Item* item)
{
    if (item->getItemType() != Item::ItemType::Sound)
        return false;

    const Sound& sound = *static_cast<const Sound*>(item);

    return SoundPlayer::IsStreamFileNotLoaded(sound);
}

void DrawItemCount(s32 shown, s32 total, const char* typeName, const char* note)
{
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetStyle().CellPadding.x);

    if (shown != total)
        ImGui::TextDisabled(messages::item::cCountFormat, shown, total);
    else
        ImGui::TextDisabled(shown == 1 ? messages::item::cCountOneFormat : messages::item::cCountAllFormat, shown, typeName);

    if (note)
    {
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::TextDisabled(messages::item::cCountNoteFormat, note);
    }
}

static s32 CompareNames(const Item* a, const Item* b)
{
    if (a->getItemType() != Item::ItemType::GroupItemInfo && a->getItemType() != Item::ItemType::BankFileInstrument)
        return ImStricmp(a->getNameOrNull().cstr(), b->getNameOrNull().cstr());

    sead::FixedSafeString<256> nameA;
    sead::FixedSafeString<256> nameB;
    FormatItemListName(a, &nameA);
    FormatItemListName(b, &nameB);

    return ImStricmp(nameA.cstr(), nameB.cstr());
}

bool SortItemRows(std::vector<Item*>* rows, const ImGuiTableSortSpecs* sortSpecs, const ItemColumn* columns, u32 columnCount)
{
    static std::vector<std::pair<s64, Item*>> sKeys;

    if (!sortSpecs || sortSpecs->SpecsCount == 0)
        return false;

    const u32 columnId = sortSpecs->Specs[0].ColumnUserID;
    const bool descending = sortSpecs->Specs[0].SortDirection == ImGuiSortDirection_Descending;

    if (columnId == static_cast<u32>(ItemColumnId::Name))
    {
        std::stable_sort(rows->begin(), rows->end(), [descending](const Item* a, const Item* b)
        {
            const s32 order = CompareNames(a, b);
            return descending ? order > 0 : order < 0;
        });

        return true;
    }

    const auto lessId = [](const Item* a, const Item* b)
    {
        return GetItemListId(a) < GetItemListId(b);
    };

    if (columnId == static_cast<u32>(ItemColumnId::Id) && !descending && std::is_sorted(rows->begin(), rows->end(), lessId))
        return false;

    const u32 index = columnId - static_cast<u32>(ItemColumnId::First);

    sKeys.clear();
    sKeys.reserve(rows->size());

    for (Item* item : *rows)
        sKeys.emplace_back(columnId >= static_cast<u32>(ItemColumnId::First) && index < columnCount ? columns[index].sortKey(item) : static_cast<s64>(GetItemListId(item)), item);

    std::stable_sort(sKeys.begin(), sKeys.end(), [descending](const std::pair<s64, Item*>& a, const std::pair<s64, Item*>& b)
    {
        return descending ? a.first > b.first : a.first < b.first;
    });

    for (size_t i = 0; i < sKeys.size(); i++)
        (*rows)[i] = sKeys[i].second;

    return true;
}
