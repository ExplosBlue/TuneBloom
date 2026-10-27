#pragma once

#include <basis/seadTypes.h>

struct ImRect;

inline constexpr const char* cSidebarWindowId = "###ProjectWindow";

bool FindSidebarStripColumn(ImRect* outColumn);
void DrawSidebarSwitchInStrip(const ImRect& column, f32 top, f32 bottom);
void ShowSidebarProject();
void DrawSidebarUI();
