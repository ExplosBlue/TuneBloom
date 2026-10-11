#include <ui/bank/PianoRollPalette.h>

#include <ui/app/UI.h>

const PianoRollPalette& GetPianoRollPalette()
{
    static constexpr f32 cPianoScrollTrackMix   = 0.14f;
    static constexpr f32 cPianoScrollTrackAlpha = 0.67f;
    static constexpr f32 cPianoScrollThumbAlpha = 0.86f;
    static constexpr f32 cPianoScrollHoverMix   = 0.4f;
    static constexpr f32 cPianoScrollActiveMix  = 0.7f;
    static constexpr f32 cPianoScrollFitsMix    = 0.4f;

    static constexpr PianoRollPalette cDarkPalette = {
        IM_COL32(20, 20, 30, 255),           // canvasBg
        IM_COL32_WHITE,                      // placeholderText
        IM_COL32(255, 255, 255, 40),         // gridLineOctave
        IM_COL32(255, 255, 255, 28),         // gridLineVel
        IM_COL32(255, 255, 255, 120),        // gridLineVelTick
        IM_COL32(255, 255, 0, 255),          // grabBar
        IM_COL32(0x2a, 0x33, 0x40, 170),     // vScrollTrack
        IM_COL32(0x7f, 0xd1, 0xff, 220),     // vScrollThumb
        IM_COL32(0xbf, 0xe6, 0xff, 255),     // vScrollThumbHover
        IM_COL32(0x2a, 0x33, 0x40, 255),     // hScrollTrack
        IM_COL32(0x3d, 0x5a, 0x73, 160),     // hScrollThumbFits
        IM_COL32(0x7f, 0xd1, 0xff, 255),     // hScrollThumbOverflow
        IM_COL32(0xea, 0xf6, 0xff, 255),     // hScrollThumbActive
        IM_COL32(0x7f, 0xd1, 0xff, 220),     // splitterHover
    };
    static constexpr PianoRollPalette cLightPalette = {
        IM_COL32(0xf2, 0xf3, 0xf5, 255),     // canvasBg
        IM_COL32(0x40, 0x46, 0x4d, 255),     // placeholderText
        IM_COL32(0, 0, 0, 35),               // gridLineOctave
        IM_COL32(0, 0, 0, 25),               // gridLineVel
        IM_COL32(0, 0, 0, 110),              // gridLineVelTick
        IM_COL32(200, 120, 0, 255),          // grabBar
        IM_COL32(0xd4, 0xd8, 0xde, 170),     // vScrollTrack
        IM_COL32(0x1f, 0x6f, 0xa8, 220),     // vScrollThumb
        IM_COL32(0x3a, 0x8e, 0xcf, 255),     // vScrollThumbHover
        IM_COL32(0xd4, 0xd8, 0xde, 255),     // hScrollTrack
        IM_COL32(0x8a, 0x9b, 0xac, 160),     // hScrollThumbFits
        IM_COL32(0x1f, 0x6f, 0xa8, 255),     // hScrollThumbOverflow
        IM_COL32(0x62, 0xab, 0xe2, 255),     // hScrollThumbActive
        IM_COL32(0x1f, 0x6f, 0xa8, 220),     // splitterHover
    };
    static PianoRollPalette sPalette;
    static s32 sPaletteFrame = -1;

    if (sPaletteFrame == ImGui::GetFrameCount())
        return sPalette;

    sPaletteFrame = ImGui::GetFrameCount();
    sPalette = gThemeIsDark ? cDarkPalette : cLightPalette;

    // Theme colors
    ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_ChildBg);
    ImVec4 ink = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    ImVec4 lit = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
    bg.w = ink.w = lit.w = 1.0f;

    const ImVec4 track = ImLerp(bg, ink, cPianoScrollTrackMix);

    sPalette.canvasBg             = ImGui::ColorConvertFloat4ToU32(bg);
    sPalette.vScrollTrack         = ImGui::ColorConvertFloat4ToU32(ImVec4(track.x, track.y, track.z, cPianoScrollTrackAlpha));
    sPalette.vScrollThumb         = ImGui::ColorConvertFloat4ToU32(ImVec4(lit.x, lit.y, lit.z, cPianoScrollThumbAlpha));
    sPalette.vScrollThumbHover    = ImGui::ColorConvertFloat4ToU32(ImLerp(lit, ink, cPianoScrollHoverMix));
    sPalette.hScrollTrack         = ImGui::ColorConvertFloat4ToU32(track);
    sPalette.hScrollThumbFits     = ImGui::ColorConvertFloat4ToU32(ImLerp(bg, lit, cPianoScrollFitsMix));
    sPalette.hScrollThumbOverflow = ImGui::ColorConvertFloat4ToU32(lit);
    sPalette.hScrollThumbActive   = ImGui::ColorConvertFloat4ToU32(ImLerp(lit, ink, cPianoScrollActiveMix));
    sPalette.splitterHover        = sPalette.vScrollThumb;

    return sPalette;
}
