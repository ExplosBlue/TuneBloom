#pragma once

#include <bfsar/model/Item.h>

class Sound;

const char* GetSoundIcon(const Sound* sound);
const char* GetItemIcon(const Item* item);
bool DrawReferencesUI(Item* item, f32 reserveBelow = 0.0f);
void DrawArchiveInfoUI(Item::ItemType itemType);
