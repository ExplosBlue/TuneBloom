#pragma once

#include <ui/wave/LoopWaveformEditor.h>

namespace ImGui
{

using s32 = int32_t;
using s64 = int64_t;

constexpr u32 cMinViewSpan = 16;

struct WaveformPalette
{
    ImU32 wave, waveLit, loop, loopGlow, introGlow, xfadeFill, good, edge, edgeSoft, inkFaint, bg, crop;
};

struct WaveformCanvas
{
    ImVec2 origin;
    float width = 0.0f;
    float height = 0.0f;
    u32 sampleCount = 0;
    bool hovered = false;
    float mouseX = 0.0f;
    ImVec2 mouse;
};

u32 ViewSpan(const LoopWaveformState& state);
float SampleToX(const LoopWaveformState& state, u32 sample, float canvasWidth);
double XToSample(const LoopWaveformState& state, float x, float canvasWidth);
void InitViewIfNeeded(LoopWaveformState& state, u32 sampleCount);
void AdvanceViewAnimation(LoopWaveformState& state, u32 sampleCount);
void StartViewPan(LoopWaveformState& state);
void DragViewPan(LoopWaveformState& state, const WaveformCanvas& canvas);
void HandleViewWheel(LoopWaveformState& state, const WaveformCanvas& canvas);
void DrawMinimap(LoopWaveformState& state, float width, u32 sampleCount);

const WaveformPalette& Palette();
void UpdatePeakCache(LoopWaveformState& state, const std::vector<float>& mono);
void DrawWaveform(ImDrawList* draw, ImVec2 origin, float width, float height,
                  LoopWaveformState& state, const std::vector<float>& mono, u32 sampleRate,
                  u32 loopStart, u32 loopEnd, float playheadSample, bool showLoop);
bool DrawSeekRuler(const char* idStr, ImVec2 origin, float width, float height,
                   const LoopWaveformState& state, u32 sampleRate,
                   float playheadSample, bool showLoop, u32 loopStart, u32 loopEnd,
                   double* outSeekSample);

bool IsCurveDrag(LoopWaveformState::Dragging dragging);
float CurveGainAt(const LoopWaveformState& state, float sample);
void DrawCurve(ImDrawList* draw, ImVec2 origin, float width, float height, const LoopWaveformState& state);
void UpdateCurveHover(LoopWaveformState& state, const WaveformCanvas& canvas);
bool BeginCurveEdit(LoopWaveformState& state, ImVec2 mouse, float width, float height, bool* changed);
bool DragCurve(LoopWaveformState& state, ImVec2 mouse, float width, float height);
void DrawCurveTooltip(const LoopWaveformState& state);
bool DrawCurvePointMenu(LoopWaveformState& state);

}
