#pragma once

#include <basis/seadTypes.h>

inline constexpr f32 cPropertyDragSpeed = 0.1f;

f32 CalcPropertyLabelWidth();
void PropertyRow(const char* label, const char* help = nullptr);
bool PropertyToggleRow(const char* label, bool* enabled);

bool BeginPropertySection(const char* title, bool* enabled = nullptr, bool* changed = nullptr, bool defaultOpen = true, s32 columnNum = 1, const char* detail = nullptr, const char* value = nullptr);
void EndPropertySection();
void RegisterPropertySettings();

bool BeginPropertyBox(const char* id);
void EndPropertyBox();
bool DrawPanelHeader(const char* title, bool* enabled, const char* detail);
bool DrawPanelHeaderCombo(const char* title, const char* id, s32* current, const char* const items[], const char* const details[], s32 itemNum, const char* detail = nullptr, bool rowsBelow = true);

bool BeginGroupBox(const char* title, const char* action = nullptr, bool* pressed = nullptr);
bool BeginGroupBoxWithTools(const char* title, f32 toolsWidth);
bool BeginGroupBoxWithStatus(const char* title, const char* status);
void EndGroupBox();
void GroupBoxRowLabel(const char* label, f32 labelWidth);
void TintGroupBoxCell();
void GroupBoxGap();

bool DrawPageHeader(const char* icon, const char* title, const char* description, const char* action = nullptr);
