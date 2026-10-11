#pragma once

#include <bfsar/model/Item.h>

void OpenItemBrowser(const char* id, const Item::List& list, Item* current);
bool IsItemBrowserOpen(const char* id);
bool DrawItemBrowser(const char* id, const Item::List& list, Item** itemPtr, bool allowNone, const char* title = nullptr, const char* acceptLabel = nullptr);
