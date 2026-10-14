#pragma once

#include <basis/seadTypes.h>

#include <ui/settings/ColorUtil.h>

#include <imgui/imgui.h>

namespace uicolors
{

inline constexpr ImVec4 cAccentDefault = Hex(0x8383FF);
inline constexpr ImVec4 cPlayback      = Hex(0xF7E04A);
inline constexpr ImVec4 cMarker        = Hex(0xFF7A8A);
inline constexpr ImVec4 cSuccess       = Hex(0x7EE0A6);
inline constexpr ImVec4 cWarning       = Hex(0xFFA552);

inline constexpr ImVec4 cInkOnDark     = Hex(0xFFFFFF);
inline constexpr ImVec4 cInkOnLight    = Hex(0x000000);
inline constexpr ImVec4 cTransparent   = HexA(0x00000000);

inline constexpr ImU32 cTransparentU32 = ToU32(cTransparent);
inline constexpr ImU32 cInkOnDarkU32   = ToU32(cInkOnDark);
inline constexpr ImU32 cRegionGap      = ToU32(cMarker, 0x96);

inline constexpr f32 cDimScale = 0.6f;

inline constexpr Themed<ImVec4> cPlaybackThemed     = { cPlayback, Hex(0x9C8200) };
inline constexpr Themed<ImVec4> cPlaybackFillThemed = { cPlayback, Hex(0xE6AC00) };
inline constexpr Themed<ImVec4> cMarkerThemed       = { cMarker,   Hex(0xCC3333) };
inline constexpr Themed<ImVec4> cSuccessThemed      = { cSuccess,  Hex(0x218C40) };
inline constexpr Themed<ImVec4> cWarningThemed      = { cWarning,  Hex(0xC25E0E) };

inline const ImVec4& Playback()
{
    return Pick(cPlaybackThemed);
}

inline const ImVec4& PlaybackFill()
{
    return Pick(cPlaybackFillThemed);
}

inline const ImVec4& Marker()
{
    return Pick(cMarkerThemed);
}

inline ImVec4 MarkerDim()
{
    return Scale(Marker(), cDimScale);
}

inline const ImVec4& Success()
{
    return Pick(cSuccessThemed);
}

inline const ImVec4& Warning()
{
    return Pick(cWarningThemed);
}

inline ImVec4 EditBorder()
{
    return WithAlpha(Warning(), 0xCC);
}

namespace token
{

inline constexpr ImVec4 cAccent      = cAccentDefault;
inline constexpr ImVec4 cWindow      = Hex(0x0D0D1A);
inline constexpr ImVec4 cPanel       = Hex(0x121222);
inline constexpr ImVec4 cCard        = Hex(0x18182D);
inline constexpr ImVec4 cControl     = Hex(0x212138);
inline constexpr ImVec4 cHover       = Hex(0x282845);
inline constexpr ImVec4 cLine        = Hex(0x363654);
inline constexpr ImVec4 cButton      = Hex(0x383858);
inline constexpr ImVec4 cButtonHover = Hex(0x444468);
inline constexpr ImVec4 cSelected    = Hex(0x2B2B66);
inline constexpr ImVec4 cPressed     = Hex(0x36367A);
inline constexpr ImVec4 cMuted       = Hex(0x7F80A8);
inline constexpr ImVec4 cText        = Hex(0xE2E3F3);
inline constexpr ImVec4 cAccentSoft  = Hex(0xA6A6FF);

inline constexpr ImVec4 cNavWindowingHighlight = HexA(0xFFFFFFB3);
inline constexpr ImVec4 cNavWindowingDimBg     = HexA(0x0000004D);
inline constexpr ImVec4 cModalWindowDimBg      = HexA(0x00000073);

inline constexpr f32 cDimTextSaturation = 0.20f;
inline constexpr f32 cDimTextLuma       = 0.50f;

}

inline constexpr ImVec4 cOnBright = token::cWindow;

namespace light
{

inline constexpr ImVec4 cWindowBg              = Hex(0xEFEFF2);
inline constexpr ImVec4 cChildBg               = Hex(0xFFFFFF);
inline constexpr ImVec4 cPopupBg               = HexA(0xF9F9FBFA);
inline constexpr ImVec4 cBorder                = HexA(0xBFBFC7E6);
inline constexpr ImVec4 cFrameBg               = Hex(0xE0E0E4);
inline constexpr ImVec4 cFrameBgHovered        = Hex(0xD5D5DB);
inline constexpr ImVec4 cFrameBgActive         = Hex(0xC7C7D1);
inline constexpr ImVec4 cTitleBg               = Hex(0xE3E3E7);
inline constexpr ImVec4 cTitleBgActive         = Hex(0xDDDDE2);
inline constexpr ImVec4 cTitleBgCollapsed      = HexA(0xE6E6EB99);
inline constexpr ImVec4 cMenuBarBg             = Hex(0xEAEAED);
inline constexpr ImVec4 cCheckMark             = Hex(0x4B4BD6);
inline constexpr ImVec4 cSliderGrab            = Hex(0x5555DD);
inline constexpr ImVec4 cSliderGrabActive      = Hex(0x3B3BC4);
inline constexpr ImVec4 cButton                = HexA(0x4B4BD62E);
inline constexpr ImVec4 cButtonHovered         = HexA(0x4B4BD647);
inline constexpr ImVec4 cButtonActive          = HexA(0x4B4BD666);
inline constexpr ImVec4 cHeader                = HexA(0x4949F23D);
inline constexpr ImVec4 cHeaderHovered         = HexA(0x6161F21A);
inline constexpr ImVec4 cHeaderActive          = HexA(0x4545E65C);
inline constexpr ImVec4 cSeparator             = Hex(0xC6C6CC);
inline constexpr ImVec4 cSeparatorHovered      = HexA(0x6C6CD9CC);
inline constexpr ImVec4 cSeparatorActive       = Hex(0x5252D9);
inline constexpr ImVec4 cTabHovered            = Hex(0xECECEE);
inline constexpr ImVec4 cTabActive             = Hex(0xEFEFF2);
inline constexpr ImVec4 cTabUnfocusedActive    = Hex(0xEFEFF2);
inline constexpr ImVec4 cDockingPreview        = HexA(0x3939E666);
inline constexpr ImVec4 cDockingEmptyBg        = Hex(0xEAEAED);
inline constexpr ImVec4 cPlotLines             = Hex(0x3A3AB8);
inline constexpr ImVec4 cPlotHistogram         = Hex(0x5A5ADB);
inline constexpr ImVec4 cPlotHistogramHovered  = Hex(0x4040C4);
inline constexpr ImVec4 cTableHeaderBg         = Hex(0xF1F1F4);
inline constexpr ImVec4 cTableBorderStrong     = Hex(0xBABABF);
inline constexpr ImVec4 cTableBorderLight      = Hex(0xD4D4D9);
inline constexpr ImVec4 cTextSelectedBg        = HexA(0x7373FF66);
inline constexpr ImVec4 cNavHighlight          = HexA(0x5252D9B3);

inline constexpr ImVec4 cText                  = HexA(0x121214F7);
inline constexpr ImVec4 cScrollbarBg           = HexA(0x0000000A);
inline constexpr ImVec4 cScrollbarGrab         = HexA(0x00000047);
inline constexpr ImVec4 cScrollbarGrabHovered  = HexA(0x0000005C);
inline constexpr ImVec4 cScrollbarGrabActive   = HexA(0x0000007A);
inline constexpr ImVec4 cResizeGrip            = HexA(0x00000040);
inline constexpr ImVec4 cResizeGripHovered     = HexA(0x00000073);
inline constexpr ImVec4 cResizeGripActive      = HexA(0x000000A6);
inline constexpr ImVec4 cTableRowBgAlt         = HexA(0x00000008);
inline constexpr ImVec4 cNavWindowingHighlight = HexA(0x00000066);
inline constexpr ImVec4 cNavWindowingDimBg     = HexA(0x00000033);
inline constexpr ImVec4 cModalWindowDimBg      = HexA(0x00000040);

}

namespace meter
{

inline constexpr ImU32 cLevelFloor = HexU32(0x28AA5A);
inline constexpr ImU32 cLevelLow   = HexU32(0x5AC850);
inline constexpr ImU32 cLevelMid   = HexU32(0xE1CD3C);
inline constexpr ImU32 cLevelHigh  = HexU32(0xE68C28);
inline constexpr ImU32 cLevelHot   = HexU32(0xE4552D);
inline constexpr ImU32 cLevelClip  = ToU32(cMarker);
inline constexpr ImU32 cClipText   = cInkOnDarkU32;
inline constexpr ImU32 cPeakHot    = cLevelClip;
inline constexpr ImU32 cPeakHigh   = HexU32(0xEB9B37);
inline constexpr ImU32 cPeakMid    = HexU32(0xE6D246);
inline constexpr ImU32 cPeakSafe   = HexU32A(0xEBEBF0E6);

}

namespace keyboard
{

inline constexpr Themed<ImGuiCol> cWhiteKeySurface = { ImGuiCol_Text, ImGuiCol_ChildBg };
inline constexpr Themed<ImGuiCol> cBlackKeyEdge    = { ImGuiCol_WindowBg, ImGuiCol_Text };
inline constexpr Themed<f32>      cBlackKeyLift    = { 0.45f, 0.18f };

inline constexpr f32 cWhiteKeyInkMix       = 0.92f;
inline constexpr f32 cWhiteKeyHoverMix     = 0.22f;
inline constexpr f32 cBlackKeyHoverMix     = 0.30f;
inline constexpr f32 cBlackKeyActiveDarken = 0.275f;
inline constexpr f32 cKeyShadowAlpha       = 0.28f;
inline constexpr f32 cKeySeparatorMix      = 0.18f;

}

namespace pianoroll
{

inline constexpr Themed<ImU32> cPlaceholderText  = { ToU32(cInkOnDark), HexU32(0x40464D) };
inline constexpr Themed<ImU32> cGridOctave       = { ToU32(cInkOnDark, 0x28), ToU32(cInkOnLight, 0x23) };
inline constexpr Themed<ImU32> cGridVelocity     = { ToU32(cInkOnDark, 0x1C), ToU32(cInkOnLight, 0x19) };
inline constexpr Themed<ImU32> cGridVelocityTick = { ToU32(cInkOnDark, 0x78), ToU32(cInkOnLight, 0x6E) };

}

}
