#pragma once

#include <ui/items/ItemList.h>

struct SearchFieldExtra
{
    bool* menuPressed = nullptr;
    bool menuActive = false;
    const char* hint = nullptr;
    const char* icon = nullptr;
    bool* wholeWord = nullptr;
    bool* enterPressed = nullptr;
};

bool SearchField(const char* id, char* buffer, size_t bufferSize, f32 width = 0.0f, bool* caseSensitive = nullptr, bool setFocus = false, const SearchFieldExtra* extra = nullptr);
bool ListSearchField(const char* id, char* buffer, size_t bufferSize, f32 width = 0.0f, bool* caseSensitive = nullptr, bool setFocus = false, const SearchFieldExtra* extra = nullptr);
f32 BeginListRow();

void CloseFilter();
void SetItemTextFilterEnabled(bool enabled);

bool ItemMatchesFilter(const Item *item);
ItemFilterCallback GetItemFilterCallback();
void DrawTabFilterBar(const SearchFieldExtra* extra = nullptr);
bool BeginListOptions(bool active);

bool ItemMatchesDetailFilter(const Item *item);
ItemFilterCallback GetDetailFilterCallback();
void DrawDetailTabFilterBar();
