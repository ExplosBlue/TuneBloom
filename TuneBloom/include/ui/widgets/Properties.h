#pragma once

#include <basis/seadTypes.h>

inline constexpr f32 cPropertyDragSpeed = 0.1f;

f32 CalcPropertyLabelWidth();
void PropertyRow(const char* label, const char* help = nullptr);
void PropertyValueColumn();
bool PropertyToggleRow(const char* label, bool* enabled);

f32 CalcSectionTitleHeight();
bool DrawSectionTitle(const char* title, f32 width, const char* status = nullptr, const char* action = nullptr, const char* icon = nullptr, u32 iconColor = 0, f32 textInset = -1.0f);
bool BeginPropertySection(const char* title, bool* enabled = nullptr, bool* changed = nullptr, bool defaultOpen = true, s32 columnNum = 1, const char* detail = nullptr, const char* value = nullptr, const char* enableDetail = nullptr);
void EndPropertySection();

f32 CalcCardPadX();
f32 CalcCardBottomExtra();
bool BeginCard(const char* title, const char* status = nullptr, s32 columnNum = 1, const f32* columnWeights = nullptr, f32 minContentHeight = 0.0f);
bool NextCardLine(s32 line, s32 columnNum = 1, const f32* columnWeights = nullptr);
void EndCard();
void RegisterPropertySettings();

bool BeginPropertyBox(const char* id);
void EndPropertyBox();
bool DrawPanelHeader(const char* title, bool* enabled, const char* detail);
bool DrawPanelHeaderCombo(const char* title, const char* id, s32* current, const char* const items[], const char* const details[], s32 itemNum, const char* detail = nullptr, bool rowsBelow = true);

bool BeginGroupBox(const char* title, const char* action = nullptr, bool* pressed = nullptr);
bool BeginGroupBoxWithTools(const char* title, f32 toolsWidth);
bool BeginGroupBoxWithStatus(const char* title, const char* status);
f32 CalcGroupBoxWidth(f32 contentWidth);
f32 CalcChoiceRowWidth(const char* const* labels, s32 labelNum);
bool DrawChoiceRow(s32* selected, const char* const* labels, s32 labelNum, const char* const* details = nullptr);
bool BeginGroupBoxWithChoice(const char* title, s32* selected, const char* const* labels, s32 labelNum);
void EndGroupBox();
void GroupBoxRowLabel(const char* label, f32 labelWidth);
void TintGroupBoxCell();
void GroupBoxGap();

bool DrawPageHeader(const char* icon, const char* title, const char* description, const char* action = nullptr);

struct PageColumn
{
    f32 workMaxX = 0.0f;
    f32 contentMaxX = 0.0f;
};

PageColumn BeginPageColumn(f32 widthEm);
void EndPageColumn(const PageColumn& column);
