#pragma once

#include <imgui/imgui.h>

#include <basis/seadTypes.h>
#include <prim/seadSafeString.h>

void BeginRowRename(void* row, ImGuiID table, const char* text, ImGuiID field = 0);
void* GetRenamedRow(ImGuiID table, ImGuiID field = 0);
void EndRowRenameOf(const void* row);
void EndStaleRowRename();

bool DrawRowRenameField(const char* hint, sead::FixedSafeString<256>* outText, ImGuiInputTextCallback charFilter = nullptr);
bool DrawRowRenameFieldAt(const ImVec2& textPos, f32 textWidth, sead::FixedSafeString<256>* outText);
