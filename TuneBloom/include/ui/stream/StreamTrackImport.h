#pragma once

#include <prim/seadSafeString.h>

#include <string>
#include <vector>

class Sound;
class WaveFile;
struct StreamSource;

void RequestStreamTrackImport(const StreamSource& source);
void DrawStreamTrackImportPopup(const StreamSource& source);
bool IsStreamTrackImportPending(const StreamSource& source);

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
