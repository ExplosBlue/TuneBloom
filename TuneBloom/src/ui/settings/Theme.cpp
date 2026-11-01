#include <ui/settings/Theme.h>

#include <ui/settings/Config.h>
#include <ui/settings/UiColors.h>

#include <imgui/imgui_custom.h>
#include <imgui/imgui_internal.h>

#include <app/Utilll.h>

#include <cmath>

static constexpr f32 cKeepTokenAlpha      = -1.0f;
static constexpr f32 cGrayscaleSaturation = 0.05f;
static constexpr f32 cSeriesHueStep       = 0.618034f;
static constexpr f32 cAccentTextMix       = 0.3f;

static constexpr f32 cAccentStoreSaturation = 0.50f;
static constexpr f32 cAccentStoreValue      = 0.60f;
static constexpr f32 cHueReferenceSaturation = 0.486f;
static constexpr f32 cAccentChromaMin       = 0.075f;
static constexpr f32 cAccentRowLightnessMax = 0.5f;
static constexpr f32 cAccentLightnessMin    = 0.2f;
static constexpr f32 cAccentLightnessMax    = 0.95f;
static constexpr f32 cSurfaceChroma         = 0.32f;
static constexpr f32 cGuardStep             = 0.01f;
static constexpr s32 cGuardStepMax          = 80;
static constexpr s32 cGamutSearchSteps      = 16;

static constexpr f32 cTextContrastWindow  = 7.0f;
static constexpr f32 cTextContrastSurface = 4.5f;
static constexpr f32 cDimContrastWindow   = 3.5f;
static constexpr f32 cDimContrastPopup    = 3.2f;
static constexpr f32 cAccentTextContrast  = 4.5f;
static constexpr f32 cAccentMarkContrast  = 3.0f;

static constexpr uicolors::Themed<f32> cSurfaceLightnessLimit = { 0.48f, 0.55f };

static constexpr uicolors::Themed<f32> cSurfaceTint       = { 0.50f, 0.25f };
static constexpr uicolors::Themed<f32> cSeriesLightness   = { 0.70f, 0.58f };
static constexpr uicolors::Themed<f32> cSeriesChroma      = { 0.16f, 0.16f };
static constexpr uicolors::Themed<f32> cTextChroma        = { 0.045f, 0.016f };
static constexpr uicolors::Themed<f32> cDimChroma         = { 0.12f, 0.04f };
static constexpr uicolors::Themed<f32> cDimMix            = { 0.62f, 0.50f };
static constexpr uicolors::Themed<f32> cAccentCuspWeight  = { 0.30f, 0.35f };

static constexpr uicolors::Themed<f32> cWindowShadowMaxAlpha = { 1.00f, 0.40f };
static constexpr uicolors::Themed<f32> cRaisedGradient    = { 0.03f, 0.015f };
static constexpr uicolors::Themed<f32> cSurfaceGradient   = { 0.02f, 0.0075f };
static constexpr uicolors::Themed<f32> cRecessedPaneValue = { 0.72f, 0.965f };
static constexpr uicolors::Themed<f32> cTrackInkMix       = { 0.18f, 0.13f };

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

struct Oklch
{
    f32 lightness;
    f32 chroma;
    f32 hue;
};

struct ContrastNeed
{
    ImVec4 backdrop;
    f32 ratio;
};

struct ThemeTint
{
    bool dark;
    f32 windowLightness;
    f32 surfaceChroma;
    f32 hueAngle;
    f32 accentShift;
};

struct GuardedAccentText
{
    ImVec4 source;
    ImVec4 window;
    ImVec4 popup;
    bool dark = false;
    bool valid = false;
    ImVec4 result;
};

}

ImVec4 gAccentColor = uicolors::cAccentDefault;
static bool sThemeIsDark = true;

static ThemeAdjust sThemeAdjust;
static f32 sWindowShadowOpacity = cShadowOpacityDefault;

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

static f32 ToLinear(f32 channel)
{
    return channel <= 0.04045f ? channel / 12.92f : powf((channel + 0.055f) / 1.055f, 2.4f);
}

static f32 ToGamma(f32 channel)
{
    channel = ImSaturate(channel);

    return channel <= 0.0031308f ? channel * 12.92f : 1.055f * powf(channel, 1.0f / 2.4f) - 0.055f;
}

static Oklch ToOklch(const ImVec4& color)
{
    const f32 red = ToLinear(color.x);
    const f32 green = ToLinear(color.y);
    const f32 blue = ToLinear(color.z);

    const f32 l = cbrtf(0.4122214708f * red + 0.5363325363f * green + 0.0514459929f * blue);
    const f32 m = cbrtf(0.2119034982f * red + 0.6806995451f * green + 0.1073969566f * blue);
    const f32 s = cbrtf(0.0883024619f * red + 0.2817188376f * green + 0.6299787005f * blue);

    const f32 lightness = 0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s;
    const f32 a = 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s;
    const f32 b = 0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s;

    return { lightness, sqrtf(a * a + b * b), atan2f(b, a) };
}

static ImVec4 ToLinearRgb(f32 lightness, f32 chroma, f32 hue)
{
    const f32 a = chroma * cosf(hue);
    const f32 b = chroma * sinf(hue);

    const f32 l = lightness + 0.3963377774f * a + 0.2158037573f * b;
    const f32 m = lightness - 0.1055613458f * a - 0.0638541728f * b;
    const f32 s = lightness - 0.0894841775f * a - 1.2914855480f * b;

    const f32 l3 = l * l * l;
    const f32 m3 = m * m * m;
    const f32 s3 = s * s * s;

    return ImVec4(4.0767416621f * l3 - 3.3077115913f * m3 + 0.2309699292f * s3, -1.2684380046f * l3 + 2.6097574011f * m3 - 0.3413193965f * s3, -0.0041960863f * l3 - 0.7034186147f * m3 + 1.7076147010f * s3, 1.0f);
}

static bool IsInGamut(const ImVec4& linear)
{
    constexpr f32 cTolerance = 0.0001f;

    return linear.x >= -cTolerance && linear.x <= 1.0f + cTolerance && linear.y >= -cTolerance && linear.y <= 1.0f + cTolerance && linear.z >= -cTolerance && linear.z <= 1.0f + cTolerance;
}

static f32 CalcGamutChroma(f32 lightness, f32 chroma, f32 hue)
{
    if (IsInGamut(ToLinearRgb(lightness, chroma, hue)))
        return chroma;

    f32 low = 0.0f;
    f32 high = chroma;

    for (s32 i = 0; i < cGamutSearchSteps; i++)
    {
        const f32 middle = (low + high) * 0.5f;

        if (IsInGamut(ToLinearRgb(lightness, middle, hue)))
            low = middle;
        else
            high = middle;
    }

    return low;
}

static ImVec4 FromOklch(f32 lightness, f32 chroma, f32 hue, f32 alpha = 1.0f)
{
    lightness = ImSaturate(lightness);

    const ImVec4 linear = ToLinearRgb(lightness, CalcGamutChroma(lightness, chroma, hue), hue);

    return ImVec4(ToGamma(linear.x), ToGamma(linear.y), ToGamma(linear.z), alpha);
}

static f32 CalcLuminance(const ImVec4& color)
{
    return 0.2126f * ToLinear(color.x) + 0.7152f * ToLinear(color.y) + 0.0722f * ToLinear(color.z);
}

static f32 CalcContrast(const ImVec4& a, const ImVec4& b)
{
    const f32 first = CalcLuminance(a);
    const f32 second = CalcLuminance(b);

    return (ImMax(first, second) + 0.05f) / (ImMin(first, second) + 0.05f);
}

static ImVec4 Flatten(const ImVec4& color, const ImVec4& backdrop)
{
    return uicolors::Opaque(ImLerp(backdrop, color, color.w));
}

static ImVec4 GuardContrast(Oklch color, f32 alpha, bool dark, const ContrastNeed* needs, s32 needNum)
{
    const f32 step = dark ? cGuardStep : -cGuardStep;

    for (s32 i = 0; i < cGuardStepMax; i++)
    {
        const ImVec4 result = FromOklch(color.lightness, color.chroma, color.hue, alpha);

        bool isLegible = true;

        for (s32 j = 0; j < needNum && isLegible; j++)
            isLegible = CalcContrast(uicolors::Opaque(result), needs[j].backdrop) >= needs[j].ratio;

        if (isLegible)
            return result;

        color.lightness = ImSaturate(color.lightness + step);
    }

    return FromOklch(color.lightness, color.chroma, color.hue, alpha);
}

static f32 CalcHueAngle(f32 hue)
{
    return ToOklch(uicolors::FromHsv(hue, cHueReferenceSaturation, 1.0f)).hue;
}

static f32 CalcCuspLightness(f32 hue)
{
    constexpr f32 cLightnessMin  = 0.3f;
    constexpr f32 cLightnessMax  = 0.98f;
    constexpr f32 cLightnessStep = 0.02f;
    constexpr f32 cChromaMax     = 0.4f;

    f32 bestChroma = 0.0f;
    f32 bestLightness = 0.5f;

    for (f32 lightness = cLightnessMin; lightness <= cLightnessMax; lightness += cLightnessStep)
    {
        const f32 chroma = CalcGamutChroma(lightness, cChromaMax, hue);

        if (chroma > bestChroma)
        {
            bestChroma = chroma;
            bestLightness = lightness;
        }
    }

    return bestLightness;
}

static f32 CalcAccentLightnessShift(f32 hueAngle, bool dark)
{
    static const f32 sDefaultCuspLightness = CalcCuspLightness(CalcHueAngle(cThemeHueDefault));

    return (CalcCuspLightness(hueAngle) - sDefaultCuspLightness) * uicolors::Pick(cAccentCuspWeight, dark);
}

static f32 MapSurfaceLightness(f32 lightness, f32 windowLightness, bool dark)
{
    if (dark)
        return ImSaturate(windowLightness * sThemeAdjust.brightness + (lightness - windowLightness) * sThemeAdjust.contrast);

    const f32 windowDepth = 1.0f - windowLightness;

    return ImSaturate(1.0f - (windowDepth / sThemeAdjust.brightness + ((1.0f - lightness) - windowDepth) * sThemeAdjust.contrast));
}

static f32 LimitSurfaceLightness(f32 lightness, bool dark)
{
    const f32 limit = uicolors::Pick(cSurfaceLightnessLimit, dark);

    return dark ? ImMin(lightness, limit) : ImMax(lightness, limit);
}

static f32 CalcSurfaceTint(bool dark)
{
    return uicolors::Pick(cSurfaceTint, dark) * sThemeAdjust.saturation;
}

static f32 MapAccentLightness(f32 lightness, f32 shift)
{
    return ImClamp((lightness + shift) * sThemeAdjust.brightness, cAccentLightnessMin, cAccentLightnessMax);
}

void SetAccentHue(f32 hue)
{
    gAccentColor = uicolors::FromHsv(hue, cAccentStoreSaturation, cAccentStoreValue);
}

ImVec4 CalcAccentPreviewColor(f32 hue, bool dark)
{
    const Oklch token = ToOklch(dark ? uicolors::token::cAccent : uicolors::light::cCheckMark);
    const f32 angle = CalcHueAngle(hue);
    const f32 lightness = MapAccentLightness(token.lightness, CalcAccentLightnessShift(angle, dark));

    return FromOklch(lightness, token.chroma * ImMax(sThemeAdjust.saturation, 0.35f), angle);
}

static f32 CalcAccentHue(const ImVec4& accent)
{
    f32 hue;
    f32 saturation;
    f32 value;
    ImGui::ColorConvertRGBtoHSV(accent.x, accent.y, accent.z, hue, saturation, value);

    return saturation < cGrayscaleSaturation ? 0.0f : hue;
}

f32 GetAccentHue()
{
    return CalcAccentHue(gAccentColor);
}

static ImVec4 TintToken(const ThemeTint& tint, const ImVec4& token, f32 alpha)
{
    const Oklch color = ToOklch(token);
    const f32 outAlpha = alpha < 0.0f ? token.w : alpha;

    if (color.chroma < cAccentChromaMin)
    {
        f32 lightness = MapSurfaceLightness(color.lightness, tint.windowLightness, tint.dark);

        if (outAlpha >= 1.0f)
            lightness = LimitSurfaceLightness(lightness, tint.dark);

        return FromOklch(lightness, tint.surfaceChroma * cSurfaceChroma * ImMin(lightness, 1.0f - lightness), tint.hueAngle, outAlpha);
    }

    const f32 chroma = color.chroma * sThemeAdjust.saturation;

    if (tint.dark && color.lightness < cAccentRowLightnessMax)
        return FromOklch(LimitSurfaceLightness(MapSurfaceLightness(color.lightness, tint.windowLightness, tint.dark), tint.dark), chroma, tint.hueAngle, outAlpha);

    return FromOklch(MapAccentLightness(color.lightness, tint.accentShift), chroma, tint.hueAngle, outAlpha);
}

static void CalcTextColors(const ThemeTint& tint, ImVec4* colors)
{
    const bool dark = tint.dark;
    const ImVec4 window = uicolors::Opaque(colors[ImGuiCol_WindowBg]);
    const ImVec4& textToken = dark ? uicolors::token::cText : uicolors::light::cText;

    const ContrastNeed textNeeds[] =
    {
        { window,                                         cTextContrastWindow  },
        { Flatten(colors[ImGuiCol_Button], window),       cTextContrastSurface },
        { Flatten(colors[ImGuiCol_Header], window),       cTextContrastSurface },
        { Flatten(colors[ImGuiCol_FrameBg], window),      cTextContrastSurface },
        { Flatten(colors[ImGuiCol_PopupBg], window),      cTextContrastSurface },
    };

    const Oklch text = { ToOklch(textToken).lightness, tint.surfaceChroma * uicolors::Pick(cTextChroma, dark), tint.hueAngle };
    colors[ImGuiCol_Text] = GuardContrast(text, textToken.w, dark, textNeeds, IM_ARRAYSIZE(textNeeds));

    const ContrastNeed dimNeeds[] =
    {
        { window,                                    cDimContrastWindow },
        { Flatten(colors[ImGuiCol_PopupBg], window), cDimContrastPopup  },
    };

    const f32 dimLightness = ImLerp(ToOklch(window).lightness, ToOklch(colors[ImGuiCol_Text]).lightness, uicolors::Pick(cDimMix, dark));
    const Oklch dim = { dimLightness, tint.surfaceChroma * uicolors::Pick(cDimChroma, dark), tint.hueAngle };
    colors[ImGuiCol_TextDisabled] = GuardContrast(dim, 1.0f, dark, dimNeeds, IM_ARRAYSIZE(dimNeeds));

    if (dark)
        colors[ImGuiCol_PlotLines] = colors[ImGuiCol_Text];
}

static void GuardAccentMarks(bool dark, ImVec4* colors)
{
    static constexpr ImGuiCol cMarks[] = { ImGuiCol_CheckMark, ImGuiCol_SliderGrab, ImGuiCol_PlotHistogram, ImGuiCol_SeparatorActive, ImGuiCol_NavHighlight };

    const ImVec4 window = uicolors::Opaque(colors[ImGuiCol_WindowBg]);

    const ContrastNeed needs[] =
    {
        { window,                                    cAccentMarkContrast },
        { Flatten(colors[ImGuiCol_FrameBg], window), cAccentMarkContrast },
    };

    for (const ImGuiCol mark : cMarks)
        colors[mark] = GuardContrast(ToOklch(colors[mark]), colors[mark].w, dark, needs, IM_ARRAYSIZE(needs));
}

void CalcThemeColors(ImVec4 accent, bool dark, ImVec4* colors)
{
    using namespace uicolors::token;

    static constexpr ThemeRole cRoles[] =
    {
        { ImGuiCol_Text,                  Fixed(cText),                      Fixed(uicolors::light::cText) },
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
        { ImGuiCol_PlotLines,             Fixed(cText),                      Tinted(uicolors::light::cPlotLines) },
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

    ThemeTint tint;
    tint.dark = dark;
    tint.windowLightness = ToOklch(dark ? cWindow : uicolors::light::cWindowBg).lightness;
    tint.surfaceChroma = CalcSurfaceTint(dark);
    tint.hueAngle = CalcHueAngle(CalcAccentHue(accent));
    tint.accentShift = CalcAccentLightnessShift(tint.hueAngle, dark);

    for (const ThemeRole& role : cRoles)
    {
        const ThemeSlot& slot = dark ? role.dark : role.light;

        colors[role.index] = slot.tint ? TintToken(tint, slot.color, slot.alpha) : slot.color;
    }

    CalcTextColors(tint, colors);
    GuardAccentMarks(dark, colors);

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
    const f32 alpha = uicolors::Pick(cWindowShadowMaxAlpha, sThemeIsDark);

    ImGui::SetWindowShadowAlpha(alpha * sWindowShadowOpacity);
}

void SetWindowShadowOpacity(f32 opacity)
{
    sWindowShadowOpacity = opacity;
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
    const ImVec4 ink = uicolors::Opaque(ImGui::GetStyleColorVec4(ImGuiCol_Text));

    return ImGui::ColorConvertFloat4ToU32(ImLerp(ImGui::ColorConvertU32ToFloat4(frameColor), ink, uicolors::Pick(cTrackInkMix, sThemeIsDark)));
}

ImVec4 GetSeriesColor(s32 index)
{
    const f32 angle = CalcHueAngle(CalcAccentHue(gAccentColor)) + static_cast<f32>(index) * cSeriesHueStep * IM_PI * 2.0f;

    return FromOklch(uicolors::Pick(cSeriesLightness, sThemeIsDark), uicolors::Pick(cSeriesChroma, sThemeIsDark), angle);
}

ImVec4 GetAccentAt(f32 lightness, f32 chroma)
{
    return FromOklch(lightness, chroma, CalcHueAngle(CalcAccentHue(gAccentColor)));
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

static ImVec4 GuardAccentText(const ImVec4& color, GuardedAccentText* last)
{
    const ImVec4 window = uicolors::Opaque(ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
    const ImVec4 popup = Flatten(ImGui::GetStyleColorVec4(ImGuiCol_PopupBg), window);

    if (last->valid && last->dark == sThemeIsDark && last->source == color && last->window == window && last->popup == popup)
        return last->result;

    const ContrastNeed needs[] =
    {
        { window, cAccentTextContrast },
        { popup,  cAccentTextContrast },
    };

    last->source = color;
    last->window = window;
    last->popup = popup;
    last->dark = sThemeIsDark;
    last->valid = true;
    last->result = GuardContrast(ToOklch(color), color.w, sThemeIsDark, needs, IM_ARRAYSIZE(needs));

    return last->result;
}

ImVec4 GetLegibleAccentColor()
{
    static GuardedAccentText sLast;

    return GuardAccentText(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), &sLast);
}

ImVec4 GetAccentTextColor()
{
    static GuardedAccentText sLast;

    return GuardAccentText(ImLerp(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), ImGui::GetStyleColorVec4(ImGuiCol_Text), cAccentTextMix), &sLast);
}
