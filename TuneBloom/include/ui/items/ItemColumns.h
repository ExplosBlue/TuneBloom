#pragma once

#include <bfsar/model/Item.h>
#include <bfsar/model/Sound.h>

#include <vector>

struct ImGuiTable;
struct ImGuiTableSortSpecs;

enum ItemColumnFlags : u32
{
    cItemColumnFlag_None     = 0,
    cItemColumnFlag_Text     = 1 << 0,
    cItemColumnFlag_WarnZero = 1 << 1,
    cItemColumnFlag_TextEnd  = 1 << 3,
    cItemColumnFlag_Hidden   = 1 << 4
};

struct ItemColumn
{
    const char* key;
    const char* title;
    const char* widthSample;
    void (*format)(const Item* item, sead::BufferedSafeString* out);
    s64 (*sortKey)(const Item* item);
    u32 flags;
    void (*tooltip)(const Item* item);
};

enum class ItemColumnId : u32
{
    Id,
    Name,
    Action,
    First
};

inline constexpr u32 cItemColumnMax = 8;
inline constexpr f32 cItemNameMinEm = 5.0f;

const char* GetItemTypeName(Item::ItemType itemType);
const ItemColumn* GetItemColumns(Item::ItemType itemType, u32* outCount);
const ItemColumn* GetSoundTypeColumns(Sound::SoundType soundType, u32* outCount);

f32 GetItemColumnWidth(const ItemColumn& column);
void RegisterItemColumnTexts();
void RegisterItemColumnSettings();
u32 GetItemListId(const Item* item);
f32 GetItemIdColumnWidth(Item::ItemType itemType);
void SetupItemIdColumn(Item::ItemType itemType);
f32 GetItemActionColumnWidth();
void SetupItemActionColumn();
void BeginItemActionCell();
ImVec2 GetItemActionCellSize();
void EndItemActionCell();
void FormatItemListName(const Item* item, sead::BufferedSafeString* out);
void SetupItemColumns(const ItemColumn* columns, u32 columnCount, bool hideAll = false);
bool IsItemColumnHiddenAtFirst(const ItemColumn& column, bool hideAll = false);
void CalcItemColumnAutoWidths(const ItemColumn* columns, u32 columnCount, const std::vector<Item*>& rows, f32* outWidths);
f32 CalcItemNameAutoWidth(const std::vector<Item*>& rows);
bool IsItemColumnAuto(const ImGuiTable* table, s32 index);
f32 GetItemColumnLayoutWidth(const ImGuiTable* table, s32 index, f32 autoWidth);
void SetItemTableEdge(s32 column, f32 inset);
void SetItemTableNameMin(f32 width);
void DrawItemHeadersRow(const ItemColumn* columns, u32 columnCount, const f32* widths = nullptr);
bool EndItemTable(const ItemColumn* columns, u32 columnCount, const std::vector<Item*>& rows, const f32* autoWidths, f32* nameWidth = nullptr);
void DrawItemColumnCells(const Item* item, const ItemColumn* columns, u32 columnCount, bool rowHovered = false);
void DrawTextEnd(ImDrawList* drawList, const ImVec2& pos, f32 maxX, const char* text, ImU32 color, bool alignRight = false);
bool IsItemNotLoaded(const Item* item);
ImVec4 GetItemValueColor();
void PushItemTablePadding();
void DrawItemCount(s32 shown, s32 total, const char* typeName, const char* note = nullptr);
bool SortItemRows(std::vector<Item*>* rows, const ImGuiTableSortSpecs* sortSpecs, const ItemColumn* columns, u32 columnCount);

void FormatItemSize(sead::BufferedSafeString* out, u32 size);
void FormatWaveFileLength(const Item* item, sead::BufferedSafeString* out);
void FormatWaveFileChannels(const Item* item, sead::BufferedSafeString* out);
