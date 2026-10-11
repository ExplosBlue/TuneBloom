#pragma once

#include <bfsar/files/WaveFile.h>

#include <string>

void DrawWaveImportInfo(WaveFile::Encoding* encoding, WaveFile::RiffWaveInfo* info, const char* header = nullptr);
f32 CalcWaveImportWidth();
f32 CalcWaveImportHeight();
std::string FinalizeImportInfoForCommit(WaveFile::RiffWaveInfo* info);
void ResetWaveImport();
void WaveImportTick();

bool WaveImportConfirmCancel(bool cancelClicked);
