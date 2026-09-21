#pragma once

#include <bfsar/LoopAnalysis.h>
#include <imgui/imgui.h>

#include <cstdint>
#include <vector>

namespace ImGui
{

using u32 = uint32_t;

struct LoopWaveformState
{
    enum class Dragging
    {
        None,
        Start,
        End,
        Pan,
        Scrollbar,
    };

    enum class Focus
    {
        Start,
        End,
    };

    u32 viewStart = 0;
    u32 viewEnd = 0;
    bool viewInitialized = false;

    double viewStartAnim = 0.0;
    double viewEndAnim = 0.0;
    double viewStartTarget = 0.0;
    double viewEndTarget = 0.0;
    bool viewAnimInit = false;

    Dragging dragging = Dragging::None;
    float dragAnchorMouseX = 0.0f;
    u32 dragAnchorViewStart = 0;
    u32 dragAnchorViewEnd = 0;

    float crossfadeMs = 0.0f;
    bool equalPowerCurve = true;

    u32 cachedPeriod = 0;
    const std::vector<float>* cachedForBuffer = nullptr;

    Focus focus = Focus::Start;
};

bool LoopWaveformEditor(const char* idStr, LoopWaveformState& state,
                        const std::vector<float>& mono, u32 sampleRate,
                        u32& loopStart, u32& loopEnd,
                        float playheadSample = -1.0f,
                        bool showLoop = true,
                        bool editable = true,
                        const char* readoutText = nullptr,
                        ImVec2 size = ImVec2(0, 0),
                        bool showSeekBar = false,
                        double* outSeekSample = nullptr);

void LoopSeamPreview(const std::vector<float>& mono, u32 loopStart, u32 loopEnd, u32 period,
                     u32 sampleRate, float crossfadeMs, bool equalPowerCurve,
                     ImVec2 size = ImVec2(280, 80));

void LoopWaveformZoomToLoop(LoopWaveformState& state, u32 loopStart, u32 loopEnd, u32 totalSamples);

void LoopWaveformFitAll(LoopWaveformState& state, u32 totalSamples);

}
