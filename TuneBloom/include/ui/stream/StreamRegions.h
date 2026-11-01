#pragma once

#include <basis/seadTypes.h>
#include <prim/seadSafeString.h>

class WaveFile;
struct ImDrawList;
struct ImVec2;

namespace ImGui
{
    struct LoopWaveformState;
}

void DrawStreamRegionsWindow();
void DrawStreamRegionsSection();

// Selection
s32 GetSelectedStreamRegion(const WaveFile* wave);
void NoteStreamRegionSelectionShown();
s32 GetShownStreamRegionSelection(const WaveFile* wave);
void SelectStreamRegion(const WaveFile* wave, s32 regionNo);
s32 GetPlayingStreamRegion(const WaveFile* wave);

s32 AddStreamRegion(WaveFile* wave, s32 afterNo);
void DeleteStreamRegion(WaveFile* wave, s32 regionNo);
void MoveStreamRegion(WaveFile* wave, s32 fromNo, s32 toNo);
bool RenameStreamRegion(WaveFile* wave, s32 regionNo, const sead::SafeString& name);

void SetStreamRegionRange(WaveFile* wave, s32 regionNo, u32 start, u32 end);
void CommitStreamRegionRange(const WaveFile* wave, s32 regionNo);

void RequestStreamRegionImport(WaveFile* wave);
void ImportTrackWavRegions(WaveFile* wave, const sead::SafeString& wavPath);
void DrawStreamRegionImportDialog();

void KeepStreamRegions(WaveFile* mainWave, WaveFile* oldMainWave);
u32 RemoveStreamRegionsPastEnd(WaveFile* wave);
void ClearStreamRegions(WaveFile* wave);

enum class RegionLaneClick
{
    None,
    Select,
    Menu
};

void DrawStreamRegionLane(WaveFile* wave, ImGui::LoopWaveformState& state);
RegionLaneClick DrawStreamRegionRowLane(WaveFile* wave, ImGui::LoopWaveformState& state, s32 regionNo, const ImVec2& min, const ImVec2& max, bool editable);

void DrawStreamRegionMarks(ImDrawList* draw, const ImVec2& min, const ImVec2& max, const WaveFile* wave, s32 onlyNo = -1, const ImGui::LoopWaveformState* view = nullptr);
s32 FindStreamRegionAt(const WaveFile* wave, u32 sample);
