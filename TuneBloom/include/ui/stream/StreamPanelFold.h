#pragma once

#include <basis/seadTypes.h>

bool CanFoldStreamPanel();
bool DrawStreamPanelFoldTool();
bool UpdateStreamPanelFold(bool toggleFold);

bool IsTrackFolded();
void SetTrackFolded(bool folded);
f32 GetTrackUnfoldedHeight();
void SetTrackUnfoldedHeight(f32 height);
