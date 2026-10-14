#include <ui/bank/KeyboardPalette.h>

#include <ui/settings/Theme.h>
#include <ui/settings/UiColors.h>

#include <imgui/imgui_internal.h>

static ImU32 OpaqueU32(const ImVec4& color)
{
    return uicolors::ToU32(uicolors::Opaque(color));
}

const KeyboardPalette& GetKeyboardPalette()
{
    static KeyboardPalette sPalette;
    static s32 sPaletteFrame = -1;

    if (sPaletteFrame == ImGui::GetFrameCount())
        return sPalette;

    sPaletteFrame = ImGui::GetFrameCount();

    const ImVec4 accent = GetThemeAccent();
    const ImVec4 playback = uicolors::PlaybackFill();
    const ImVec4 surface = uicolors::Opaque(ImGui::GetStyleColorVec4(uicolors::Pick(uicolors::keyboard::cWhiteKeySurface)));
    const ImVec4 edge = uicolors::Opaque(ImGui::GetStyleColorVec4(uicolors::Pick(uicolors::keyboard::cBlackKeyEdge)));
    const ImVec4 control = uicolors::Opaque(ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
    const ImVec4 ink = uicolors::Opaque(ImGui::GetStyleColorVec4(ImGuiCol_Text));

    const ImVec4 white = ImLerp(uicolors::cInkOnDark, surface, uicolors::keyboard::cWhiteKeyInkMix);
    const ImVec4 black = ImLerp(edge, control, uicolors::Pick(uicolors::keyboard::cBlackKeyLift));
    const ImVec4 blackActive = uicolors::Darken(playback, uicolors::keyboard::cBlackKeyActiveDarken);

    sPalette.whiteKey           = OpaqueU32(white);
    sPalette.whiteKeyHover      = OpaqueU32(ImLerp(white, accent, uicolors::keyboard::cWhiteKeyHoverMix));
    sPalette.whiteKeyActive     = OpaqueU32(playback);
    sPalette.whiteKeyRoot       = OpaqueU32(accent);
    sPalette.whiteKeyRootActive = OpaqueU32(playback);
    sPalette.blackKey           = OpaqueU32(black);
    sPalette.blackKeyHover      = OpaqueU32(ImLerp(black, accent, uicolors::keyboard::cBlackKeyHoverMix));
    sPalette.blackKeyActive     = OpaqueU32(blackActive);
    sPalette.blackKeyRoot       = OpaqueU32(accent);
    sPalette.blackKeyRootActive = OpaqueU32(blackActive);
    sPalette.blackKeyEdge       = OpaqueU32(edge);
    sPalette.keyShadow          = uicolors::ToU32(uicolors::cInkOnLight, uicolors::UnitToByte(uicolors::keyboard::cKeyShadowAlpha));
    sPalette.separator          = OpaqueU32(ImLerp(white, ink, uicolors::keyboard::cKeySeparatorMix));
    sPalette.rootStripe         = OpaqueU32(accent);

    return sPalette;
}
