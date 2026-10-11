#pragma once

#include <basis/seadTypes.h>

struct ImVec2;

void DrawProjectUI();
bool DrawNavRow(const char* icon, const char* name, bool selected, const char* value = nullptr, bool dim = false, bool title = false, f32 indent = 0.0f);
f32 CalcNavRowGap();
ImVec2 CalcNavBoxPadding(bool roomy);
bool BeginNavBox(const char* id, const ImVec2& padding, bool border = true, f32 width = 0.0f);
void EndNavBox();

void DrawProjectInfoUI();
void DrawFileStatisticsUI();
