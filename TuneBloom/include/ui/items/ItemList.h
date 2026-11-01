#pragma once

#include <bfsar/model/Item.h>

using ItemPropertiesCallback = void (*)(bool clear, Item* item, bool* validate);
using InstanciateItemCallback = Item* (*)();
using CreateItemCallback = InstanciateItemCallback (*)(bool clear);
using ItemNamePrefixCallback = const char* (*)(Item* item);
using ItemRowActionCallback = void (*)(Item* item);
using ContextMenuCallback = void (*)(Item* item);
using ItemFilterCallback = bool (*)(const Item* item);
using ItemInsertCallback = bool (*)(Item* afterItem, Item* newItem);
using ItemRemoveCallback = bool (*)(Item* item);
using ItemListFooterCallback = void (*)();

struct FileDrag;
using ItemListFileDropCallback = bool (*)(const FileDrag& drag, bool isDropped);

struct ItemColumn;

void SetNextItemListNote(const char* note);
void SetNextItemListColumns(const ItemColumn* columns, u32 columnCount, bool hideAll = false);
void SetNextItemListAction(ItemRowActionCallback callback);
void SetNextItemListOpen(ItemRowActionCallback callback);
void SetNextItemListAddDialogWidth(f32 width);
void SetNextItemListFixedOrder();
void SetNextItemListRemovable(bool removable);
void SetNextItemListDeselectable();
void SetNextItemListAddMenu(ContextMenuCallback callback);
void SetNextItemListFooter(ItemListFooterCallback callback, f32 width);
void SetNextItemListRemoveText(const char* label, const char* manyFormat, bool removesData);
void SetNextItemListFileDrop(ItemListFileDropCallback callback, const char* action, bool opensAddDialog = false);

void DrawAllItemsUI(const char* id, const char *listName, Item::List &list, CreateItemCallback createCallback = nullptr, ItemNamePrefixCallback nameCallback = nullptr, ContextMenuCallback menuCallback = nullptr, ItemFilterCallback filterCallback = nullptr, bool disableAddWindow = false, ItemInsertCallback onInsert = nullptr, ItemRemoveCallback onRemove = nullptr, bool forceSubWindow = false, Item *highlightItem = nullptr);
void DrawItemPropertiesUI(bool labelLeft = false);
void DrawItemNameRow(Item* item);
void ApplyRenameShortcut();
bool CanRenameItem(const Item* item);
bool OffersItemDuplicate(const Item* item);
bool CanDuplicateSelection(const Item* item, const char** outReason = nullptr);
void RequestSelectionDuplicate(Item* item);

Item* GetInsertAfterItem();
void ClearInsertAfterItem();

InstanciateItemCallback CreateItemFunc(bool clear, InstanciateItemCallback instanciateItemCallback, ItemPropertiesCallback itemPropertiesCallback);
void SuggestCreateItemName(const sead::SafeString& name);
void SetNextCreateItemHead(void (*draw)());
void SetNextCreateItemNameTail(void (*draw)(), f32 width);
const sead::SafeString& GetCreateItemName();

void WarningPopup(const char* name, const char* content);

void DupeNamePopup(const char* name = "###Dupe");
