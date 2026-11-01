#pragma once

#include <basis/seadTypes.h>

#include <ui/settings/UiColors.h>

#include <imgui/imgui.h>

inline constexpr uicolors::Themed<f32> cLaneRowSelectedAlpha = { 0.10f, 0.06f };
inline constexpr uicolors::Themed<f32> cLaneRowIndexAlpha    = { 0.45f, 0.18f };

bool DrawLaneRowSelectable(const char* id, bool selected, bool playing, f32 height, bool* outHovered);
void DrawLaneIndexBadge(s32 index, const ImVec2& cellMin, const ImVec2& cellSize, bool selected);
f32 CalcLaneIndexWidth(const char* widest);

ImVec2 CalcLanePad();
void DrawLaneFill(ImDrawList* draw, const ImVec2& min, const ImVec2& max, bool selected);
void DrawLaneRuler(u32 sampleCount, u32 sampleRate);

namespace ImGui
{
    struct LoopWaveformState;
}

struct LaneSeekRuler
{
    f64 seekSample = -1.0;
    f32 laneLeft = 0.0f;
    f32 laneWidth = 0.0f;
};

LaneSeekRuler DrawLaneSeekRuler(ImGui::LoopWaveformState& view, u32 sampleCount, u32 sampleRate, f32 playSample, bool showLoop, u32 loopStart, u32 loopEnd);
f32 CalcLaneZoomBarHeight();
void DrawLaneZoomBar(ImGui::LoopWaveformState& view, f32 laneLeft, f32 laneWidth, u32 sampleCount);
f32 CalcLaneViewRatio(const ImGui::LoopWaveformState& view, f32 sample);
void DrawLaneRowPlayhead(f32 laneLeft, f32 laneRight, f32 rowHeight, f32 playRatio);

void DrawLaneRowMenuButton(const char* popupId, bool rowHovered, const char* tooltip);
void PlaceLaneRowMenu(const char* popupId);
