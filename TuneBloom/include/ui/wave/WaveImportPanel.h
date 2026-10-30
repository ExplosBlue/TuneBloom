#pragma once

#include <aal/OldAudioData.h>
#include <bfsar/files/WaveFile.h>

#include <string>

enum class WaveImportResult
{
    None,
    Commit,
    Cancel
};

struct WaveImportEncoding
{
    s32* value = nullptr;
    const char* const* names = nullptr;
    const char* const* details = nullptr;
    const char* const* reasons = nullptr;
    s32 count = 0;
    bool (*acceptsSampleRate)(s32 value, u32 sampleRate) = nullptr;
    const char* sampleRateReason = nullptr;
    u32* bitrate = nullptr;
    s32 bitrateValue = -1;
    u32 originalBitrate = 0;
};

WaveImportEncoding MakeWaveImportEncoding(WaveFile::Encoding* encoding);
void DrawWaveImportInfo(const WaveImportEncoding& encoding, WaveFile::RiffWaveInfo* info, const char* header = nullptr);
void DrawWaveImportInfo(WaveFile::Encoding* encoding, WaveFile::RiffWaveInfo* info, const char* header = nullptr);
f32 CalcWaveImportWidth();
f32 CalcWaveImportHeight();
std::string FinalizeImportInfoForCommit(WaveFile::RiffWaveInfo* info);
void ResetWaveImport();
void WaveImportTick();

bool WaveImportConfirmCancel(bool cancelClicked);
void SetNextWaveImportOldData(const OldAudioData* data, OldAudioClear* cleared);
WaveImportResult DrawWaveImportBody(const char* header, const WaveImportEncoding& encoding, WaveFile::RiffWaveInfo* info, const char* commitLabel, const char* footerWarning = nullptr);
WaveImportResult DrawWaveImportBody(const char* header, WaveFile::Encoding* encoding, WaveFile::RiffWaveInfo* info, const char* commitLabel, const char* footerWarning = nullptr);
