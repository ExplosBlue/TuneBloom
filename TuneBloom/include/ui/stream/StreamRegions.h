#pragma once

#include <basis/seadTypes.h>
#include <prim/seadSafeString.h>

class Sound;
class WaveFile;
struct ImDrawList;
struct ImVec2;

namespace ImGui
{
    struct LoopWaveformState;
}

void DrawStreamRegionsWindow();

// Selection
s32 GetSelectedStreamRegion(const WaveFile* wave);
void SelectStreamRegion(const WaveFile* wave, s32 regionNo);
void ClickStreamRegion(const WaveFile* wave, s32 regionNo);
void GetPlayingStreamRegions(const WaveFile* wave, s32* playingNo, s32* nextNo);

s32 AddStreamRegion(WaveFile* wave, s32 afterNo);
void DeleteStreamRegion(WaveFile* wave, s32 regionNo);
void MoveStreamRegion(WaveFile* wave, s32 fromNo, s32 toNo);
bool RenameStreamRegion(WaveFile* wave, s32 regionNo, const sead::SafeString& name);

void SetStreamRegionRange(WaveFile* wave, s32 regionNo, u32 start, u32 end);
void CommitStreamRegionRange(const WaveFile* wave, s32 regionNo);

void RequestStreamRegionImport(WaveFile* wave);
void ImportTrackWavRegions(WaveFile* wave, const sead::SafeString& wavPath);
void DrawStreamRegionImportDialog();

void KeepStreamRegions(Sound* sound, WaveFile* oldMainWave);
void TrimStreamRegions(WaveFile* wave);

void DrawStreamRegionLane(WaveFile* wave, ImGui::LoopWaveformState& state);

void DrawStreamRegionMarks(ImDrawList* draw, const ImVec2& min, const ImVec2& max, const WaveFile* wave, s32 onlyNo = -1);
s32 FindStreamRegionAt(const WaveFile* wave, u32 sample);
