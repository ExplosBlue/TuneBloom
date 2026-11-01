#pragma once

#include <imgui/imgui.h>

#include <basis/seadTypes.h>

struct RangeSegment
{
    ImVec2 min;
    ImVec2 max;
    ImVec4 color;
    ImVec4 backdrop;
    f32 labelLeft = 0.0f;
    f32 labelRight = 0.0f;
    const char* label = nullptr;
    const char* shortLabel = nullptr;
    bool strong = false;
    bool marked = false;
    bool endDivider = false;
};

struct RangeSpan
{
    f32 left;
    f32 right;
};

bool SetRangeSegmentCover(RangeSegment* segment, const RangeSpan* spans, s32 spanNum, s32 index);
void DrawRangeSegment(ImDrawList* draw, const RangeSegment& segment);
void DrawRangeSegmentHover(ImDrawList* draw, const ImVec2& min, const ImVec2& max);
