#pragma once

#include <basis/seadTypes.h>

void DrawProjectUI();
bool DrawNavRow(const char* icon, const char* name, bool selected, const char* value = nullptr, bool dim = false, bool title = false, f32 indent = 0.0f);

void DrawProjectInfoUI();
void DrawFileStatisticsUI();
