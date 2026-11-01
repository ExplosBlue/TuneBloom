#pragma once

#include <basis/seadTypes.h>
#include <prim/seadSafeString.h>

#include <vector>

class AudioAsset;
class AudioResource;
enum class AudioAssetKind;
struct LooseFileFormat;

void FormatResourceVersion(sead::BufferedSafeString* out, u16 version);
LooseFileFormat GetAudioResourceFormat(const AudioResource& resource);
bool DrawBarsAudioFormatRow(AudioAssetKind* kind, const char* fixedReason, const char* bfwavReason);

void DrawAudioAssetsUI();
void DrawAudioResourceInfoUI();
void DrawAudioAssetPropertiesUI(AudioAsset* asset);
void DrawAudioResourceDialogs();
void CancelAudioResourceRequests();
void PlayAudioAsset(AudioAsset* asset);
void RequestAudioAssetReplace(AudioAsset* asset);
void RequestAudioAssetReimport(AudioAsset* asset);
bool IsAudioAssetReplacePending(const AudioAsset* asset);
void ReleaseAudioAssetView();
bool IsResourceAsset(const AudioAsset* asset);
bool CanExportAudioAssetFiles(const std::vector<AudioAsset*>& assets, const char** outReason);
bool CanExportAudioAssetWavs(const std::vector<AudioAsset*>& assets, sead::BufferedSafeString* outReason);
void RequestExportAudioAssetFiles(const std::vector<AudioAsset*>& assets);
void RequestExportAudioAssetWavs(const std::vector<AudioAsset*>& assets);
