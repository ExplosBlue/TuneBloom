#pragma once

#include <bfsar/model/Item.h>

#include <ui/Messages.h>

using ItemPropertiesCallback = void (*)(bool clear, Item* item, bool* validate);
using InstanciateItemCallback = Item* (*)();
using CreateItemCallback = InstanciateItemCallback (*)(bool clear);
using ItemNamePrefixCallback = const char* (*)(Item* item);
using ItemRowActionCallback = void (*)(Item* item);
using ContextMenuCallback = void (*)(Item* item);
using ItemFilterCallback = bool (*)(const Item* item);
using ItemInsertCallback = void (*)(Item* afterItem, Item* newItem);
using ItemRemoveCallback = void (*)(Item* item);

struct ItemColumn;

void SetNextItemListNote(const char* note);
void SetNextItemListColumns(const ItemColumn* columns, u32 columnCount, bool hideAll = false);
void SetNextItemListAction(ItemRowActionCallback callback);
void SetNextItemListRemoveText(const char* label, const char* manyFormat, bool removesData);

void DrawAllItemsUI(const char* id, const char *listName, Item::List &list, CreateItemCallback createCallback = nullptr, ItemNamePrefixCallback nameCallback = nullptr, ContextMenuCallback menuCallback = nullptr, ItemFilterCallback filterCallback = nullptr, bool disableAddWindow = false, ItemInsertCallback onInsert = nullptr, ItemRemoveCallback onRemove = nullptr, bool forceSubWindow = false, Item *highlightItem = nullptr);
void DrawItemPropertiesUI(bool labelLeft = false);
void DrawItemNameRow(Item* item);
void ApplyRenameShortcut();

Item* GetInsertAfterItem();
void ClearInsertAfterItem();

InstanciateItemCallback CreateItemFunc(bool clear, InstanciateItemCallback instanciateItemCallback, ItemPropertiesCallback itemPropertiesCallback);
void SuggestCreateItemName(const sead::SafeString& name);
const sead::SafeString& GetCreateItemName();

void WarningPopup(const char* name, const char* content);

inline void DupeNamePopup(const char* name = "###Dupe")
{
    WarningPopup(name, messages::item::cNameDuplicated);
}
