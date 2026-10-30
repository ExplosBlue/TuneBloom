#pragma once

#include <basis/seadTypes.h>

struct ImRect;

inline constexpr const char* cSidebarWindowId = "###ProjectWindow";

bool FindSidebarStripWidth(f32* outWidth);
void DrawSidebarSwitchInStrip(const ImRect& column);
void DrawSidebarSwitchInTitleRow(const ImRect& bb);
void ShowSidebarProject();
void DrawSidebarUI();
