#pragma once

#include <ui/items/ItemColumns.h>
#include <ui/items/ItemList.h>
#include <ui/items/Selection.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <vector>

struct ItemRemoveText
{
    const char* label = nullptr;
    const char* manyFormat = nullptr;
    bool removesData = true;
};

enum class ItemRenameResult
{
    None,
    Renamed,
    Empty,
    Invalid,
    Duplicate
};

struct ItemListContext
{
    Item::List& list;
    const char* listName = nullptr;
    CreateItemCallback createCallback = nullptr;
    ItemNamePrefixCallback nameCallback = nullptr;
    ContextMenuCallback menuCallback = nullptr;
    ItemFilterCallback filterCallback = nullptr;
    ItemInsertCallback onInsert = nullptr;
    ItemRemoveCallback onRemove = nullptr;
    Item* highlightItem = nullptr;
    bool disableAddWindow = false;
    bool isSubWindow = false;

    const char* listNote = nullptr;
    ItemRemoveText removeText;
    ItemRowActionCallback actionCallback = nullptr;
    ItemListFooterCallback footerCallback = nullptr;
    f32 footerWidth = 0.0f;

    std::vector<Item*> listItems;
    std::vector<Item*> rows;
    std::vector<Item*> allRows;
    const std::vector<Item*>* fitRows = nullptr;
    s32 rowNum = 0;

    const ItemColumn* columns = nullptr;
    u32 columnCount = 0;
    bool hideAll = false;
    Item::ItemType itemType = Item::ItemType::Invalid;
    bool hasHeader = false;
    bool hasFooter = false;
    bool joined = false;
    s32 actionColumn = -1;
    s32 idColumn = -1;
    s32 nameColumn = 0;

    Item** selection = nullptr;
    bool listFocused = false;

    ImGuiID tableId = 0;
    ImGuiTableFlags tableFlags = 0;
    bool tableOpen = false;
    bool sorted = false;
    bool showNameEnd = false;
    bool freezeName = false;
    f32 footerHeight = 0.0f;
    f32 innerWidth = 0.0f;
    f32 columnWidths[cItemColumnMax] = {};
    f32 autoWidths[cItemColumnMax] = {};
    bool collapsed[cItemColumnMax] = {};

    ImGuiStorage* storage = nullptr;
    ImGuiID nameWidthId = 0;
    ImGuiID nameOffsetId = 0;
    f32 nameWidth = 0.0f;
    f32 nameOffset = 0.0f;

    ItemRenameResult renameResult = ItemRenameResult::None;
    bool add = false;
    bool reorder = false;
    Item* reorderSource = nullptr;
    Item* reorderTarget = nullptr;
};

void ScrollItemListTo(Item* item);
Item* GetItemListScrollTarget();

void SetSelectionInSubWindow(bool isSubWindow);
void PruneFilteredSelection(const std::vector<Item*>& listItems, ItemFilterCallback filterCallback);
void HandleItemListKeys(ItemListContext& ctx);
void SelectItemListRow(ItemListContext& ctx, Item* item);

bool DrawItemContextMenu(ItemListContext& ctx, Item* item);

void RequestItemDelete(Item* item);
void RequestItemsDelete(const std::vector<Item*>& items);
void RequestSelectionDuplicate(Item* sound);
void SetInsertAfterItem(Item* item);
void ForgetItemListRequests(const std::vector<Item*>& removed);
void ApplyItemListEdits(ItemListContext& ctx);

bool CanRenameItem(const Item* item);
ItemRenameResult RenameItem(Item* item, const sead::FixedSafeString<256>& name);
void OpenRenamePopup(ItemRenameResult result);
void DrawRenamePopups();
void BeginItemRename(Item* item, ImGuiID table);
ItemRenameResult DrawItemRenameField(Item* item);
Item* GetRenamedItem(ImGuiID table);

bool CanEditItemListId(const Item* item);
void BeginItemListIdEdit(Item* item, ImGuiID table);
void DrawItemListIdInput(Item* item);
Item* GetItemListIdEditedItem(ImGuiID table);

Sound* CloneSound(Sound* src);
