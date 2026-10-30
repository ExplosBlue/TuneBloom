#pragma once

#include <basis/seadTypes.h>

bool HasCustomTitleBar();
f32 DrawTitleBarIcon(f32 left, f32 top, f32 bottom);
f32 DrawWindowButtons(f32 right, f32 top, f32 bottom);
void FinishTitleBar(f32 menuLeft, f32 menuRight, f32 toolsLeft, f32 bottom);
