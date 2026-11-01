#pragma once

#include <imgui/imgui.h>

#include <basis/seadTypes.h>

struct RangeCell
{
    ImDrawList* draw = nullptr;
    ImVec2 min;
    ImVec2 max;
};

RangeCell BeginRangeCell(f32 blockHeight);
void DrawRangeMark(ImDrawList* draw, const ImVec2& min, const ImVec2& max, u32 start, u32 end, u32 sampleCount, ImU32 color);
void DrawRangeMarkInView(ImDrawList* draw, const ImVec2& min, const ImVec2& max, u32 start, u32 end, u32 viewStart, u32 viewEnd, ImU32 color);
bool DrawCellTimeField(const char* id, u32* value, u32 min, u32 max, u32 sampleCount, u32 sampleRate, bool disabled);
