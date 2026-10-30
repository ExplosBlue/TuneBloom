#pragma once

#include <basis/seadTypes.h>

struct GLFWwindow;

namespace windowframe
{

struct Span
{
    s32 left = 0;
    s32 right = 0;
};

enum class WindowButton
{
    Minimize,
    Maximize,
    Close,
};

static constexpr s32 cWindowButtonNum = 3;

struct CaptionArea
{
    static constexpr s32 cClientSpanMax = 4;

    s32 height = 0;
    Span clientSpans[cClientSpanMax];
    s32 clientSpanNum = 0;
    Span buttons[cWindowButtonNum];
};

enum class ButtonState
{
    Idle,
    Hovered,
    Held,
};

void Install(GLFWwindow* window);
bool IsCustom();
void SetDark(GLFWwindow* window, bool isDark);
void SetRoundCorners(GLFWwindow* window, u32 borderColor);
void SetCaptionArea(const CaptionArea& area);
ButtonState GetButtonState(WindowButton button);

bool IsWindowMaximized(GLFWwindow* window);
bool CanMaximize(GLFWwindow* window);

void SetSize(GLFWwindow* window, s32 width, s32 height);
void SetSizeLimits(GLFWwindow* window, s32 minWidth, s32 minHeight, s32 maxWidth, s32 maxHeight);

}
