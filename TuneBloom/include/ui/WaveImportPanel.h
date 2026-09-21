#pragma once

#include <string>

#include <bfsar/WaveFile.h>

void DrawWaveImportInfo(WaveFile::Encoding* encoding, WaveFile::RiffWaveInfo* info);
std::string FinalizeImportInfoForCommit(WaveFile::RiffWaveInfo* info);
bool WaveImportModified();
void ResetWaveImport();
void WaveImportTick();

bool WaveImportConfirmCancel(bool cancelClicked);
