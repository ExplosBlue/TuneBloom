#pragma once

#include <basis/seadTypes.h>

#include <vector>

struct OldAudioData
{
    std::vector<u32> markerEnds;
    std::vector<u32> regionEnds;
    bool hasBeatGrid = false;
    f32 tempo = 0.0f;
    bool isMidSide = false;
    bool canRewriteMeta = true;
    bool hasSourceRate = false;

    bool isEmpty() const
    {
        return markerEnds.empty() && regionEnds.empty() && !hasBeatGrid && !isMidSide;
    }
};

struct OldAudioClear
{
    bool markers = false;
    bool beatGrid = false;
    bool midSide = false;
    bool regions = false;
};
