#pragma once

#include <basis/seadTypes.h>

#include <vector>

class BankFile;
class Item;

void RequestExportBankBundles(const std::vector<BankFile*>& bankFiles);
void RequestImportBankBundle();
void RequestExportInstrument(Item* instrument);
void RequestImportInstrument(Item* targetBank);
void RequestReplaceInstrument(Item* instrument, Item* bank);
void CancelBankBundleTransfers();

void ExportPendingBankBundles();
void ExportPendingInstrument();
bool BeginInstrumentImport();
void DrawInstrumentImportDialog();
bool BeginBankBundleImport();
bool DrawBankBundleImportDialog();

void PatchWaveFileHeader(u8* waveData);
