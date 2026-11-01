#pragma once

#include <basis/seadTypes.h>

enum class ZoomScrollResult
{
    None,
    Scrolled,
    Zoomed,
    Reset,
};

f32 ZoomScrollBarHeight();
ZoomScrollResult ZoomScrollBar(const char* id, f32 width, f32* left, f32* right, f32 minSpan, f32 maxSpan);
