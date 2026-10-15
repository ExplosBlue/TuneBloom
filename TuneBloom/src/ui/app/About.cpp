#include <ui/app/About.h>

#include <ui/widgets/Dialogs.h>
#include <ui/items/ItemColumns.h>
#include <ui/Messages.h>
#include <ui/app/Shortcuts.h>
#include <ui/file/SystemShell.h>
#include <ui/settings/Theme.h>
#include <ui/settings/UiColors.h>
#include <ui/widgets/WindowAnimation.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_internal.h>

#include <gfx/gl/seadTextureGL.h>

#include <app/Utilll.h>

static bool sWantsAbout = false;

void RequestAbout()
{
    sWantsAbout = true;
}

static constexpr f32 cAboutWidthEm     = 23.0f;
static constexpr f32 cAboutPadEm       = 1.2f;
static constexpr f32 cAboutRoundingEm  = 0.6f;
static constexpr f32 cAboutGlowEm      = 11.0f;
static constexpr f32 cAboutFooterGapEm = 0.8f;
static constexpr f32 cAboutChipPadXEm  = 0.7f;
static constexpr f32 cAboutChipPadYEm  = 0.15f;
static constexpr f32 cAboutLogoSize    = 130.0f;

static constexpr uicolors::Themed<f32> cAboutGlowMix = { 0.30f, 0.12f };

static void AboutGap(f32 heightEm)
{
    ImGui::Dummy(ImVec2(0.0f, ImFloor(ImGui::GetFontSize() * heightEm)));
}

static void AboutCenter(f32 width)
{
    ImGui::SetCursorPosX(ImFloor((ImGui::GetWindowSize().x - width) * 0.5f));
}

static void DrawAboutText(const char* text, const ImVec4& color)
{
    AboutCenter(ImGui::CalcTextSize(text).x);
    ImGui::TextColored(color, "%s", text);
}

static void DrawAboutChip(const char* text)
{
    const f32 em = ImGui::GetFontSize();
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    const ImVec2 size(textSize.x + ImFloor(em * cAboutChipPadXEm) * 2.0f, textSize.y + ImFloor(em * cAboutChipPadYEm) * 2.0f);

    AboutCenter(size.x);

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    drawList->AddRectFilled(pos, pos + size, ImGui::GetColorU32(ImGuiCol_FrameBg), size.y * 0.5f);
    drawList->AddText(ImFloor(pos + (size - textSize) * 0.5f), ImGui::GetColorU32(GetItemValueColor()), text);

    ImGui::Dummy(size);
}

static void DrawAboutLink(const char* text, const char* url)
{
    const ImVec4 color = GetAccentTextColor();

    ImGui::TextColored(color, "%s", text);

    if (ImGui::IsItemHovered())
    {
        const ImVec2 max = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(ImGui::GetItemRectMin().x, max.y), max, ImGui::GetColorU32(color));

        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            OpenURL(url);
    }

    ImGui::SetItemTooltip("%s", url);
}

static void DrawAboutButton(const char* icon, const char* label, const char* url)
{
    const sead::FormatFixedSafeString<64> text("%s %s", icon, label);
    const f32 width = ImFloor(ImGui::CalcTextSize(text.cstr()).x + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetFontSize());

    if (ImGui::Button(text.cstr(), ImVec2(width, 0.0f)))
        OpenURL(url);

    ImGui::SetItemTooltip("%s", url);
}

void DrawAppIcon(float size)
{
    sead::TextureGL* tex = util::getIcon() ? sead::DynamicCast<sead::TextureGL>(util::getIcon()) : nullptr;

    if (!tex)
    {
        ImGui::Dummy(ImVec2(size, size));
        return;
    }

    ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(tex->getID())), ImVec2(size, size));
}

const char* GetAppVersionFormat()
{
    return util::cIsCommitBuild ? messages::about::cBuildFormat : messages::about::cVersionFormat;
}

static void DrawTuneBloomSplash()
{
    ImGuiIO& io = ImGui::GetIO();
    const ImGuiStyle& style = ImGui::GetStyle();

    const f32 em = ImGui::GetFontSize();
    const f32 pad = ImFloor(em * cAboutPadEm);
    const f32 width = ImFloor(em * cAboutWidthEm);
    const f32 rounding = ImFloor(em * cAboutRoundingEm);

    const ImVec4 background = uicolors::Opaque(ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, FLT_MAX));

    ImGui::PushStyleColor(ImGuiCol_PopupBg, background);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
    const bool visible = ImGui::BeginPopupModal("##TuneBloomSplash",
        nullptr,
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_AlwaysAutoResize
    );
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    if (!visible)
        return;

    AnimateWindow(WindowAnimation::Dialog);

    // Glow
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 winPos = ImGui::GetWindowPos();
    const ImVec2 winSize = ImGui::GetWindowSize();
    const f32 border = style.WindowBorderSize;
    const ImVec2 glowMin(winPos.x + border, winPos.y + border);
    const ImVec2 glowMax(winPos.x + winSize.x - border, winPos.y + ImFloor(em * cAboutGlowEm));
    const ImVec4 glow = ImLerp(background, GetThemeAccent(), uicolors::Pick(cAboutGlowMix));

    drawList->PushClipRect(winPos, winPos + winSize, false);
    const s32 vtxBegin = drawList->VtxBuffer.Size;
    drawList->AddRectFilled(glowMin, glowMax, uicolors::cInkOnDarkU32, ImMax(rounding - border, 0.0f), ImDrawFlags_RoundCornersTop);
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(drawList, vtxBegin, drawList->VtxBuffer.Size, glowMin, ImVec2(glowMin.x, glowMax.y), ImGui::GetColorU32(glow), ImGui::GetColorU32(background));
    drawList->PopClipRect();

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, 0.0f));

    // Logo
    AboutGap(0.4f);
    AboutCenter(cAboutLogoSize);
    DrawAppIcon(cAboutLogoSize);
    AboutGap(0.5f);

    // Name
    ImGui::PushFont(io.Fonts->Fonts[0]);
    DrawAboutText(util::cAppName.cstr(), ImGui::GetStyleColorVec4(ImGuiCol_Text));
    ImGui::PopFont();
    AboutGap(0.4f);

    // Version
    DrawAboutChip(sead::FormatFixedSafeString<64>(GetAppVersionFormat(), util::cAppVersion.cstr()).cstr());
    AboutGap(1.2f);

    DrawAboutText(messages::about::cTagline, ImGui::GetStyleColorVec4(ImGuiCol_Text));
    AboutGap(0.9f);

    // Credit
    const f32 space = ImGui::CalcTextSize(" ").x;
    const f32 creditWidth = ImGui::CalcTextSize(messages::about::cBy).x + ImGui::CalcTextSize(messages::about::cAuthor).x + ImGui::CalcTextSize(messages::about::cAndMore).x + space * 2.0f;

    AboutCenter(creditWidth);
    ImGui::TextColored(GetItemValueColor(), "%s", messages::about::cBy);
    ImGui::SameLine(0.0f, space);
    DrawAboutLink(messages::about::cAuthor, cAuthorUrl);
    ImGui::SameLine(0.0f, space);
    ImGui::TextColored(GetItemValueColor(), "%s", messages::about::cAndMore);
    AboutGap(0.25f);

    DrawAboutText(messages::about::cMadeInLine, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    AboutGap(1.2f);

    ImGui::PopStyleVar();

    // Footer
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, ImMax(ImFloor(em * cAboutFooterGapEm) - style.ItemSpacing.y * 2.0f, 0.0f)));

    DrawAboutButton(ICON_LC_FOLDER_GIT_2, messages::about::cGitHub, cRepositoryUrl);
    ImGui::SameLine();
    DrawAboutButton(ICON_LC_MESSAGE_CIRCLE, messages::about::cDiscord, cDiscordUrl);
    ImGui::SameLine();

    BeginDialogFooter({ messages::common::cClose }, false);

    if (DialogButton(messages::common::cClose) || shortcuts::Pressed(shortcuts::Action::CloseDialog))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

void DrawAboutPopup()
{
    if (sWantsAbout)
    {
        ImGui::OpenPopup("##TuneBloomSplash");
        sWantsAbout = false;
    }

    if (ImGui::IsPopupOpen("##TuneBloomSplash"))
        DrawTuneBloomSplash();
}
