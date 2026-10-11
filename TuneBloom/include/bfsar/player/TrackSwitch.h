#pragma once

#include <basis/seadTypes.h>

inline bool IsTrackAudible(u32 muteTracks, u32 soloTracks, u32 trackNo)
{
    if (soloTracks != 0)
        return (soloTracks & (1 << trackNo)) != 0;

    return (muteTracks & (1 << trackNo)) == 0;
}
