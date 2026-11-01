#pragma once

#include <ui/file/FileDialogs.h>

#include <bfsar/model/Item.h>
#include <bfsar/model/Sound.h>

#include <algorithm>
#include <string>
#include <vector>

class WaveFile;

inline constexpr f32 cExportDialogWidthEm = 27.0f;
inline constexpr f32 cExportLabelWidthEm  = 5.5f;

void SanitizeFilenameInPlace(sead::BufferedSafeString *s);
void ResolveExportBaseName(const Item *item, const char *fallbackLabel, sead::BufferedSafeString *out);
void BuildDefaultExportPath(sead::BufferedSafeString* outPath, const Sound* sound, const char* ext);
void BuildItemDefaultExportPath(const Item* item, const char* fallbackLabel, const char* ext, sead::BufferedSafeString* out);
void ReplacePathExtension(sead::BufferedSafeString* path, const char* ext);
std::string MakeSiblingPath(const std::string& path, const char* extension);

void BeginExportNameBatch();
void BuildUniqueExportPath(sead::BufferedSafeString *outPath, const char *dir, const sead::SafeString &rawName, const char *ext);
u32 CalcExportIdWidth(u32 maxId);
void PrefixExportId(sead::BufferedSafeString* name, u32 id, u32 idWidth);
void BuildExportFileName(sead::BufferedSafeString* out, const Sound* sound, u32 idWidth);

template <typename T>
u32 CalcExportIdWidth(const std::vector<T*>& items)
{
    u32 maxId = 0;

    for (const T* item : items)
        maxId = std::max<u32>(maxId, item->getId());

    return CalcExportIdWidth(maxId);
}

bool IsExportIdInName();
void DrawExportIdCheck(const char* exampleName, const char* extension);
void BuildExportPathFromDir(sead::BufferedSafeString *outPath, const char *dir, const Sound *sound, const char *ext, u32 idWidth);

const WaveFile* FindStreamExportWave(const Sound::StreamSoundInfo& stream);
bool ExportStreamToWav(Sound::StreamSoundInfo& stream, const char* path, bool multiChannel, u32 loopCount, f32 fadeSec, u32 targetRate, bool loopMarkers);
bool ExportLooseStreamToWav(Sound::StreamSoundInfo& stream, const char* path);

void ShowExportResult(const char* message);
void ShowBatchExportResult(s32 total, s32 failureCount, const char* formatName);
void ShowWavExportResult(bool exported);
void ShowWavBatchExportResult(s32 total, s32 failureCount);
void HideExportResult();

void CancelMidiExport();
void ExportPendingMidi();

struct SoundExportOptions
{
    u32 loopCount = 0;
    f32 fadeSec = 0.0f;
    u32 noEndSecs = 0;
    u32 sampleRate = 0;
    bool multiChannel = true;
    bool splitSeqTracks = false;
    bool loopMarkers = true;
    bool idInName = false;
};

bool StartSoundExport(const std::vector<Sound*>& sounds, const SoundExportOptions& options);
void CancelSoundExportRun();

void BuildTrackExportPath(sead::BufferedSafeString* outPath, const char* path, u32 trackNo);

void DrawShowExportedFilesCheck();
void ShowExportedFiles(const sead::SafeString& path);

bool BeginExportProgressDialog(const char* popupId);
bool DrawExportProgressRows(const char* fileName, s32 fileNo, s32 fileNum, f32 fraction);
void CloseExportProgressDialog(const char* popupId);

struct ExportFileType
{
    const char* extension;
    const char* filterName;
    const char* fallbackName;
    const char* folderTitle;
};

enum class BatchExportStep
{
    Open,
    Export,
    Cancel
};

BatchExportStep DrawBatchExportDialog(const ExportFileType& type, const Item* firstItem, s32 count, const char* exampleName);

template <typename T, typename WriteFn, typename ReportBatchFn>
void ExportItemBatch(const std::vector<T*>& items, const ExportFileType& type, u32 idWidth, WriteFn write, ReportBatchFn reportBatch)
{
    sead::FixedSafeString<512> dirPath;
    if (!SelectFolderDialog(&dirPath, type.folderTitle))
        return;

    BeginExportNameBatch();

    s32 failureCount = 0;

    for (T* item : items)
    {
        sead::FixedSafeString<256> baseName;
        ResolveExportBaseName(item, type.fallbackName, &baseName);
        PrefixExportId(&baseName, item->getId(), idWidth);

        sead::FixedSafeString<512> filePath;
        BuildUniqueExportPath(&filePath, dirPath.cstr(), baseName, type.extension);

        if (!write(item, filePath))
            failureCount++;
    }

    const s32 total = static_cast<s32>(items.size());

    reportBatch(total, failureCount);

    if (failureCount < total)
        ShowExportedFiles(dirPath);
}

template <typename T, typename WriteFn, typename ReportFn, typename ReportBatchFn>
void ExportItemsToFiles(std::vector<T*>* pending, const ExportFileType& type, WriteFn write, ReportFn report, ReportBatchFn reportBatch)
{
    if (pending->empty())
        return;

    if (pending->size() > 1)
    {
        const u32 idWidth = CalcExportIdWidth(*pending);

        sead::FixedSafeString<256> exampleName;
        ResolveExportBaseName(pending->front(), type.fallbackName, &exampleName);
        PrefixExportId(&exampleName, pending->front()->getId(), idWidth);

        const BatchExportStep step = DrawBatchExportDialog(type, pending->front(), static_cast<s32>(pending->size()), exampleName.cstr());

        if (step == BatchExportStep::Open)
            return;

        if (step == BatchExportStep::Export)
            ExportItemBatch(*pending, type, IsExportIdInName() ? idWidth : 0, write, reportBatch);
    }
    else
    {
        T* item = pending->front();

        sead::FixedSafeString<512> defaultPath;
        BuildItemDefaultExportPath(item, type.fallbackName, type.extension, &defaultPath);

        sead::FixedSafeString<512> path;
        if (SaveFileDialogWithExtension(&path, type.filterName, type.extension, defaultPath.cstr()))
            report(write(item, path));
    }

    pending->clear();
}
