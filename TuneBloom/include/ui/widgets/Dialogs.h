#pragma once

#include <basis/seadTypes.h>

#include <imgui/imgui.h>

#include <initializer_list>

inline constexpr f32 cDialogFormWidthEm = 24.0f;

void CenterNextDialog();
void SetDetachedDialogArea(const ImVec2& size);
bool AreDialogsDetached();
ImVec2 GetDialogAreaSize();
bool BeginDialog(const char* name, bool* open = nullptr, ImGuiWindowFlags flags = 0);

f32 CalcDialogFooterHeight();
f32 CalcDialogFooterWidth(std::initializer_list<const char*> labels);
void BeginDialogFooter(std::initializer_list<const char*> labels, bool separator = true, const char* warning = nullptr);
bool DialogButton(const char* label);

void DialogRowGap();
void DialogRow(const char* label, const char* help = nullptr);

bool BeginDialogList(const char* id, s32 columnNum, s32 rowNum, bool frameHeader = false);
void EndDialogList();
void DrawDialogListTitles(const char* const* titles, s32 titleNum, bool alignLastRight);
bool DrawDialogListCheck(bool* on);
void DrawDialogListText(const char* text, ImU32 color, bool alignRight = false, bool showEnd = false);

inline constexpr f32 cDialogIconFontSize = 40.0f;

const ImWchar* GetDialogIconRanges();
void BeginDialogMessage(const char* icon, ImGuiCol color = ImGuiCol_TextDisabled);
void EndDialogMessage();
void DialogNamedText(const char* format, const char* name);
