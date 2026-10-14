#define IMGUI_DEFINE_MATH_OPERATORS
#include <ui/settings/Theme.h>

#include <ui/settings/UiColors.h>

#include <imgui/imgui_custom.h>
#include <imgui/imgui_internal.h>

#include <app/Utilll.h>

static constexpr f32 cKeepTokenAlpha      = -1.0f;
static constexpr f32 cGrayscaleSaturation = 0.05f;
static constexpr f32 cLightTrackInkMix    = 0.13f;
static constexpr f32 cSeriesHueStep       = 0.618034f;
static constexpr f32 cSeriesSaturationMin = 0.5f;
static constexpr f32 cAccentTextMix       = 0.3f;

static constexpr uicolors::Themed<f32> cWindowShadowAlpha = { 0.50f, 0.20f };
static constexpr uicolors::Themed<f32> cRaisedGradient    = { 0.03f, 0.015f };
static constexpr uicolors::Themed<f32> cSurfaceGradient   = { 0.02f, 0.0075f };
static constexpr uicolors::Themed<f32> cRecessedPaneValue = { 0.72f, 0.965f };

namespace
{

struct ThemeSlot
{
    ImVec4 color;
    bool tint;
    f32 alpha;
};

struct ThemeRole
{
    ImGuiCol index;
    ThemeSlot dark;
    ThemeSlot light;
};

constexpr ThemeSlot Tinted(const ImVec4& color, f32 alpha = cKeepTokenAlpha)
{
    return { color, true, alpha };
}

constexpr ThemeSlot Fixed(const ImVec4& color)
{
    return { color, false, cKeepTokenAlpha };
}

}

ImVec4 gAccentColor = uicolors::cAccentDefault;
static bool sThemeIsDark = true;

static ThemeAdjust sThemeAdjust;
static bool sWindowShadows = true;

bool IsDarkTheme()
{
    return sThemeIsDark;
}

void SetDarkTheme(bool dark)
{
    sThemeIsDark = dark;
}

ThemeAdjust& GetThemeAdjust()
{
    return sThemeAdjust;
}

static ImVec4 FromAdjustedHsv(f32 hue, f32 saturation, f32 value, f32 alpha)
{
    saturation = ImSaturate(saturation * sThemeAdjust.saturation);
    value = ImSaturate((value * sThemeAdjust.brightness - 0.5f) * sThemeAdjust.contrast + 0.5f);

    return uicolors::FromHsv(hue, saturation, value, alpha);
}

static ImVec4 Tint(f32 hue, const ImVec4& token, f32 alpha)
{
    f32 tokenHue;
    f32 saturation;
    f32 value;
    ImGui::ColorConvertRGBtoHSV(token.x, token.y, token.z, tokenHue, saturation, value);

    return FromAdjustedHsv(hue, saturation, value, alpha < 0.0f ? token.w : alpha);
}

static ImVec4 Neutral(f32 hue, f32 saturation, f32 luma)
{
    const ImVec4 base = uicolors::FromHsv(hue, ImSaturate(saturation * sThemeAdjust.saturation), 1.0f);

    const f32 scale = luma / (0.2126f * base.x + 0.7152f * base.y + 0.0722f * base.z);

    return ImVec4(ImMin(base.x * scale, 1.0f), ImMin(base.y * scale, 1.0f), ImMin(base.z * scale, 1.0f), 1.0f);
}

void CalcThemeColors(ImVec4 accent, bool dark, ImVec4* colors)
{
    using namespace uicolors::token;

    static constexpr ThemeRole cRoles[] =
    {
        { ImGuiCol_Text,                  Tinted(cText),                     Fixed(uicolors::light::cText) },
        { ImGuiCol_WindowBg,              Tinted(cWindow),                   Tinted(uicolors::light::cWindowBg) },
        { ImGuiCol_ChildBg,               Tinted(cPanel),                    Tinted(uicolors::light::cChildBg) },
        { ImGuiCol_PopupBg,               Tinted(cCard),                     Tinted(uicolors::light::cPopupBg) },
        { ImGuiCol_Border,                Tinted(cLine),                     Tinted(uicolors::light::cBorder) },
        { ImGuiCol_FrameBg,               Tinted(cControl),                  Tinted(uicolors::light::cFrameBg) },
        { ImGuiCol_FrameBgHovered,        Tinted(cHover),                    Tinted(uicolors::light::cFrameBgHovered) },
        { ImGuiCol_FrameBgActive,         Tinted(cLine),                     Tinted(uicolors::light::cFrameBgActive) },
        { ImGuiCol_TitleBg,               Tinted(cPanel),                    Tinted(uicolors::light::cTitleBg) },
        { ImGuiCol_TitleBgActive,         Tinted(cCard),                     Tinted(uicolors::light::cTitleBgActive) },
        { ImGuiCol_TitleBgCollapsed,      Tinted(cWindow),                   Tinted(uicolors::light::cTitleBgCollapsed) },
        { ImGuiCol_MenuBarBg,             Tinted(cPanel),                    Tinted(uicolors::light::cMenuBarBg) },
        { ImGuiCol_ScrollbarBg,           Tinted(cPanel),                    Fixed(uicolors::light::cScrollbarBg) },
        { ImGuiCol_ScrollbarGrab,         Tinted(cLine),                     Fixed(uicolors::light::cScrollbarGrab) },
        { ImGuiCol_ScrollbarGrabHovered,  Tinted(cMuted),                    Fixed(uicolors::light::cScrollbarGrabHovered) },
        { ImGuiCol_ScrollbarGrabActive,   Tinted(cAccent),                   Fixed(uicolors::light::cScrollbarGrabActive) },
        { ImGuiCol_CheckMark,             Tinted(cAccent),                   Tinted(uicolors::light::cCheckMark) },
        { ImGuiCol_SliderGrab,            Tinted(cAccent),                   Tinted(uicolors::light::cSliderGrab) },
        { ImGuiCol_SliderGrabActive,      Tinted(cAccentSoft),               Tinted(uicolors::light::cSliderGrabActive) },
        { ImGuiCol_Button,                Tinted(cButton),                   Tinted(uicolors::light::cButton) },
        { ImGuiCol_ButtonHovered,         Tinted(cButtonHover),              Tinted(uicolors::light::cButtonHovered) },
        { ImGuiCol_ButtonActive,          Tinted(cPressed),                  Tinted(uicolors::light::cButtonActive) },
        { ImGuiCol_Header,                Tinted(cSelected),                 Tinted(uicolors::light::cHeader) },
        { ImGuiCol_HeaderHovered,         Tinted(cHover),                    Tinted(uicolors::light::cHeaderHovered) },
        { ImGuiCol_HeaderActive,          Tinted(cPressed),                  Tinted(uicolors::light::cHeaderActive) },
        { ImGuiCol_Separator,             Tinted(cLine),                     Tinted(uicolors::light::cSeparator) },
        { ImGuiCol_SeparatorHovered,      Tinted(cMuted),                    Tinted(uicolors::light::cSeparatorHovered) },
        { ImGuiCol_SeparatorActive,       Tinted(cAccent),                   Tinted(uicolors::light::cSeparatorActive) },
        { ImGuiCol_ResizeGrip,            Tinted(cLine),                     Fixed(uicolors::light::cResizeGrip) },
        { ImGuiCol_ResizeGripHovered,     Tinted(cMuted),                    Fixed(uicolors::light::cResizeGripHovered) },
        { ImGuiCol_ResizeGripActive,      Tinted(cAccent),                   Fixed(uicolors::light::cResizeGripActive) },
        { ImGuiCol_Tab,                   Tinted(cPanel),                    Fixed(uicolors::cTransparent) },
        { ImGuiCol_TabHovered,            Tinted(cHover),                    Tinted(uicolors::light::cTabHovered) },
        { ImGuiCol_TabActive,             Tinted(cSelected),                 Tinted(uicolors::light::cTabActive) },
        { ImGuiCol_TabUnfocused,          Tinted(cPanel),                    Fixed(uicolors::cTransparent) },
        { ImGuiCol_TabUnfocusedActive,    Tinted(cControl),                  Tinted(uicolors::light::cTabUnfocusedActive) },
        { ImGuiCol_DockingPreview,        Tinted(cAccent, 0.55f),            Tinted(uicolors::light::cDockingPreview) },
        { ImGuiCol_DockingEmptyBg,        Tinted(cWindow),                   Tinted(uicolors::light::cDockingEmptyBg) },
        { ImGuiCol_PlotLines,             Tinted(cText),                     Tinted(uicolors::light::cPlotLines) },
        { ImGuiCol_PlotHistogram,         Tinted(cAccent),                   Tinted(uicolors::light::cPlotHistogram) },
        { ImGuiCol_PlotHistogramHovered,  Tinted(cAccentSoft),               Tinted(uicolors::light::cPlotHistogramHovered) },
        { ImGuiCol_TableHeaderBg,         Tinted(cCard),                     Tinted(uicolors::light::cTableHeaderBg) },
        { ImGuiCol_TableBorderStrong,     Tinted(cLine),                     Tinted(uicolors::light::cTableBorderStrong) },
        { ImGuiCol_TableBorderLight,      Tinted(cControl),                  Tinted(uicolors::light::cTableBorderLight) },
        { ImGuiCol_TableRowBgAlt,         Tinted(cCard),                     Fixed(uicolors::light::cTableRowBgAlt) },
        { ImGuiCol_TextSelectedBg,        Tinted(cAccent, 0.35f),            Tinted(uicolors::light::cTextSelectedBg) },
        { ImGuiCol_NavHighlight,          Tinted(cAccent),                   Tinted(uicolors::light::cNavHighlight) },
        { ImGuiCol_NavWindowingHighlight, Fixed(cNavWindowingHighlight),     Fixed(uicolors::light::cNavWindowingHighlight) },
        { ImGuiCol_NavWindowingDimBg,     Fixed(cNavWindowingDimBg),         Fixed(uicolors::light::cNavWindowingDimBg) },
        { ImGuiCol_ModalWindowDimBg,      Fixed(cModalWindowDimBg),          Fixed(uicolors::light::cModalWindowDimBg) },
    };

    f32 hue;
    f32 saturation;
    f32 value;
    ImGui::ColorConvertRGBtoHSV(accent.x, accent.y, accent.z, hue, saturation, value);

    if (saturation < cGrayscaleSaturation)
        hue = 0.0f;

    for (const ThemeRole& role : cRoles)
    {
        const ThemeSlot& slot = dark ? role.dark : role.light;

        colors[role.index] = slot.tint ? Tint(hue, slot.color, slot.alpha) : slot.color;
    }

    colors[ImGuiCol_TextDisabled]     = dark ? Tint(hue, cMuted, cKeepTokenAlpha) : Neutral(hue, cDimTextSaturation, cDimTextLuma);
    colors[ImGuiCol_BorderShadow]     = uicolors::cTransparent;
    colors[ImGuiCol_TableRowBg]       = uicolors::cTransparent;
    colors[ImGuiCol_PlotLinesHovered] = uicolors::cPlayback;
    colors[ImGuiCol_DragDropTarget]   = uicolors::Pick(uicolors::cSuccessThemed, dark);
}

f32 CalcRaisedGradient(bool dark)
{
    return uicolors::Pick(cRaisedGradient, dark) * sThemeAdjust.gradient;
}

f32 CalcSurfaceGradient(bool dark)
{
    return uicolors::Pick(cSurfaceGradient, dark) * sThemeAdjust.gradient;
}

static void ApplyWindowShadowAlpha()
{
    const f32 alpha = uicolors::Pick(cWindowShadowAlpha, sThemeIsDark);

    ImGui::SetWindowShadowAlpha(sWindowShadows ? alpha : 0.0f);
}

void SetWindowShadows(bool enabled)
{
    sWindowShadows = enabled;
    ApplyWindowShadowAlpha();
}

void ApplyTheme()
{
    ImVec4* colors = ImGui::GetStyle().Colors;

    CalcThemeColors(gAccentColor, sThemeIsDark, colors);
    ApplyWindowShadowAlpha();
    ImGui::SetRaisedGradient(CalcRaisedGradient(sThemeIsDark));
    ImGui::SetSurfaceGradient(CalcSurfaceGradient(sThemeIsDark));

    const ImVec4& windowBg = colors[ImGuiCol_WindowBg];
    util::setClearColor(sead::Color4f(windowBg.x, windowBg.y, windowBg.z, 1.0f));
}

ImU32 GetRecessedPaneColorU32()
{
    const ImVec4& bg = ImGui::GetStyleColorVec4(ImGuiCol_ChildBg);

    f32 hue;
    f32 saturation;
    f32 value;
    ImGui::ColorConvertRGBtoHSV(bg.x, bg.y, bg.z, hue, saturation, value);
    value *= uicolors::Pick(cRecessedPaneValue, sThemeIsDark);

    return ImGui::ColorConvertFloat4ToU32(uicolors::FromHsv(hue, saturation, value, bg.w));
}

ImU32 GetTrackColorU32(ImU32 frameColor)
{
    if (sThemeIsDark)
        return frameColor;

    const ImVec4 ink = uicolors::Opaque(ImGui::GetStyleColorVec4(ImGuiCol_Text));

    return ImGui::ColorConvertFloat4ToU32(ImLerp(ImGui::ColorConvertU32ToFloat4(frameColor), ink, cLightTrackInkMix));
}

ImVec4 GetSeriesColor(s32 index)
{
    const ImVec4& accent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);

    f32 hue;
    f32 saturation;
    f32 value;
    ImGui::ColorConvertRGBtoHSV(accent.x, accent.y, accent.z, hue, saturation, value);
    hue = ImFmod(hue + static_cast<f32>(index) * cSeriesHueStep, 1.0f);
    saturation = ImMax(saturation, cSeriesSaturationMin);

    return uicolors::FromHsv(hue, saturation, value, accent.w);
}

ImVec4 GetThemeAccent()
{
    return uicolors::Opaque(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));
}

ImVec4 GetOnAccentTextColor()
{
    if (sThemeIsDark)
        return ImGui::GetStyleColorVec4(ImGuiCol_Text);

    return uicolors::cInkOnDark;
}

ImVec4 GetAccentTextColor()
{
    return ImLerp(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), ImGui::GetStyleColorVec4(ImGuiCol_Text), cAccentTextMix);
}
