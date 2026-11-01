#pragma once

#include <prim/seadSafeString.h>

#include <string>
#include <vector>

class Item;
class Sound;
class WaveFile;
struct StreamSource;

void RequestStreamTrackImport(const StreamSource& source, Item* anchorTrack = nullptr);
void RequestStreamTrackWaveImport(const StreamSource& source, Item* anchorTrack, WaveFile* wave);
void DrawStreamTrackImportPopup(const StreamSource& source);
bool IsStreamTrackImportPending(const StreamSource& source);
void CancelStreamTrackImports();

void ResetStreamCreateImport();
bool StartStreamCreateImport(const std::vector<std::string>& paths);
void DrawStreamCreateMainEditor();
bool HasStreamCreateMainWave();
const sead::SafeString& GetStreamCreateMainName();
u32 GetStreamCreateFileCount();
f32 CalcStreamCreateImportWidth();
f32 CalcStreamCreateImportHeight();
void DrawStreamCreateImport();
bool BuildStreamCreateWaves(sead::BufferedSafeString* error);
void AttachStreamCreateWaves(Sound* sound);
