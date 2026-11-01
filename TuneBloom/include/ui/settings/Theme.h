#pragma once

#include <basis/seadTypes.h>

#include <imgui/imgui.h>

extern ImVec4 gAccentColor;

bool IsDarkTheme();
void SetDarkTheme(bool dark);

inline constexpr f32 cThemeHueDefault    = 240.0f / 360.0f;
inline constexpr f32 cThemeSaturationMin = 0.0f;
inline constexpr f32 cThemeSaturationMax = 2.0f;
inline constexpr f32 cThemeBrightnessMin = 0.25f;
inline constexpr f32 cThemeBrightnessMax = 1.75f;
inline constexpr f32 cThemeContrastMin   = 0.5f;
inline constexpr f32 cThemeContrastMax   = 2.0f;
inline constexpr f32 cThemeGradientMin   = 0.0f;
inline constexpr f32 cThemeGradientMax   = 3.0f;

struct ThemeAdjust
{
    f32 saturation = 1.0f;
    f32 brightness = 1.0f;
    f32 contrast = 1.0f;
    f32 gradient = 1.0f;
};

ThemeAdjust& GetThemeAdjust();
f32 GetAccentHue();
void SetAccentHue(f32 hue);
ImVec4 CalcAccentPreviewColor(f32 hue, bool dark);

void ApplyTheme();
void SetWindowShadowOpacity(f32 opacity);
void CalcThemeColors(ImVec4 accent, bool dark, ImVec4* colors);
f32 CalcRaisedGradient(bool dark);
f32 CalcSurfaceGradient(bool dark);
ImU32 GetTrackColorU32(ImU32 frameColor);
ImU32 GetRecessedPaneColorU32();
ImVec4 GetThemeAccent();
ImVec4 GetAccentTextColor();
ImVec4 GetLegibleAccentColor();
ImVec4 GetOnAccentTextColor();

ImVec4 GetSeriesColor(s32 index);
ImVec4 GetAccentAt(f32 lightness, f32 chroma);
