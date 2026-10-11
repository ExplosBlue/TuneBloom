#pragma once

#include <ui/file/FileDialogs.h>

#include <bfsar/model/Item.h>

#include <vector>

class Sound;
class WaveFile;

inline constexpr f32 cExportDialogWidthEm = 27.0f;
inline constexpr f32 cExportLabelWidthEm  = 7.5f;

void SanitizeFilenameInPlace(sead::BufferedSafeString *s);
void ResolveExportBaseName(const Item *item, const char *fallbackLabel, sead::BufferedSafeString *out);
void BuildDefaultExportPath(sead::BufferedSafeString* outPath, const Sound* sound, const char* ext);
void BuildItemDefaultExportPath(const Item* item, const char* fallbackLabel, const char* ext, sead::BufferedSafeString* out);
void ReplacePathExtension(sead::BufferedSafeString* path, const char* ext);

void BeginExportNameBatch();
void BuildUniqueExportPath(sead::BufferedSafeString *outPath, const char *dir, const sead::SafeString &rawName, const char *ext);
void BuildExportPathFromDir(sead::BufferedSafeString *outPath, const char *dir, const Sound *sound, const char *ext);

const WaveFile* FindStreamExportWave(const Sound* sound);
bool HasExportLoop(const WaveFile* wave);
bool ExportStreamSoundToWav(Sound* sound, const char* path, bool multiChannel, u32 loopCount, f32 fadeSec, u32 targetRate, bool loopMarkers);

void ShowExportResult(const char* message);
void ShowBatchExportResult(s32 total, s32 failureCount, const char* formatName);
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
};

bool StartSoundExport(const std::vector<Sound*>& sounds, const SoundExportOptions& options);
bool IsSoundExportRunning();
void CancelSoundExportRun();

void BuildTrackExportPath(sead::BufferedSafeString* outPath, const char* path, u32 trackNo);

void DrawShowExportedFilesCheck();
void ShowExportedFiles(const sead::SafeString& path);

struct ExportFileType
{
    const char* extension;
    const char* filterName;
    const char* fallbackName;
    const char* folderTitle;
};

template <typename T, typename WriteFn, typename ReportFn, typename ReportBatchFn>
void ExportItemsToFiles(std::vector<T*>* pending, const ExportFileType& type, WriteFn write, ReportFn report, ReportBatchFn reportBatch)
{
    if (pending->empty())
        return;

    if (pending->size() > 1)
    {
        sead::FixedSafeString<512> dirPath;
        if (SelectFolderDialog(&dirPath, type.folderTitle))
        {
            BeginExportNameBatch();

            s32 failureCount = 0;

            for (T* item : *pending)
            {
                sead::FixedSafeString<256> baseName;
                ResolveExportBaseName(item, type.fallbackName, &baseName);

                sead::FixedSafeString<512> filePath;
                BuildUniqueExportPath(&filePath, dirPath.cstr(), baseName, type.extension);

                if (!write(item, filePath))
                    failureCount++;
            }

            reportBatch(static_cast<s32>(pending->size()), failureCount);
        }
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
