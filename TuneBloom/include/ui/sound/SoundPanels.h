#pragma once

#include <ui/app/UI.h>

inline constexpr ImU8 cPanMin = 0;
inline constexpr ImU8 cPanMax = 127;

inline constexpr u8 cSoundLpfMax          = 64;
inline constexpr u8 cSoundVolumeDefault   = 127;
inline constexpr u8 cSoundMainSendDefault = 127;

f32 CalcBiquadTypeWidth();
bool DrawBiquadTypeCombo(u8* biquadType, f32 width);
