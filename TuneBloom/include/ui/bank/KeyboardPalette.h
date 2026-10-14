#pragma once

#include <imgui/imgui.h>

struct KeyboardPalette
{
    ImU32 whiteKey;
    ImU32 whiteKeyHover;
    ImU32 whiteKeyActive;
    ImU32 whiteKeyRoot;
    ImU32 whiteKeyRootActive;
    ImU32 blackKey;
    ImU32 blackKeyHover;
    ImU32 blackKeyActive;
    ImU32 blackKeyRoot;
    ImU32 blackKeyRootActive;
    ImU32 blackKeyEdge;
    ImU32 keyShadow;
    ImU32 separator;
    ImU32 rootStripe;
};

const KeyboardPalette& GetKeyboardPalette();
