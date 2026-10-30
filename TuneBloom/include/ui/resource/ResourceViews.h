#pragma once

#include <basis/seadTypes.h>
#include <prim/seadSafeString.h>

class AudioAsset;
enum class AudioAssetKind;

void FormatResourceVersion(sead::BufferedSafeString* out, u16 version);
bool DrawBarsAudioFormatRow(AudioAssetKind* kind, const char* fixedReason, const char* bfwavReason);

void DrawAudioAssetsUI();
void DrawAudioResourceInfoUI();
void DrawAudioAssetPropertiesUI(AudioAsset* asset);
void DrawAudioResourceDialogs();
void CancelAudioResourceRequests();
void PlayAudioAsset(AudioAsset* asset);
void RequestAudioAssetReplace(AudioAsset* asset);
bool IsAudioAssetReplacePending(const AudioAsset* asset);
void ReleaseAudioAssetView();
