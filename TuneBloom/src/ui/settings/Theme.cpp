#define IMGUI_DEFINE_MATH_OPERATORS
#include <ui/settings/Theme.h>

#include <imgui/imgui_custom.h>
#include <imgui/imgui_internal.h>

#include <app/Utilll.h>

ImVec4 gAccentColor = cDefaultAccentColor;
bool gThemeIsDark = true;

static ThemeAdjust sThemeAdjust;
static bool sWindowShadows = true;

ThemeAdjust& GetThemeAdjust()
{
    return sThemeAdjust;
}

static ImVec4 HSV(float h, float s, float v, float a = 1.0f)
{
    s *= sThemeAdjust.saturation;
    if (s > 1.0f) s = 1.0f;
    if (s < 0.0f) s = 0.0f;

    v *= sThemeAdjust.brightness;
    v = (v - 0.5f) * sThemeAdjust.contrast + 0.5f;
    if (v > 1.0f) v = 1.0f;
    if (v < 0.0f) v = 0.0f;

    ImVec4 c;
    ImGui::ColorConvertHSVtoRGB(h, s, v, c.x, c.y, c.z);
    c.w = a;
    return c;
}

static constexpr float cDimTextSaturation = 0.20f;
static constexpr float cDimTextLuma       = 0.50f;

static ImVec4 Neutral(float h, float s, float luma)
{
    s *= sThemeAdjust.saturation;
    if (s > 1.0f) s = 1.0f;
    if (s < 0.0f) s = 0.0f;

    ImVec4 c;
    ImGui::ColorConvertHSVtoRGB(h, s, 1.0f, c.x, c.y, c.z);

    const float scale = luma / (0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z);
    return ImVec4(ImMin(c.x * scale, 1.0f), ImMin(c.y * scale, 1.0f), ImMin(c.z * scale, 1.0f), 1.0f);
}

void CalcThemeColors(ImVec4 accent, bool dark, ImVec4* colors)
{
    float h, s, v;
    ImGui::ColorConvertRGBtoHSV(accent.x, accent.y, accent.z, h, s, v);
    if (s < 0.05f) h = 0.0f;

    auto C = [&](float s, float v, float a = 1.0f) { return HSV(h, s, v, a); };
    auto D = [&](float ds, float dv, float ls, float lv) { return dark ? C(ds, dv) : C(ls, lv); };
    auto F = [&](ImVec4 d, ImVec4 l) { return dark ? d : l; };

    colors[ImGuiCol_Text]              = F({0.92f, 0.92f, 0.93f, 0.90f}, {0.07f, 0.07f, 0.08f, 0.97f});
    colors[ImGuiCol_TextDisabled]      = Neutral(h, cDimTextSaturation, cDimTextLuma);

    colors[ImGuiCol_WindowBg]          = D(0.40f, 0.17f, 0.012f, 0.950f);
    colors[ImGuiCol_ChildBg]           = D(0.35f, 0.13f, 0.00f, 1.00f);
    colors[ImGuiCol_PopupBg]           = dark ? C(0.38f, 0.15f, 0.85f) : C(0.01f, 0.985f, 0.98f);
    colors[ImGuiCol_Border]            = dark ? C(0.30f, 0.40f, 0.65f) : C(0.04f, 0.78f, 0.90f);
    colors[ImGuiCol_BorderShadow]      = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    colors[ImGuiCol_FrameBg]           = D(0.42f, 0.22f, 0.02f, 0.895f);
    colors[ImGuiCol_FrameBgHovered]    = dark ? C(0.45f, 0.32f, 0.40f) : C(0.03f, 0.86f);
    colors[ImGuiCol_FrameBgActive]     = dark ? C(0.48f, 0.38f, 0.45f) : C(0.05f, 0.82f);

    colors[ImGuiCol_TitleBg]           = dark ? C(0.30f, 0.12f, 0.83f) : C(0.015f, 0.905f);
    colors[ImGuiCol_TitleBgActive]     = dark ? C(0.32f, 0.14f, 0.87f) : C(0.02f, 0.885f);
    colors[ImGuiCol_TitleBgCollapsed]  = dark ? C(0.35f, 0.20f, 0.20f) : C(0.02f, 0.92f, 0.60f);
    colors[ImGuiCol_MenuBarBg]         = dark ? C(0.35f, 0.13f, 0.80f) : C(0.012f, 0.93f);

    colors[ImGuiCol_ScrollbarBg]           = dark ? C(0.38f, 0.20f, 0.60f) : ImVec4(0.00f, 0.00f, 0.00f, 0.04f);
    colors[ImGuiCol_ScrollbarGrab]         = dark ? C(0.40f, 0.38f, 0.51f) : ImVec4(0.00f, 0.00f, 0.00f, 0.28f);
    colors[ImGuiCol_ScrollbarGrabHovered]  = dark ? C(0.45f, 0.48f, 1.00f) : ImVec4(0.00f, 0.00f, 0.00f, 0.36f);
    colors[ImGuiCol_ScrollbarGrabActive]   = dark ? C(0.50f, 0.56f, 0.91f) : ImVec4(0.00f, 0.00f, 0.00f, 0.48f);

    colors[ImGuiCol_CheckMark]             = dark ? C(0.60f, 0.80f, 0.83f) : C(0.80f, 0.92f);
    colors[ImGuiCol_SliderGrab]            = dark ? C(0.40f, 0.45f, 0.62f) : C(0.78f, 0.92f);
    colors[ImGuiCol_SliderGrabActive]      = dark ? C(0.50f, 0.65f, 0.84f) : C(0.88f, 0.80f);

    colors[ImGuiCol_Button]                = dark ? C(0.50f, 0.60f, 0.49f) : C(0.30f, 0.20f, 0.07f);
    colors[ImGuiCol_ButtonHovered]         = dark ? C(0.50f, 0.70f, 0.68f) : C(0.30f, 0.20f, 0.12f);
    colors[ImGuiCol_ButtonActive]          = dark ? C(0.55f, 0.50f, 1.00f) : C(0.30f, 0.20f, 0.19f);

    colors[ImGuiCol_Header]                = dark ? C(0.45f, 0.50f, 0.53f) : C(0.70f, 0.95f, 0.24f);
    colors[ImGuiCol_HeaderHovered]         = dark ? C(0.50f, 0.65f, 0.28f) : C(0.60f, 0.95f, 0.10f);
    colors[ImGuiCol_HeaderActive]          = dark ? C(0.55f, 0.55f, 0.60f) : C(0.70f, 0.90f, 0.36f);

    colors[ImGuiCol_Separator]             = dark ? C(0.15f, 0.30f, 0.50f) : C(0.03f, 0.80f);
    colors[ImGuiCol_SeparatorHovered]      = dark ? C(0.40f, 0.55f, 0.78f) : C(0.50f, 0.85f, 0.80f);
    colors[ImGuiCol_SeparatorActive]       = dark ? C(0.50f, 0.65f, 1.00f) : C(0.70f, 0.90f);

    colors[ImGuiCol_ResizeGrip]            = F({1.00f, 1.00f, 1.00f, 0.85f}, {0.00f, 0.00f, 0.00f, 0.25f});
    colors[ImGuiCol_ResizeGripHovered]     = F({1.00f, 1.00f, 1.00f, 0.60f}, {0.00f, 0.00f, 0.00f, 0.45f});
    colors[ImGuiCol_ResizeGripActive]      = F({1.00f, 1.00f, 1.00f, 0.90f}, {0.00f, 0.00f, 0.00f, 0.65f});

    colors[ImGuiCol_Tab]                   = dark ? C(0.35f, 0.18f) : ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TabHovered]            = dark ? C(0.50f, 0.55f, 0.80f) : C(0.012f, 0.935f);
    colors[ImGuiCol_TabActive]             = dark ? C(0.38f, 0.24f) : C(0.012f, 0.950f);
    colors[ImGuiCol_TabUnfocused]          = dark ? C(0.30f, 0.14f) : ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TabUnfocusedActive]    = D(0.32f, 0.20f, 0.012f, 0.950f);

    colors[ImGuiCol_DockingPreview]        = dark ? C(0.50f, 0.65f, 0.70f) : C(0.75f, 0.90f, 0.40f);
    colors[ImGuiCol_DockingEmptyBg]        = D(0.00f, 0.20f, 0.012f, 0.93f);

    colors[ImGuiCol_PlotLines]             = dark ? ImVec4(1.00f, 1.00f, 1.00f, 1.00f) : C(0.85f, 0.78f);
    colors[ImGuiCol_PlotLinesHovered]      = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogram]         = dark ? C(0.55f, 0.65f, 1.00f) : C(0.75f, 0.90f);
    colors[ImGuiCol_PlotHistogramHovered]  = dark ? C(0.55f, 0.80f, 1.00f) : C(0.85f, 0.80f);

    colors[ImGuiCol_TableHeaderBg]         = D(0.35f, 0.22f, 0.012f, 0.955f);
    colors[ImGuiCol_TableBorderStrong]     = D(0.20f, 0.25f, 0.03f, 0.75f);
    colors[ImGuiCol_TableBorderLight]      = D(0.15f, 0.18f, 0.02f, 0.85f);
    colors[ImGuiCol_TableRowBg]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt]         = F({1.00f, 1.00f, 1.00f, 0.06f}, {0.00f, 0.00f, 0.00f, 0.03f});

    colors[ImGuiCol_TextSelectedBg]        = dark ? C(0.40f, 0.40f, 0.35f) : C(0.55f, 1.00f, 0.40f);
    colors[ImGuiCol_DragDropTarget]        = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);

    colors[ImGuiCol_NavHighlight]          = dark ? C(0.50f, 0.60f, 1.00f) : C(0.75f, 0.90f, 0.70f);
    colors[ImGuiCol_NavWindowingHighlight] = F({1.00f, 1.00f, 1.00f, 0.70f}, {0.00f, 0.00f, 0.00f, 0.40f});
    colors[ImGuiCol_NavWindowingDimBg]     = F({0.00f, 0.00f, 0.00f, 0.30f}, {0.00f, 0.00f, 0.00f, 0.20f});
    colors[ImGuiCol_ModalWindowDimBg]      = F({0.00f, 0.00f, 0.00f, 0.45f}, {0.00f, 0.00f, 0.00f, 0.25f});
}

static constexpr f32 cWindowShadowAlphaDark  = 0.50f;
static constexpr f32 cWindowShadowAlphaLight = 0.20f;
static constexpr f32 cRaisedGradientDark     = 0.03f;
static constexpr f32 cRaisedGradientLight    = 0.015f;
static constexpr f32 cSurfaceGradientDark    = 0.02f;
static constexpr f32 cSurfaceGradientLight   = 0.0075f;

f32 CalcRaisedGradient(bool dark)
{
    return (dark ? cRaisedGradientDark : cRaisedGradientLight) * sThemeAdjust.gradient;
}

f32 CalcSurfaceGradient(bool dark)
{
    return (dark ? cSurfaceGradientDark : cSurfaceGradientLight) * sThemeAdjust.gradient;
}

static void ApplyWindowShadowAlpha()
{
    const f32 alpha = gThemeIsDark ? cWindowShadowAlphaDark : cWindowShadowAlphaLight;

    ImGui::SetWindowShadowAlpha(sWindowShadows ? alpha : 0.0f);
}

void SetWindowShadows(bool enabled)
{
    sWindowShadows = enabled;
    ApplyWindowShadowAlpha();
}

void ApplyThemeFromAccent(ImVec4 accent)
{
    ImVec4* colors = ImGui::GetStyle().Colors;

    CalcThemeColors(accent, gThemeIsDark, colors);
    ApplyWindowShadowAlpha();
    ImGui::SetRaisedGradient(CalcRaisedGradient(gThemeIsDark));
    ImGui::SetSurfaceGradient(CalcSurfaceGradient(gThemeIsDark));

    const ImVec4& wbg = colors[ImGuiCol_WindowBg];
    util::setClearColor(sead::Color4f(wbg.x, wbg.y, wbg.z, 1.0f));
}

static constexpr f32 cRecessedPaneValueDark  = 0.72f;
static constexpr f32 cRecessedPaneValueLight = 0.965f;

ImU32 GetRecessedPaneColorU32()
{
    const ImVec4& bg = ImGui::GetStyleColorVec4(ImGuiCol_ChildBg);

    f32 h, s, v;
    ImGui::ColorConvertRGBtoHSV(bg.x, bg.y, bg.z, h, s, v);
    v *= gThemeIsDark ? cRecessedPaneValueDark : cRecessedPaneValueLight;

    ImVec4 c(0.0f, 0.0f, 0.0f, bg.w);
    ImGui::ColorConvertHSVtoRGB(h, s, v, c.x, c.y, c.z);
    return ImGui::ColorConvertFloat4ToU32(c);
}

static constexpr f32 cLightTrackInkMix = 0.13f;

ImU32 GetTrackColorU32(ImU32 frameColor)
{
    if (gThemeIsDark)
        return frameColor;

    ImVec4 ink = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    ink.w = 1.0f;

    return ImGui::ColorConvertFloat4ToU32(ImLerp(ImGui::ColorConvertU32ToFloat4(frameColor), ink, cLightTrackInkMix));
}

static constexpr f32 cSeriesHueStep       = 0.618034f;
static constexpr f32 cSeriesSaturationMin = 0.5f;

ImVec4 GetSeriesColor(s32 index)
{
    const ImVec4& accent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);

    f32 h, s, v;
    ImGui::ColorConvertRGBtoHSV(accent.x, accent.y, accent.z, h, s, v);
    h = ImFmod(h + static_cast<f32>(index) * cSeriesHueStep, 1.0f);
    s = ImMax(s, cSeriesSaturationMin);

    ImVec4 c(0.0f, 0.0f, 0.0f, accent.w);
    ImGui::ColorConvertHSVtoRGB(h, s, v, c.x, c.y, c.z);
    return c;
}

static constexpr f32 cAccentTextMix = 0.3f;

ImVec4 GetAccentTextColor()
{
    return ImLerp(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), ImGui::GetStyleColorVec4(ImGuiCol_Text), cAccentTextMix);
}
