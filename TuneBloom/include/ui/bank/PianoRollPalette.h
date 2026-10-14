#pragma once

#include <imgui/imgui.h>

struct PianoRollPalette
{
    ImU32 canvasBg;
    ImU32 placeholderText;
    ImU32 gridLineOctave;
    ImU32 gridLineVel;
    ImU32 gridLineVelTick;
    ImU32 vScrollTrack;
    ImU32 vScrollThumb;
    ImU32 vScrollThumbHover;
    ImU32 hScrollTrack;
    ImU32 hScrollThumbFits;
    ImU32 hScrollThumbOverflow;
    ImU32 hScrollThumbActive;
    ImU32 splitterHover;
};

const PianoRollPalette& GetPianoRollPalette();
