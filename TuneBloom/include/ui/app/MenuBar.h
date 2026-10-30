#pragma once

#include <basis/seadTypes.h>

#include <imgui/imgui.h>

void DrawMenuBar();

ImDrawList* BeginMenuHighlight();
void EndMenuHighlight(ImDrawList* drawList, bool open, f32 hoverAlpha = 1.0f, const ImVec2* rangeY = nullptr);
bool MenuBarItem(const char* label, const char* shortcut = nullptr, bool selected = false, bool enabled = true);
bool MenuBarIconItem(const char* icon, const char* label, const char* shortcut = nullptr, bool enabled = true, const char* disabledReason = nullptr);

bool BeginSubMenu(const char* label);

void SetMenuBarTooltip(const char* tooltip);

f32 CalcMenuBarIconButtonWidth(const char* icon);
bool MenuBarIconButton(const char* id, const char* icon, f32 right, f32 top, f32 bottom, bool isOpen = false, const ImVec4* iconColor = nullptr, ImVec2* iconPos = nullptr);
