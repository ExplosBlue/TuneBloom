#pragma once

#include <basis/seadTypes.h>

#include <imgui/imgui.h>

inline constexpr ImVec4 cWarningTextColor(1.0f, 0.78f, 0.35f, 1.0f);
inline constexpr ImVec4 cDefaultAccentColor(0.24f, 0.50f, 0.88f, 1.00f);

extern ImVec4 gAccentColor;
extern bool gThemeIsDark;

struct ThemeAdjust
{
    f32 brightness = 1.0f;
    f32 saturation = 1.0f;
    f32 contrast = 1.0f;
    f32 gradient = 1.0f;
};

ThemeAdjust& GetThemeAdjust();

void ApplyThemeFromAccent(ImVec4 accent);
void SetWindowShadows(bool enabled);
void CalcThemeColors(ImVec4 accent, bool dark, ImVec4* colors);
f32 CalcRaisedGradient(bool dark);
f32 CalcSurfaceGradient(bool dark);
ImU32 GetTrackColorU32(ImU32 frameColor);
ImU32 GetRecessedPaneColorU32();
ImVec4 GetAccentTextColor();

ImVec4 GetSeriesColor(s32 index);
