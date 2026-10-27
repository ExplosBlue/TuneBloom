#pragma once

#include <bfsar/files/WaveFile.h>

#include <string>

enum class WaveImportResult
{
    None,
    Commit,
    Cancel
};

void DrawWaveImportInfo(WaveFile::Encoding* encoding, WaveFile::RiffWaveInfo* info, const char* header = nullptr);
f32 CalcWaveImportWidth();
f32 CalcWaveImportHeight();
std::string FinalizeImportInfoForCommit(WaveFile::RiffWaveInfo* info);
u32 CalcWaveImportOutputLength(const WaveFile::RiffWaveInfo& info);
void ResetWaveImport();
void WaveImportTick();

bool WaveImportConfirmCancel(bool cancelClicked);
WaveImportResult DrawWaveImportBody(const char* header, WaveFile::Encoding* encoding, WaveFile::RiffWaveInfo* info, const char* commitLabel, const char* footerWarning = nullptr);
