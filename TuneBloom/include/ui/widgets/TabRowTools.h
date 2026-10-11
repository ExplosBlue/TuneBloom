#pragma once

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <basis/seadTypes.h>
#include <prim/seadSafeString.h>

f32 CalcTabRowToolsWidth(s32 toolNum);
bool BeginTabRowTools(ImGuiDockNode* node, f32 toolsWidth);
void EndTabRowTools();

bool DrawTabRowTool(const char* label, bool inTabBar);
bool DrawTabRowCommand(const char* label, const char* tooltip, const char* disabledReason, bool inTabBar);

struct TabRowTitle
{
    const char* icon;
    const char* title;
    const char* id;
    s32 countDigits;
};

void FormatTabRowTitle(sead::BufferedSafeString* name, const TabRowTitle& title, s32 count);
void DrawTabRowCount(const TabRowTitle& title, s32 count);

void SetTabRowDisabled(const char* disabledReason);
