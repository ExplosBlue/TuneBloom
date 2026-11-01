#pragma once

#include <basis/seadTypes.h>

class WaveMonoJob;

struct LoopFieldsResult
{
    bool changed = false;
    bool startCommit = false;
    bool endCommit = false;
};

struct WaveformTool
{
    const char* tooltip = nullptr;
    const char* disabledReason = nullptr;
    bool pressed = false;
};

struct WaveformSourceTools
{
    WaveformTool* reimport = nullptr;
    WaveformTool* replace = nullptr;
};

bool BeginWaveformHeader(f32 playToolsWidth, bool isLoop, const char* loopDisabledReason = nullptr, const WaveformSourceTools& sourceTools = {});
void EndWaveformHeader();
void SeekPlayerToSample(f64 sample);
void DrawWaveformPlaceholder(const WaveMonoJob& job, bool tooLong, const char* emptyText, f32 canvasHeight);
LoopFieldsResult DrawLoopFields(u32* loopStartFrame, u32* loopEndFrame, u32 sampleCount, u32 sampleRate, bool enabled, const char* endFixedDetail = nullptr);
