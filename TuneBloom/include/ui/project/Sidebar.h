#pragma once

#include <basis/seadTypes.h>

struct ImRect;

bool FindSidebarStripColumn(ImRect* outColumn);
void DrawSidebarSwitchInStrip(const ImRect& column, f32 top, f32 bottom);
void ShowSidebarProject();
void DrawSidebarUI();
