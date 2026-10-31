#pragma once

#include <basis/seadTypes.h>

struct LoopFieldsResult
{
    bool changed = false;
    bool startCommit = false;
    bool endCommit = false;
};

struct WaveformReplaceTool
{
    const char* tooltip = nullptr;
    const char* disabledReason = nullptr;
    bool pressed = false;
};

bool BeginWaveformHeader(f32 playToolsWidth, bool isLoop, const char* loopDisabledReason = nullptr, WaveformReplaceTool* replace = nullptr);
void EndWaveformHeader();
LoopFieldsResult DrawLoopFields(u32* loopStartFrame, u32* loopEndFrame, u32 sampleCount, u32 sampleRate, bool enabled, const char* endFixedDetail = nullptr);
