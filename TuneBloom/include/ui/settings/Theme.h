#pragma once

#include <basis/seadTypes.h>

#include <imgui/imgui.h>

extern ImVec4 gAccentColor;

bool IsDarkTheme();
void SetDarkTheme(bool dark);

struct ThemeAdjust
{
    f32 brightness = 1.0f;
    f32 saturation = 1.0f;
    f32 contrast = 1.0f;
    f32 gradient = 1.0f;
};

ThemeAdjust& GetThemeAdjust();

void ApplyTheme();
void SetWindowShadowOpacity(f32 opacity);
void CalcThemeColors(ImVec4 accent, bool dark, ImVec4* colors);
f32 CalcRaisedGradient(bool dark);
f32 CalcSurfaceGradient(bool dark);
ImU32 GetTrackColorU32(ImU32 frameColor);
ImU32 GetRecessedPaneColorU32();
ImVec4 GetThemeAccent();
ImVec4 GetAccentTextColor();
ImVec4 GetOnAccentTextColor();

ImVec4 GetSeriesColor(s32 index);
