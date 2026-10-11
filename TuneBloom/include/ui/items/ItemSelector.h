#pragma once

#include <bfsar/model/Item.h>

#include <imgui/imgui.h>

bool ItemSelector(const char* name, const Item::List& list, Item** item, bool allowNone = false, bool compact = false);
bool ItemSelectorDialogRow(const char* label, const Item::List& list, Item** item, bool allowNone = false);
void DrawSelectorName(ImGuiID id, const char* name, const ImVec2& min, const ImVec2& max, bool hovered, bool showEnd = false);
bool WaveArchiveSelector(const char* name, WaveArchiveType* warcType, Item** warc, const Item::List& warcList);
