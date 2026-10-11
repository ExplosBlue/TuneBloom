#pragma once

#include <ui/items/ItemList.h>

#include <vector>

class BankFile;
class Group;

void DrawBanksUI();
void DrawBankPropertiesUI();
void BankContextMenuFunc(Item *item);
std::vector<BankFile*> CollectBankFilesFromBanks(Item* item);

void DrawWaveArchivesUI();
void DrawWaveArchivePropertiesUI();

void DrawGroupsUI();
void DrawGroupPropertiesUI();
InstanciateItemCallback CreateGroupFunc(bool clear);
void DrawGroupFileRow(const Group* group);
void DrawGroupContextMenu(Item* item);
void HandleGroupFileRequests();
void NotifyDeletedGroupFiles();
