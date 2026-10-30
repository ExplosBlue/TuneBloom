#pragma once

#include <basis/seadTypes.h>

struct ImGuiViewport;

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

bool FadeOutPlatformWindow(ImGuiViewport* viewport);
void DrawPlatformWindowFades();
