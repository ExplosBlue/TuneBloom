#pragma once

#include <basis/seadTypes.h>

#include <imgui/imgui.h>

void DrawMenuBar();

ImDrawList* BeginMenuHighlight();
void EndMenuHighlight(ImDrawList* drawList, bool open, f32 hoverAlpha = 1.0f, const ImVec2* rangeY = nullptr);
bool MenuBarItem(const char* label, const char* shortcut = nullptr, bool selected = false, bool enabled = true);
bool MenuBarIconItem(const char* icon, const char* label, const char* shortcut = nullptr, bool enabled = true, const char* disabledReason = nullptr);

bool BeginSubMenu(const char* label);
