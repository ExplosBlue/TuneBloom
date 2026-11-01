#include <ui/settings/AppearancePage.h>

#include <messages/Messages.h>
#include <ui/settings/Config.h>
#include <ui/settings/Theme.h>
#include <ui/settings/UiColors.h>
#include <ui/widgets/Knob.h>
#include <ui/widgets/Properties.h>
#include <ui/widgets/Widgets.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_custom.h>
#include <imgui/imgui_internal.h>

#include <prim/seadSafeString.h>

#include <cstring>

static constexpr f32 cCardHeightEm = 4.2f;
static constexpr f32 cCardGapEm    = 0.6f;

static constexpr f32 cDegreesPerTurn   = 360.0f;
static constexpr f32 cDegreesMax       = 359.0f;
static constexpr f32 cPercentScale     = 100.0f;
static constexpr f32 cHueKnobSpeed     = 1.0f;
static constexpr f32 cAmountKnobSpeed  = 0.01f;
static constexpr f32 cPercentKnobSpeed = 0.5f;

static constexpr const char* cAmountFormat  = "%.2f";
static constexpr const char* cDegreesFormat = "%.0f";

static const char* const cKnobLabels[] = {
    messages::preferences::cHueLabel,
    messages::preferences::cSaturationLabel,
    messages::preferences::cBrightnessLabel,
    messages::preferences::cContrastLabel,
    messages::preferences::cGradientLabel,
    messages::preferences::cShadowKnobLabel,
};

static bool DrawThemeCard(const char* label, f32 width, bool dark, bool selected)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 em = ImGui::GetFontSize();
    const ImVec2 size(width, ImFloor(em * cCardHeightEm));
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 max = pos + size;
    const f32 rounding = style.FrameRounding * 2.0f;
    const f32 pad = ImFloor(em * 0.5f);
    const f32 barHeight = ImFloor(em * 0.7f);
    const f32 rowHeight = ImFloor(em * 0.8f);

    const bool pressed = ImGui::InvisibleButton(label, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();

    ImVec4 colors[ImGuiCol_COUNT];
    memcpy(colors, style.Colors, sizeof(colors));
    CalcThemeColors(gAccentColor, dark, colors);

    auto color = [&colors](ImGuiCol index)
    {
        const ImVec4& backdrop = colors[ImGuiCol_WindowBg];

        return ImGui::ColorConvertFloat4ToU32(uicolors::Opaque(ImLerp(backdrop, colors[index], colors[index].w)));
    };

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // Window and title bar
    ImGui::AddRectFilledGradient(drawList, ImVec2(pos.x, pos.y + barHeight), max, color(ImGuiCol_WindowBg), CalcSurfaceGradient(dark), rounding, ImDrawFlags_RoundCornersBottom);
    drawList->AddRectFilled(pos, ImVec2(max.x, pos.y + barHeight), color(ImGuiCol_TitleBgActive), rounding, ImDrawFlags_RoundCornersTop);

    const f32 rowY = pos.y + barHeight + pad;
    const f32 innerWidth = size.x - pad * 2.0f;
    const f32 rowWidth = ImFloor(innerWidth * 0.32f);
    const f32 buttonWidth = ImFloor(innerWidth * 0.2f);
    const f32 fieldMin = pos.x + pad + rowWidth + pad;
    const f32 fieldMax = max.x - pad - buttonWidth - pad;

    drawList->AddRectFilled(ImVec2(pos.x + pad, rowY), ImVec2(pos.x + pad + rowWidth, rowY + rowHeight), color(ImGuiCol_Header), style.FrameRounding);
    drawList->AddRectFilled(ImVec2(fieldMin, rowY), ImVec2(fieldMax, rowY + rowHeight), color(ImGuiCol_FrameBg), style.FrameRounding);
    drawList->AddRectFilled(ImVec2(fieldMin, rowY), ImVec2(ImFloor(fieldMin + (fieldMax - fieldMin) * 0.55f), rowY + rowHeight), color(ImGuiCol_SliderGrab), style.FrameRounding, ImDrawFlags_RoundCornersLeft);
    ImGui::AddRectFilledGradient(drawList, ImVec2(max.x - pad - buttonWidth, rowY), ImVec2(max.x - pad, rowY + rowHeight), color(ImGuiCol_Button), CalcRaisedGradient(dark), style.FrameRounding);

    const f32 textY = max.y - em - pad * 0.6f;

    drawList->AddText(ImVec2(pos.x + pad, textY), color(ImGuiCol_Text), label);

    if (selected)
        drawList->AddText(ImVec2(max.x - pad - ImGui::CalcTextSize(ICON_LC_CHECK).x, textY), color(ImGuiCol_CheckMark), ICON_LC_CHECK);

    if (selected)
        drawList->AddRect(pos, max, ImGui::GetColorU32(ImGuiCol_CheckMark), rounding, 0, 2.0f);
    else
        drawList->AddRect(pos, max, ImGui::GetColorU32(held ? ImGuiCol_Text : hovered ? ImGuiCol_TextDisabled : ImGuiCol_Border), rounding);

    return pressed;
}

static void DrawThemeChoice(const char* label, f32 width, bool dark, bool* changed, bool* done)
{
    if (!DrawThemeCard(label, width, dark, IsDarkTheme() == dark) || IsDarkTheme() == dark)
        return;

    SetDarkTheme(dark);
    *changed = true;
    *done = true;
}

static void DrawThemeCards(bool* changed, bool* done)
{
    const f32 gap = ImFloor(ImGui::GetFontSize() * cCardGapEm);
    const f32 width = ImFloor((ImGui::GetContentRegionAvail().x - gap) * 0.5f);

    DrawThemeChoice(messages::mainmenu::cThemeDark, width, true, changed, done);

    ImGui::SameLine(0.0f, gap);
    DrawThemeChoice(messages::mainmenu::cThemeLight, width, false, changed, done);
}

static ImVec2 CalcKnobSize()
{
    return ImVec2(CalcKnobWidth(cKnobLabels, IM_ARRAYSIZE(cKnobLabels)), CalcRegionStripHeight());
}

static void NextKnob()
{
    ImGui::SameLine(0.0f, CalcKnobGap());
}

static void MarkKnobDone(bool changed, bool* done)
{
    if (ImGui::IsItemDeactivatedAfterEdit() || (changed && !ImGui::IsItemActive()))
        *done = true;
}

static bool DrawAmountKnob(const char* label, f32* value, f32 min, f32 max, f32 reset, bool* done)
{
    SetNextKnobReset(reset);

    const bool changed = KnobF32(label, value, min, max, cAmountFormat, cAmountKnobSpeed, CalcKnobSize());

    MarkKnobDone(changed, done);

    return changed;
}

static f32 ToHue(f32 ratio)
{
    return ratio * cDegreesMax / cDegreesPerTurn;
}

static ImVec4 GetHueRingColor(f32 ratio)
{
    return CalcAccentPreviewColor(ToHue(ratio), IsDarkTheme());
}

static bool DrawHueKnob(const char* label, f32* hue, f32 reset, bool* done)
{
    f32 degrees = *hue * cDegreesPerTurn;

    SetNextKnobReset(reset * cDegreesPerTurn);
    SetNextKnobTrack(&GetHueRingColor);
    SetNextKnobText(sead::FormatFixedSafeString<16>(messages::preferences::cHueDegreesFormat, degrees).cstr());

    const bool changed = KnobF32(label, &degrees, 0.0f, cDegreesMax, cDegreesFormat, cHueKnobSpeed, CalcKnobSize());

    if (changed)
        *hue = degrees / cDegreesPerTurn;

    MarkKnobDone(changed, done);

    return changed;
}

static void DrawColorKnobs(bool* changed, bool* done)
{
    const ThemeAdjust defaults;
    ThemeAdjust& adjust = GetThemeAdjust();

    f32 hue = GetAccentHue();

    if (DrawHueKnob(messages::preferences::cHueLabel, &hue, cThemeHueDefault, done))
    {
        SetAccentHue(hue);
        *changed = true;
    }

    NextKnob();
    *changed |= DrawAmountKnob(messages::preferences::cSaturationLabel, &adjust.saturation, cThemeSaturationMin, cThemeSaturationMax, defaults.saturation, done);

    NextKnob();
    *changed |= DrawAmountKnob(messages::preferences::cBrightnessLabel, &adjust.brightness, cThemeBrightnessMin, cThemeBrightnessMax, defaults.brightness, done);

    NextKnob();
    *changed |= DrawAmountKnob(messages::preferences::cContrastLabel, &adjust.contrast, cThemeContrastMin, cThemeContrastMax, defaults.contrast, done);
}

static void DrawShadowKnob()
{
    InterfaceSettings& settings = GetInterfaceSettings();

    f32 percent = ImFloor(settings.shadowOpacity * cPercentScale + 0.5f);
    bool released = false;

    SetNextKnobReset(cShadowOpacityDefault * cPercentScale);
    SetNextKnobText(percent > 0.0f ? nullptr : messages::common::cOff);

    const bool changed = KnobF32(messages::preferences::cShadowKnobLabel, &percent, cShadowOpacityMin * cPercentScale, cShadowOpacityMax * cPercentScale, messages::common::cPercentFloatFormat, cPercentKnobSpeed, CalcKnobSize());

    MarkKnobDone(changed, &released);

    if (changed)
    {
        settings.shadowOpacity = percent / cPercentScale;
        ApplyInterfaceConfig();
    }

    if (released)
        SaveInterfaceConfig();
}

static void DrawShadingKnobs(bool* changed, bool* done)
{
    const ThemeAdjust defaults;
    ThemeAdjust& adjust = GetThemeAdjust();

    *changed |= DrawAmountKnob(messages::preferences::cGradientLabel, &adjust.gradient, cThemeGradientMin, cThemeGradientMax, defaults.gradient, done);
    DetailTooltip(messages::preferences::cGradientDetail);

    NextKnob();
    DrawShadowKnob();
    DetailTooltip(messages::preferences::cShadowOpacityDetail);
}

static void ResetColors()
{
    const ThemeAdjust defaults;
    ThemeAdjust& adjust = GetThemeAdjust();

    gAccentColor = uicolors::cAccentDefault;
    adjust.saturation = defaults.saturation;
    adjust.brightness = defaults.brightness;
    adjust.contrast = defaults.contrast;
}

bool DrawAppearancePage()
{
    bool changed = false;
    bool done = false;

    DrawPageHeader(ICON_LC_PALETTE, messages::preferences::cPageAppearance, nullptr);

    if (BeginGroupBox(messages::common::cTheme))
    {
        DrawThemeCards(&changed, &done);
        EndGroupBox();
    }

    bool reset = false;

    if (BeginGroupBox(messages::preferences::cBoxColors, messages::preferences::cAppearanceReset, &reset))
    {
        DrawColorKnobs(&changed, &done);
        EndGroupBox();
    }

    if (BeginGroupBox(messages::preferences::cBoxShading))
    {
        DrawShadingKnobs(&changed, &done);
        EndGroupBox();
    }

    if (reset)
    {
        ResetColors();

        changed = true;
        done = true;
    }

    if (done)
        SaveThemeConfig();

    return changed;
}
