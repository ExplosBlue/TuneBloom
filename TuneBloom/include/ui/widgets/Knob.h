#pragma once

#include <imgui/imgui.h>

#include <basis/seadTypes.h>
#include <prim/seadSafeString.h>

#include <snd/Global.h>

inline constexpr f32 cRegionGapEm      = 1.5f;
inline constexpr f32 cRegionKnobGapEm  = 0.5f;
inline constexpr f32 cRegionCurveMinEm = 4.0f;
inline constexpr f32 cRegionCurveEm    = 5.0f;
inline constexpr f32 cRegionCurveMaxEm = 10.0f;

void SetRegionPanelFill(bool fill);
bool IsRegionPanelFill();

bool KnobU8(const char* label, u8* value, u8 min, u8 max, const ImVec2& size);
bool KnobU8(const char* label, u8* value, u8 min, u8 max, u8 origin, const ImVec2& size);
bool KnobF32(const char* label, f32* value, f32 min, f32 max, const char* format, f32 speed, const ImVec2& size);
bool KnobF32(const char* label, f32* value, f32 min, f32 max, f32 origin, const char* format, f32 speed, const ImVec2& size);
bool KnobRatio(const char* label, f32* value, f32 min, f32 max, const char* format, f32 speed, const ImVec2& size);
void KnobTooltip(const char* format, ...);
void SetNextKnobReset(f64 value);
void SetNextKnobText(const char* text);

using KnobScale = f32 (*)(u8 value);
void SetNextKnobScale(KnobScale scale);
void UpdateKnobCursor();
bool DragField(const char* label, ImGuiDataType dataType, void* value, f32 speed = 1.0f, const void* min = nullptr, const void* max = nullptr, const char* format = nullptr, ImGuiSliderFlags flags = 0);
bool DrawPitchKnobs(f32* pitch, const ImVec2& knobSize);
sead::FixedSafeString<8> FormatPanText(u8 pan, u8 center);

void RegionHeader(const char* title, f32 reservedWidth = 0.0f);
void CenterRegionKnobs(f32 rowWidth);
f32 CalcRegionStripHeight();
f32 CalcKnobGap();
f32 CalcKnobWidth(const char* const* labels, s32 labelNum);
f32 CalcKnobFitWidth(const char* const* labels, s32 labelNum);
f32 CalcKnobRowWidth(const char* const* labels, s32 labelNum);

f32 CalcAdshrEditorWidth(bool stacked);
bool DrawAdshrEditor(snd::AdshrCurve& adshrCurve, f32 height, bool stacked = false);
