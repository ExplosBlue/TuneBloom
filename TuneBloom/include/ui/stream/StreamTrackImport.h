#pragma once

#include <prim/seadSafeString.h>

#include <string>
#include <vector>

class Sound;
class WaveFile;

void RequestStreamTrackImport(Sound* sound);
void DrawStreamTrackImportPopup(Sound* sound);

void ResetStreamCreateImport();
void SetStreamCreateMainWave(WaveFile* wave, const sead::SafeString& name);
bool HasStreamCreateMainWave();
void SetStreamCreateFiles(const std::vector<std::string>& paths);
u32 GetStreamCreateFileCount();
bool TakeStreamCreateEditRequest();
f32 CalcStreamCreateImportWidth();
f32 CalcStreamCreateImportHeight();
void DrawStreamCreateImport();
bool BuildStreamCreateWaves(sead::BufferedSafeString* error);
void AttachStreamCreateWaves(Sound* sound);
