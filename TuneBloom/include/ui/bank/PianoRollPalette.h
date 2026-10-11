#pragma once

#include <imgui/imgui.h>

struct PianoRollPalette
{
    ImU32 canvasBg, placeholderText;
    ImU32 gridLineOctave, gridLineVel, gridLineVelTick;
    ImU32 grabBar;
    ImU32 vScrollTrack, vScrollThumb, vScrollThumbHover;
    ImU32 hScrollTrack, hScrollThumbFits, hScrollThumbOverflow, hScrollThumbActive;
    ImU32 splitterHover;
};

const PianoRollPalette& GetPianoRollPalette();
