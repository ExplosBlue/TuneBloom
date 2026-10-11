#pragma once

#include <bfsar/audio/AudioProcessing.h>
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
        CropStart,
        CropEnd,
        CurvePoint,
        CurveTension,
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

    bool showCrop = false;
    u32 cropStart = 0;
    u32 cropEnd = 0;

    float displayGain = 1.0f;

    bool showCurve = false;
    AudioProcessing::Curve curve;
    int curveHoverIndex = -1;
    bool curveHoverTension = false;
    int curveDragIndex = -1;
    int curveDragButton = 0;
    float curveDragTension = 0.0f;
    float curveDragMouseY = 0.0f;
    int curveMenuIndex = -1;

    Focus focus = Focus::Start;

    bool grabMarkers = false;

    // Peak cache
    u32 dataRevision = 0;
    std::vector<float> peakMin;
    std::vector<float> peakMax;
    const float* peakSource = nullptr;
    u32 peakSampleCount = 0;
    u32 peakRevision = 0;
};

void LoopWaveformBuildPeaks(const std::vector<float>& mono, std::vector<float>* peakMin, std::vector<float>* peakMax);
void LoopWaveformSetPeaks(LoopWaveformState& state, const std::vector<float>& mono, std::vector<float>& peakMin, std::vector<float>& peakMax);

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

void LoopWaveformZoomToLoop(LoopWaveformState& state, u32 loopStart, u32 loopEnd, u32 totalSamples);

void LoopWaveformFitAll(LoopWaveformState& state, u32 totalSamples);

}
