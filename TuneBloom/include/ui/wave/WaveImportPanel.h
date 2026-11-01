#pragma once

#include <aal/OldAudioData.h>
#include <bfsar/files/WaveFile.h>

#include <string>

struct DecodedPcm;

enum class WaveImportResult
{
    None,
    Commit,
    Cancel
};

enum class WaveImportMode
{
    Import,
    Replace,
    Reimport
};

struct WaveImportModeTexts
{
    const char* icon = nullptr;
    const char* commitLabel = nullptr;
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
    u32 fixedSampleRate = 0;
    const char* fixedReason = nullptr;
};

WaveImportModeTexts GetWaveImportModeTexts(WaveImportMode mode);
WaveImportEncoding MakeWaveImportEncoding(WaveFile::Encoding* encoding);
void DrawWaveImportSource(const WaveFile::RiffWaveInfo& info, const char* name = nullptr);
void DrawWaveImportInfo(const WaveImportEncoding& encoding, WaveFile::RiffWaveInfo* info, const char* sourceName = nullptr);
void DrawWaveImportInfo(WaveFile::Encoding* encoding, WaveFile::RiffWaveInfo* info, const char* sourceName = nullptr);
void DrawWaveImportFormInfo(const WaveImportEncoding& encoding, WaveFile::RiffWaveInfo* info);
f32 CalcWaveImportWidth();
f32 CalcWaveImportHeight();
std::string FinalizeImportInfoForCommit(WaveFile::RiffWaveInfo* info);

class ImportCommit
{
public:
    explicit ImportCommit(WaveFile::RiffWaveInfo* info);
    ~ImportCommit();

    ImportCommit(const ImportCommit&) = delete;
    ImportCommit& operator=(const ImportCommit&) = delete;

private:
    std::string mTempPath;
};

bool ReadImportedWave(WaveFile* wave, WaveFile::RiffWaveInfo* info, WaveFile::Encoding encoding);
WaveFile* CreateImportedArchiveWave(WaveFile::RiffWaveInfo* info, WaveFile::Encoding encoding, const sead::SafeString& name);
bool WriteReimportSource(const DecodedPcm& pcm, Item* item, WaveFile::RiffWaveInfo* out);
void RemoveReimportSource();
void ResetWaveImport();
void WaveImportTick();
bool IsWaveImportDrawn();

bool WaveImportConfirmCancel(bool cancelClicked);
void SetNextWaveImportOldData(const OldAudioData* data, OldAudioClear* cleared);
WaveImportResult DrawWaveImportBody(const char* sourceName, const WaveImportEncoding& encoding, WaveFile::RiffWaveInfo* info, const char* commitLabel, const char* footerWarning = nullptr, bool resetOnAppear = true);
WaveImportResult DrawWaveImportBody(const char* sourceName, WaveFile::Encoding* encoding, WaveFile::RiffWaveInfo* info, const char* commitLabel, const char* footerWarning = nullptr);
