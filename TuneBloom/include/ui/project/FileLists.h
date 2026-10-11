#pragma once

#include <bfsar/model/Item.h>

#include <prim/seadSafeString.h>

#include <vector>

class BankFile;
class WaveFile;

void AddFileToList(Item::List& list, Item* file);

void DrawWaveFilesUI();
void DrawSequenceFilesUI();
void DrawBankFilesUI();

void SequenceFileContextMenuFunc(Item* item);
void BankFileContextMenuFunc(Item* item);
void DrawBankFileMenuItems(bool hasItem, const std::vector<BankFile*>& bankFiles);

void RequestWaveFileReplace(WaveFile* wave);
void RequestWaveFileReimport(WaveFile* wave);
void DrawWaveFileReplacePopup();

extern sead::FixedSafeString<512> sDroppedWavPath;
extern Item* sWavDropTargetVel;

void DrawWavRegionDropImport();
