#include <ui/bank/PianoRollPalette.h>

#include <ui/app/UI.h>
#include <ui/settings/UiColors.h>

const PianoRollPalette& GetPianoRollPalette()
{
    static constexpr f32 cPianoScrollTrackMix   = 0.14f;
    static constexpr f32 cPianoScrollTrackAlpha = 0.67f;
    static constexpr f32 cPianoScrollThumbAlpha = 0.86f;
    static constexpr f32 cPianoScrollHoverMix   = 0.4f;
    static constexpr f32 cPianoScrollActiveMix  = 0.7f;

    static constexpr uicolors::Themed<f32> cPianoScrollFitsMix     = { 0.4f, 0.22f };
    static constexpr uicolors::Themed<f32> cPianoScrollOverflowMix = { 1.0f, 0.45f };

    static PianoRollPalette sPalette;
    static s32 sPaletteFrame = -1;

    if (sPaletteFrame == ImGui::GetFrameCount())
        return sPalette;

    sPaletteFrame = ImGui::GetFrameCount();

    ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_ChildBg);
    ImVec4 ink = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    ImVec4 lit = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
    bg.w = ink.w = lit.w = 1.0f;

    const ImVec4 track = ImLerp(bg, ink, cPianoScrollTrackMix);

    sPalette.placeholderText      = uicolors::Pick(uicolors::pianoroll::cPlaceholderText);
    sPalette.gridLineOctave       = uicolors::Pick(uicolors::pianoroll::cGridOctave);
    sPalette.gridLineVel          = uicolors::Pick(uicolors::pianoroll::cGridVelocity);
    sPalette.gridLineVelTick      = uicolors::Pick(uicolors::pianoroll::cGridVelocityTick);
    sPalette.canvasBg             = ImGui::ColorConvertFloat4ToU32(bg);
    sPalette.vScrollTrack         = ImGui::ColorConvertFloat4ToU32(ImVec4(track.x, track.y, track.z, cPianoScrollTrackAlpha));
    sPalette.vScrollThumb         = ImGui::ColorConvertFloat4ToU32(ImVec4(lit.x, lit.y, lit.z, cPianoScrollThumbAlpha));
    sPalette.vScrollThumbHover    = ImGui::ColorConvertFloat4ToU32(ImLerp(lit, ink, cPianoScrollHoverMix));
    sPalette.hScrollTrack         = ImGui::ColorConvertFloat4ToU32(track);
    sPalette.hScrollThumbFits     = ImGui::ColorConvertFloat4ToU32(ImLerp(bg, lit, uicolors::Pick(cPianoScrollFitsMix)));
    sPalette.hScrollThumbOverflow = ImGui::ColorConvertFloat4ToU32(ImLerp(bg, lit, uicolors::Pick(cPianoScrollOverflowMix)));
    sPalette.hScrollThumbActive   = ImGui::ColorConvertFloat4ToU32(ImLerp(lit, ink, cPianoScrollActiveMix));
    sPalette.splitterHover        = sPalette.vScrollThumb;

    return sPalette;
}
