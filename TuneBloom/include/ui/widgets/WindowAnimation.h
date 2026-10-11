#pragma once

#include <basis/seadTypes.h>

enum class WindowAnimation
{
    Menu,
    Dialog,
    Tooltip
};

void InitWindowAnimation();

bool IsMotionAllowed();
void SetAnimationEnabled(bool enabled);
bool IsAnimationEnabled();

void AnimateWindow(WindowAnimation kind);
