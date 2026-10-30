#pragma once

#include <ui/file/LooseFileInfo.h>

class BarsListEntry;
class BarsListFile;
class Item;
struct FoundBars;

const FoundBars* FindEntryBars(const Item* item);
LooseFileFormat GetBarsListFormat(const BarsListFile& file);
void DrawBarsListUI(BarsListFile& file);
void DrawBarsListInfoUI(const BarsListFile& file);
void DrawBarsListEntryPropertiesUI(BarsListEntry* entry);
