#pragma once

#include <basis/seadTypes.h>
#include <prim/seadSafeString.h>

class AudioAsset;

void FormatResourceVersion(sead::BufferedSafeString* out, u16 version);

void DrawAudioAssetsUI();
void DrawAudioResourceInfoUI();
void DrawAudioAssetPropertiesUI(AudioAsset* asset);
void DrawAudioResourceDialogs();
void CancelAudioResourceRequests();
void PlayAudioAsset(AudioAsset* asset);
void RequestAudioAssetReplace(AudioAsset* asset);
void ReleaseAudioAssetView();
