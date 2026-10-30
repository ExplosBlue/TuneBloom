#include <ui/widgets/Widgets.h>

#include <ui/items/ItemColumns.h>
#include <ui/widgets/Knob.h>
#include <messages/Messages.h>
#include <ui/widgets/Properties.h>
#include <ui/settings/Config.h>
#include <ui/settings/Theme.h>
#include <ui/settings/UiColors.h>
#include <ui/file/UnsavedChanges.h>
#include <ui/widgets/WindowAnimation.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_custom.h>
#include <imgui/imgui_internal.h>

#include <snd/snd_Util.h>

#include <cstdlib>
#include <cstring>

void NormalizePathSlashes(sead::BufferedSafeString* path)
{
    for (s32 i = 0; i < path->calcLength(); i++)
        if (path->getBuffer()[i] == '\\')
            path->getBuffer()[i] = '/';
}

static constexpr f32 cRowButtonEm              = 1.15f;
static constexpr f32 cRowButtonIconEm          = 0.8f;
static constexpr f32 cBrightButtonMix          = 0.15f;
static constexpr f32 cIconButtonBorderSize     = 1.0f;
static constexpr f32 cIconButtonBorderHoverMix = 0.8f;
static constexpr f32 cVersionFieldEm           = 2.0f;
static constexpr f32 cVersionDotGap            = 6.0f;
static constexpr f32 cDetailTooltipWrapEm      = 32.0f;
static constexpr f32 cTextButtonPadEm          = 1.0f;
static constexpr f32 cFitComboExtraEm          = 0.5f;
static constexpr f32 cInsertLineAlpha          = 0.86f;
static constexpr f32 cInsertLineHeight         = 3.0f;

static constexpr uicolors::Themed<f32> cIconButtonBorderIdleMix   = { 0.25f, 0.20f };
static constexpr uicolors::Themed<f32> cIconButtonBorderIdleAlpha = { 0.40f, 0.55f };
static constexpr uicolors::Themed<f32> cIconButtonTintAlpha       = { 0.10f, 0.10f };
static constexpr uicolors::Themed<f32> cIconButtonTintHoverAlpha  = { 0.20f, 0.17f };
static constexpr uicolors::Themed<f32> cIconButtonIconTextMix     = { 0.65f, 0.45f };

static constexpr f32 cGlyphFrameRatio  = 0.36f;
static constexpr f32 cGlyphCorner      = 0.07f;
static constexpr f32 cPlayHalfWidth    = 0.43f;
static constexpr f32 cPlayOpticalShift = 0.05f;
static constexpr f32 cPlayInsetScale   = 1.5f;
static constexpr f32 cPlayStrokeScale  = 2.0f;
static constexpr f32 cPauseBarWidth    = 0.27f;
static constexpr f32 cPauseBarGap      = 0.22f;
static constexpr f32 cPauseHeight      = 0.90f;
static constexpr f32 cPauseRounding    = 0.30f;
static constexpr f32 cStopSize         = 0.86f;
static constexpr f32 cStopRounding     = 0.15f;

namespace
{

struct IconButtonFrame
{
    bool pressed;
    ImVec2 min;
    ImVec2 max;
    ImU32 iconColor;
};

}

void PushBrightButtonColors(const ImVec4& color)
{
    ImGui::PushStyleColor(ImGuiCol_Button, color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, uicolors::Lighten(color, cBrightButtonMix));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, uicolors::Darken(color, cBrightButtonMix));
    ImGui::PushStyleColor(ImGuiCol_Text, uicolors::cOnBright);
}

void PopBrightButtonColors()
{
    ImGui::PopStyleColor(4);
}

static void DrawPlayGlyph(ImDrawList* draw, const ImVec2& center, f32 extent, ImU32 color)
{
    const f32 radius = extent * cGlyphCorner;
    const f32 half = extent * 0.5f;
    const f32 halfWidth = extent * cPlayHalfWidth;
    const f32 shift = extent * cPlayOpticalShift;

    const ImVec2 points[3] =
    {
        ImVec2(center.x - halfWidth + shift + radius, center.y - half + radius * cPlayInsetScale),
        ImVec2(center.x - halfWidth + shift + radius, center.y + half - radius * cPlayInsetScale),
        ImVec2(center.x + halfWidth + shift - radius, center.y),
    };

    draw->AddConvexPolyFilled(points, 3, color);
    draw->AddPolyline(points, 3, color, ImDrawFlags_Closed, radius * cPlayStrokeScale);
}

static void DrawPauseGlyph(ImDrawList* draw, const ImVec2& center, f32 extent, ImU32 color)
{
    const f32 barWidth = extent * cPauseBarWidth;
    const f32 gap = extent * cPauseBarGap;
    const f32 halfHeight = extent * cPauseHeight * 0.5f;
    const f32 rounding = barWidth * cPauseRounding;

    const f32 leftMin = center.x - gap * 0.5f - barWidth;
    const f32 rightMin = center.x + gap * 0.5f;

    draw->AddRectFilled(ImVec2(leftMin, center.y - halfHeight), ImVec2(leftMin + barWidth, center.y + halfHeight), color, rounding);
    draw->AddRectFilled(ImVec2(rightMin, center.y - halfHeight), ImVec2(rightMin + barWidth, center.y + halfHeight), color, rounding);
}

static void DrawStopGlyph(ImDrawList* draw, const ImVec2& center, f32 extent, ImU32 color)
{
    const f32 half = extent * cStopSize * 0.5f;

    draw->AddRectFilled(ImVec2(center.x - half, center.y - half), ImVec2(center.x + half, center.y + half), color, extent * cStopRounding);
}

static void DrawTransportGlyph(ImDrawList* draw, TransportGlyph glyph, const ImVec2& center, f32 extent, ImU32 color)
{
    switch (glyph)
    {
    case TransportGlyph::Play:
        DrawPlayGlyph(draw, center, extent, color);
        break;

    case TransportGlyph::Pause:
        DrawPauseGlyph(draw, center, extent, color);
        break;

    case TransportGlyph::Stop:
        DrawStopGlyph(draw, center, extent, color);
        break;
    }
}

bool TransportButton(TransportGlyph glyph, const char* id, const ImVec2& size)
{
    const ImGuiStyle& style = ImGui::GetStyle();

    ImVec2 buttonSize = size;

    if (buttonSize.x <= 0.0f)
        buttonSize.x = ImGui::GetFontSize() + style.FramePadding.x * 2.0f;

    if (buttonSize.y <= 0.0f)
        buttonSize.y = ImGui::GetFrameHeight();

    const bool pressed = ImGui::Button(id, buttonSize);

    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const ImVec2 center(ImFloor((min.x + max.x) * 0.5f), ImFloor((min.y + max.y) * 0.5f));

    DrawTransportGlyph(ImGui::GetWindowDrawList(), glyph, center, ImMin(buttonSize.x, buttonSize.y) * cGlyphFrameRatio, ImGui::GetColorU32(ImGuiCol_Text));

    return pressed;
}

static ImFont* GetIconFont(f32 size)
{
    for (ImFont* font : ImGui::GetIO().Fonts->Fonts)
    {
        if (font->FontSize == size && font->FindGlyphNoFallback(ICON_MIN_LC))
            return font;
    }

    return ImGui::GetFont();
}

ImFont* FindFontOfSize(f32 size)
{
    for (ImFont* font : ImGui::GetIO().Fonts->Fonts)
    {
        if (font->FontSize == size)
            return font;
    }

    return ImGui::GetFont();
}

ImFont* GetSmallFont()
{
    return FindFontOfSize(ScaleSize(cSmallFontSize));
}

f32 CalcCapCenterOffset(const ImFont* font, f32 size)
{
    const ImFontGlyph* capital = font->FindGlyph('H');

    return capital ? (capital->Y0 + capital->Y1) * 0.5f * size / font->FontSize : size * 0.5f;
}

static ImVec4 CalcIconButtonFill(bool lit, bool isPlayTint, bool held, f32 hoverRatio)
{
    if (!lit)
        return held ? ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive) : ImLerp(ImGui::GetStyleColorVec4(ImGuiCol_Button), ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered), hoverRatio);

    const ImVec4 color = isPlayTint ? uicolors::PlaybackFill() : GetThemeAccent();

    if (held)
        return uicolors::Darken(color, cBrightButtonMix);

    return uicolors::Lighten(color, cBrightButtonMix * hoverRatio);
}

static void DrawIconButtonAccentTint(const ImVec2& min, const ImVec2& max, bool held, f32 hoverRatio)
{
    const f32 idleAlpha = uicolors::Pick(cIconButtonTintAlpha);
    const f32 hoverAlpha = uicolors::Pick(cIconButtonTintHoverAlpha);
    const f32 alpha = held ? hoverAlpha : ImLerp(idleAlpha, hoverAlpha, hoverRatio);

    ImVec4 tint = GetThemeAccent();
    tint.w = alpha;

    ImGui::GetWindowDrawList()->AddRectFilled(min, max, ImGui::GetColorU32(tint), ImGui::GetStyle().FrameRounding);
}

static void DrawIconButtonBorder(const ImVec2& min, const ImVec2& max, bool held, f32 hoverRatio)
{
    const f32 emphasis = held ? 1.0f : hoverRatio;
    const f32 accentMix = ImLerp(uicolors::Pick(cIconButtonBorderIdleMix), cIconButtonBorderHoverMix, emphasis);
    const f32 alpha = ImLerp(uicolors::Pick(cIconButtonBorderIdleAlpha), 1.0f, emphasis);

    ImVec4 color = ImLerp(ImGui::GetStyleColorVec4(ImGuiCol_Border), GetThemeAccent(), accentMix);
    color.w *= alpha;

    ImGui::GetWindowDrawList()->AddRect(min, max, ImGui::GetColorU32(color), ImGui::GetStyle().FrameRounding, 0, cIconButtonBorderSize);
}

static ImU32 CalcIconButtonIconColor(bool lit, bool isPlayTint, bool held, f32 hoverRatio)
{
    if (isPlayTint)
        return ImGui::GetColorU32(uicolors::cOnBright);

    if (lit)
        return ImGui::GetColorU32(GetOnAccentTextColor());

    const f32 textMix = ImLerp(uicolors::Pick(cIconButtonIconTextMix), 1.0f, held ? 1.0f : hoverRatio);

    return ImGui::GetColorU32(ImLerp(GetThemeAccent(), ImGui::GetStyleColorVec4(ImGuiCol_Text), textMix));
}

static IconButtonFrame DrawIconButtonFrame(const char* id, const ImVec2& itemSize, f32 frameSize, bool lit, IconButtonTint tint, ImGuiButtonFlags flags)
{
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 min(ImFloor(pos.x + (itemSize.x - frameSize) * 0.5f), ImFloor(pos.y + (itemSize.y - frameSize) * 0.5f));
    const ImVec2 max(min.x + frameSize, min.y + frameSize);

    const bool pressed = ImGui::InvisibleButton(id, itemSize, flags);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();

    if (hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    const f32 hoverRatio = ImGui::GetHoverRatio(ImGui::GetItemID() + 1, hovered || held);
    const bool isPlayTint = lit && tint == IconButtonTint::Play;
    const ImVec4 fill = CalcIconButtonFill(lit, isPlayTint, held, hoverRatio);

    ImGui::AddRectFilledRaised(ImGui::GetWindowDrawList(), min, max, ImGui::GetColorU32(fill), ImGui::GetStyle().FrameRounding);

    if (!lit)
    {
        DrawIconButtonAccentTint(min, max, held, hoverRatio);
        DrawIconButtonBorder(min, max, held, hoverRatio);
    }

    return { pressed, min, max, CalcIconButtonIconColor(lit, isPlayTint, held, hoverRatio) };
}

bool DrawIconButton(const char* id, const char* icon, const ImVec2& itemSize, f32 frameSize, f32 iconSize, bool lit, IconButtonTint tint, ImGuiButtonFlags flags)
{
    const IconButtonFrame frame = DrawIconButtonFrame(id, itemSize, frameSize, lit, tint, flags);

    ImFont* font = GetIconFont(iconSize);
    const ImVec2 textSize = font->CalcTextSizeA(iconSize, FLT_MAX, 0.0f, icon);

    ImGui::GetWindowDrawList()->AddText(font, iconSize, ImVec2(ImFloor(frame.min.x + (frameSize - textSize.x) * 0.5f), ImFloor(frame.min.y + (frameSize - textSize.y) * 0.5f)), frame.iconColor, icon);

    return frame.pressed;
}

f32 CalcFitComboWidth(const char* const* items, s32 itemNum)
{
    f32 textWidth = 0.0f;

    for (s32 i = 0; i < itemNum; i++)
        textWidth = ImMax(textWidth, ImGui::CalcTextSize(items[i]).x);

    return ImFloor(textWidth + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetFontSize() * cFitComboExtraEm);
}

f32 CalcTextButtonWidth(const char* label)
{
    return ImFloor(ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetFontSize() * cTextButtonPadEm);
}

bool TextButton(const char* label)
{
    return ImGui::Button(label, ImVec2(CalcTextButtonWidth(label), 0.0f));
}

f32 CalcTextActionWidth(const char* label)
{
    return ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
}

bool DrawTextAction(const char* id, const char* label, f32 height, f32 textOffsetY, bool* outHovered)
{
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    const f32 width = CalcTextActionWidth(label);

    const bool isPressed = ImGui::InvisibleButton(id, ImVec2(width, height));
    const bool isHovered = ImGui::IsItemHovered();

    const ImU32 color = ImGui::GetHoverColorU32(ImGui::GetItemID(), isHovered, ImGui::GetColorU32(ImGuiCol_CheckMark), ImGui::GetColorU32(ImGuiCol_Text));
    const ImVec2 min = ImGui::GetItemRectMin();

    ImGui::GetWindowDrawList()->AddText(ImVec2(min.x + ImFloor((width - textSize.x) * 0.5f), min.y + textOffsetY), color, label);

    if (outHovered)
        *outHovered = isHovered;

    return isPressed;
}

bool DrawGlyphButton(const char* id, TransportGlyph glyph, const ImVec2& itemSize, f32 frameSize, bool lit, IconButtonTint tint, ImGuiButtonFlags flags)
{
    const IconButtonFrame frame = DrawIconButtonFrame(id, itemSize, frameSize, lit, tint, flags);
    const ImVec2 center(ImFloor((frame.min.x + frame.max.x) * 0.5f), ImFloor((frame.min.y + frame.max.y) * 0.5f));

    DrawTransportGlyph(ImGui::GetWindowDrawList(), glyph, center, frameSize * cGlyphFrameRatio, frame.iconColor);

    return frame.pressed;
}

bool PlayToggleButton(TransportGlyph glyph, const char* id, const ImVec2& size, bool playing)
{
    if (playing)
        return TransportButton(glyph, id, size);

    PushBrightButtonColors(uicolors::PlaybackFill());

    const bool pressed = TransportButton(glyph, id, size);

    PopBrightButtonColors();

    return pressed;
}

bool ToggleButton(const char* label, bool on, const ImVec2& size)
{
    if (on)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, GetThemeAccent());
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, GetThemeAccent());
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, GetThemeAccent());
        ImGui::PushStyleColor(ImGuiCol_Text, GetOnAccentTextColor());
    }

    const bool pressed = ImGui::Button(label, size);

    if (on)
        ImGui::PopStyleColor(4);

    return pressed;
}

f32 GetRowButtonSize()
{
    return ImFloor(ImGui::GetFontSize() * cRowButtonEm);
}

static bool DrawRowButton(u32 id, const char* icon, bool lit)
{
    return DrawIconButton(sead::FormatFixedSafeString<32>("###%u", id).cstr(), icon, GetItemActionCellSize(), GetRowButtonSize(), ImFloor(ImGui::GetFontSize() * cRowButtonIconEm), lit);
}

bool DrawRowPlayButton(u32 id, const char* playbackError, bool playing)
{
    ImGui::BeginDisabled(playbackError != nullptr);

    const bool pressed = DrawGlyphButton(sead::FormatFixedSafeString<32>("###%u", id).cstr(), TransportGlyph::Play, GetItemActionCellSize(), GetRowButtonSize(), playing, IconButtonTint::Play);

    ImGui::EndDisabled();

    if (playbackError)
        ImGui::SetItemTooltip("%s", playbackError);

    return pressed;
}

void DrawInsertLine(ImDrawList* drawList, f32 left, f32 right, f32 centerY)
{
    const f32 top = ImFloor(centerY - cInsertLineHeight * 0.5f);

    drawList->AddRectFilled(ImVec2(left, top), ImVec2(right, top + cInsertLineHeight), ImGui::GetColorU32(ImGuiCol_CheckMark, cInsertLineAlpha));
}

bool DrawTableRowSelectable(const char* id, bool selected, bool* outHovered)
{
    const ImU32 rowColor        = ImGui::GetColorU32(ImGuiCol_Header);
    const ImU32 rowHoveredColor = ImGui::GetColorU32(ImGuiCol_HeaderHovered);
    const ImU32 rowActiveColor  = ImGui::GetColorU32(ImGuiCol_HeaderActive);

    ImGui::PushStyleColor(ImGuiCol_Header,        uicolors::cTransparentU32);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, uicolors::cTransparentU32);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  uicolors::cTransparentU32);

    const bool rowPressed = ImGui::Selectable(id, selected, ImGuiSelectableFlags_SpanAllColumns);

    ImGui::PopStyleColor(3);

    const ImGuiID hoveredId = ImGui::GetHoveredID();
    const bool rowHovered = ImGui::IsItemHovered() && (hoveredId == 0 || hoveredId == ImGui::GetItemID());

    const ImGuiID rowFadeId = ImGui::GetItemID() + 1;
    const f32 rowHoverRatio = ImGui::GetHoverRatio(rowFadeId, rowHovered);

    if (rowHoverRatio > 0.0f || selected)
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, rowHovered && ImGui::IsItemActive() ? rowActiveColor : selected ? rowColor : ImGui::GetHoverColorU32(rowFadeId, rowHovered, 0, rowHoveredColor));

    *outHovered = rowHovered;

    return rowPressed;
}

bool IsItemEditCommitted(bool changed)
{
    return ImGui::IsItemDeactivatedAfterEdit() || (changed && !ImGui::IsItemActive());
}

bool DrawRowHoverButton(const char* id, const char* icon, bool visible, const char* tooltip, const ImVec2& hitSize, f32 size)
{
    const ImVec2 hitPos = ImGui::GetCursorScreenPos();
    const ImVec2 pos = hitPos + ImFloor((hitSize - ImVec2(size, size)) * 0.5f);

    const bool pressed = ImGui::InvisibleButton(id, hitSize);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();

    ImGui::SetItemTooltip("%s", tooltip);

    if (!visible && !hovered && !held)
        return pressed;

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const f32 hoverRatio = ImGui::GetHoverRatio(ImGui::GetItemID() + 1, hovered || held);

    if (hoverRatio > 0.0f)
        drawList->AddRectFilled(pos, pos + ImVec2(size, size), ImGui::GetColorU32(held ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered, hoverRatio), ImGui::GetStyle().FrameRounding);

    const ImVec2 iconSize = ImGui::CalcTextSize(icon);
    const ImGuiCol color = hovered || held ? ImGuiCol_Text : ImGuiCol_TextDisabled;

    drawList->AddText(pos + ImFloor((ImVec2(size, size) - iconSize) * 0.5f), ImGui::GetColorU32(color), icon);

    return pressed;
}

void DrawSpinner(const ImVec2& center, f32 radius, ImU32 color)
{
    constexpr f32 cTurnsPerSecond = 1.2f;
    constexpr f32 cArcLength      = IM_PI * 1.4f;
    constexpr s32 cSegments       = 16;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const f32 start = static_cast<f32>(ImGui::GetTime()) * cTurnsPerSecond * IM_PI * 2.0f;
    const f32 thickness = ImGui::GetIconStrokeThickness(ImGui::GetFontSize());

    drawList->PathArcTo(center, radius, start, start + cArcLength, cSegments);
    drawList->PathStroke(color, ImDrawFlags_None, thickness);
}

void DrawProgressBar(const ImVec2& min, const ImVec2& max, f32 fraction)
{
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const f32 rounding = (max.y - min.y) * 0.5f;
    const f32 fillX = min.x + ImFloor((max.x - min.x) * ImSaturate(fraction));

    drawList->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_FrameBg), rounding);

    if (fillX > min.x + rounding * 2.0f)
        drawList->AddRectFilled(min, ImVec2(fillX, max.y), ImGui::GetColorU32(ImGuiCol_PlotHistogram), rounding);
}

bool DrawRowOpenButton(u32 id, const char* tooltip, const char* disabledReason)
{
    ImGui::BeginDisabled(disabledReason != nullptr);

    const bool pressed = DrawRowButton(id, ICON_LC_FILE_PEN, false);

    ImGui::EndDisabled();

    ImGui::SetItemTooltip("%s", disabledReason ? disabledReason : tooltip);

    return pressed;
}

s32 GetItemWheelSteps()
{
    if (!ImGui::IsItemHovered())
        return 0;

    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);

    return -static_cast<s32>(ImGui::GetIO().MouseWheel);
}

bool ComboScroll(const char *label, int *current_item, const char *const items[], int items_count, int popup_max_height_in_items)
{
    const int oldItem = *current_item;

    ImGui::Combo(label, current_item, items, items_count, popup_max_height_in_items);

    const s32 wheelSteps = GetItemWheelSteps();
    if (wheelSteps != 0)
        *current_item = sead::MathCalcCommon<s32>::clamp2(0, *current_item + wheelSteps, items_count - 1);

    return *current_item != oldItem;
}

static s32 StepToEnabledItem(s32 current, s32 steps, const char* const reasons[], s32 itemNum)
{
    const s32 direction = steps > 0 ? 1 : -1;

    for (s32 left = std::abs(steps); left > 0; left--)
    {
        s32 next = current + direction;

        while (next >= 0 && next < itemNum && reasons && reasons[next])
            next += direction;

        if (next < 0 || next >= itemNum)
            break;

        current = next;
    }

    return current;
}

bool ComboScroll(const char* label, s32* current, const char* const items[], const char* const details[], s32 itemNum, const char* detail)
{
    return ComboScroll(label, current, items, details, nullptr, itemNum, detail);
}

bool ComboScroll(const char* label, s32* current, const char* const items[], const char* const details[], const char* const reasons[], s32 itemNum, const char* detail)
{
    const s32 oldItem = *current;
    const bool valid = *current >= 0 && *current < itemNum;
    const ListOpener opener = GetNextListOpener();

    if (ImGui::BeginCombo(label, valid ? items[*current] : ""))
    {
        for (s32 i = 0; i < itemNum; i++)
        {
            const char* reason = reasons ? reasons[i] : nullptr;

            ImGui::BeginDisabled(reason != nullptr);

            if (ImGui::Selectable(items[i], i == *current))
                *current = i;

            ImGui::EndDisabled();

            if (i == *current)
                ImGui::SetItemDefaultFocus();

            ListItemDetailTooltip(reason ? reason : details[i], opener);
        }

        ImGui::EndCombo();
    }
    else
        DetailTooltip(detail);

    const s32 wheelSteps = GetItemWheelSteps();
    if (wheelSteps != 0)
        *current = StepToEnabledItem(*current, wheelSteps, reasons, itemNum);

    return *current != oldItem;
}

sead::FixedSafeString<24> FormatPanLabel(f32 pan)
{
    sead::FixedSafeString<24> out;

    if (pan > -0.005f && pan < 0.005f)
        out.copy(messages::sound::cPanCentered);
    else if (pan < 0.0f)
        out.format(messages::sound::cPanLeftFormat, static_cast<s32>(-pan * 100.0f + 0.5f));
    else
        out.format(messages::sound::cPanRightFormat, static_cast<s32>(pan * 100.0f + 0.5f));

    return out;
}

sead::FixedSafeString<24> FormatPanLabel(u8 pan, u8 min, u8 center, u8 max)
{
    f32 normalized;

    if (pan == center)
        normalized = 0.0f;
    else if (pan < center)
        normalized = -static_cast<f32>(center - pan) / static_cast<f32>(center - min);
    else
        normalized = static_cast<f32>(pan - center) / static_cast<f32>(max - center);

    return FormatPanLabel(normalized);
}

sead::FixedSafeString<32> FormatCountText(u64 count)
{
    const sead::FormatFixedSafeString<32> digits("%llu", static_cast<unsigned long long>(count));
    const s32 digitNum = digits.calcLength();
    sead::FixedSafeString<32> out;

    for (s32 i = 0; i < digitNum; i++)
    {
        if (i > 0 && (digitNum - i) % 3 == 0)
            out.append(messages::common::cDigitGroupSeparator);

        out.append(digits.at(i));
    }

    return out;
}

static const char* ArchiveVersionRangeStr(bool isBcsar)
{
    return isBcsar ? "2.0.0 - 2.3.2" : "1.0.0 - 2.x";
}

static bool VersionField(u8* value, u8 maxValue)
{
    const u8 minValue = 0;

    ImGui::SetNextItemWidth(ImFloor(ImGui::GetFontSize() * cVersionFieldEm));

    return DragField("##value", ImGuiDataType_U8, value, cPropertyDragSpeed, &minValue, &maxValue, "%d", ImGuiSliderFlags_AlwaysClamp);
}

f32 CalcVersionUIWidth()
{
    return ImFloor(ImGui::GetFontSize() * cVersionFieldEm) * 3.0f + (cVersionDotGap * 2.0f + ImGui::CalcTextSize(".").x) * 2.0f;
}

bool DrawVersionUI(u32 *versionPtr, ArchiveFormat format, bool showLabel)
{
    const bool isBcsar = (format == ArchiveFormat::BCSAR);
    const bool highByteMajor = isBcsar || nw::snd::internal::Util::IsHighByteMajorVersion(*versionPtr);
    const u32 shift[3] = {highByteMajor ? 24u : 16u, highByteMajor ? 16u : 8u, highByteMajor && !isBcsar ? 8u : 0u};

    const u32 version = *versionPtr;
    u8 field[3] = {
        (u8)((version >> shift[0]) & 0xFF),
        (u8)((version >> shift[1]) & 0xFF),
        (u8)((version >> shift[2]) & 0xFF),
    };

    bool updated = false;

    for (int i = 0; i < 3; i++)
    {
        if (i > 0)
        {
            ImGui::SameLine(0.0f, cVersionDotGap);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled(".");
            ImGui::SameLine(0.0f, cVersionDotGap);
        }

        ImGui::PushID(i);

        if (VersionField(&field[i], 9))
            updated = true;

        ImGui::PopID();
    }

    if (showLabel)
    {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(messages::common::cVersion);
    }

    if (ImGui::BeginItemTooltip())
    {
        ImGui::Text(messages::project::cVersionRangeFormat, ArchiveVersionRangeStr(isBcsar));
        ImGui::TextDisabled("%s", messages::project::cVersionEditDetail);
        ImGui::EndTooltip();
    }

    if (updated)
        *versionPtr = ((u32)field[0] << shift[0]) | ((u32)field[1] << shift[1]) | ((u32)field[2] << shift[2]);

    return updated;
}

bool DrawInnerVersionUI(u32 *versionPtr, u32 versionByteNum)
{
    const ImU8 stepU8 = 1;

    bool updated = false;
    for (u32 i = 0; i < versionByteNum; i++)
    {
        ImGui::PushID(i);
        ImGui::SetNextItemWidth(80.0f);

        u32 version = *versionPtr;
        u32 versionByteOffset = ((versionByteNum - 1) - i) * 0x8;

        const char *label = (i + 1 == versionByteNum) ? messages::common::cVersion : "";

        u8 versionByte = (version >> versionByteOffset) & 0xFF;
        if (ImGui::InputScalar(label, ImGuiDataType_U8, &versionByte, &stepU8))
        {
            *versionPtr = (version & ~(0xFF << versionByteOffset)) | (versionByte << versionByteOffset);
            updated = true;
        }

        if (i + 1 < versionByteNum)
            ImGui::SameLine();

        ImGui::PopID();
    }

    return updated;
}

void CenteredText(const char* text, const ImVec2& sizeArg)
{
    ImVec2 textSize = ImGui::CalcTextSize(text);

    if (sizeArg.x != -1)
        ImGui::SetCursorPosX((sizeArg.x / 2.0f) - (textSize.x / 2.0f));

    if (sizeArg.y != -1)
        ImGui::SetCursorPosY((sizeArg.y / 2.0f) - (textSize.y / 2.0f));

    ImGui::Text("%s", text);
}

void CenteredTextWrapped(const char* text, f32 width)
{
    const f32 left = ImGui::GetCursorPosX();
    const char* end = text + strlen(text);
    ImFont* font = ImGui::GetFont();
    const f32 scale = ImGui::GetFontSize() / font->FontSize;

    while (text < end)
    {
        const char* lineEnd = font->CalcWordWrapPositionA(scale, text, end, width);
        if (lineEnd == text)
            lineEnd++;

        const char* textEnd = lineEnd;
        while (textEnd > text && textEnd[-1] == ' ')
            textEnd--;

        ImGui::SetCursorPosX(left + ImFloor((width - ImGui::CalcTextSize(text, textEnd).x) * 0.5f));
        ImGui::TextUnformatted(text, textEnd);

        text = lineEnd;
        while (text < end && *text == ' ')
            text++;
    }
}

static bool IsUtf8Continuation(char c)
{
    return (static_cast<u8>(c) & 0xC0) == 0x80;
}

static const char* NextUtf8Char(const char* text)
{
    text++;

    while (IsUtf8Continuation(*text))
        text++;

    return text;
}

static const char* FindUtf8CharStart(const char* text, const char* min)
{
    while (text > min && IsUtf8Continuation(*text))
        text--;

    return text;
}

const char* FindTextTailStart(const char* text, f32 width)
{
    const char* first = text;
    const char* last = text + strlen(text);

    while (first < last)
    {
        const char* middle = FindUtf8CharStart(first + (last - first) / 2, first);

        if (ImGui::CalcTextSize(middle).x <= width)
            last = middle;
        else
            first = NextUtf8Char(middle);
    }

    return last;
}

const char* FindTextHeadEnd(const char* text, f32 width)
{
    const char* end = text + strlen(text);

    if (ImGui::CalcTextSize(text).x <= width)
        return end;

    const char* first = text;
    const char* last = end;

    while (first < last && NextUtf8Char(first) < last)
    {
        const char* middle = FindUtf8CharStart(first + (last - first) / 2, first);

        if (middle == first)
            middle = NextUtf8Char(first);

        if (ImGui::CalcTextSize(text, middle).x <= width)
            first = middle;
        else
            last = middle;
    }

    return first;
}

std::string CutPathStart(const std::string& path, size_t maxLength)
{
    const size_t markLength = strlen(messages::backup::cPathCutMark);

    if (path.size() <= maxLength || maxLength <= markLength)
        return path;

    return messages::backup::cPathCutMark + path.substr(path.size() - maxLength + markLength);
}

bool IsItemHoveredForDetail()
{
    return ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_NoSharedDelay);
}

static void ShowDetailTooltip(const char* detail, const ImVec2* pos, const ImVec2& pivot)
{
    if (!detail || ImGui::IsItemActive())
        return;

    if (!IsItemHoveredForDetail())
        return;

    char name[16];
    ImFormatString(name, IM_ARRAYSIZE(name), "##Tooltip_%02d", GImGui->TooltipOverrideCount);
    const ImGuiWindow* tooltip = ImGui::FindWindowByName(name);
    const bool below = tooltip && tooltip->Active;

    if (pos && !below)
        ImGui::SetNextWindowPos(*pos, ImGuiCond_Always, pivot);

    const ImGuiNextItemData nextItemData = GImGui->NextItemData;

    ImGui::BeginTooltip();

    if (below)
        ImGui::Separator();

    ImGui::PushTextWrapPos(ImGui::GetFontSize() * cDetailTooltipWrapEm);
    ImGui::TextUnformatted(detail);
    ImGui::PopTextWrapPos();

    ImGui::EndTooltip();

    GImGui->NextItemData = nextItemData;
}

void DetailTooltip(const char* detail)
{
    ShowDetailTooltip(detail, nullptr, ImVec2(0.0f, 0.0f));
}

ListOpener GetNextListOpener()
{
    ListOpener opener;
    opener.top = ImGui::GetCursorScreenPos().y;
    opener.bottom = opener.top + ImGui::GetFrameHeight();

    return opener;
}

void ListItemDetailTooltip(const char* detail, const ListOpener& opener)
{
    const ImGuiWindow* list = ImGui::GetCurrentWindow();
    const ImGuiViewport* viewport = list->Viewport;
    const f32 gap = ImGui::GetStyle().ItemSpacing.y;
    const f32 top = sead::Mathf::min(list->Pos.y, opener.top);
    const f32 bottom = sead::Mathf::max(list->Pos.y + list->Size.y, opener.bottom);
    const f32 spaceBelow = viewport->Pos.y + viewport->Size.y - bottom;
    const f32 spaceAbove = top - viewport->Pos.y;
    const bool right = list->Pos.x + list->Size.x * 0.5f > viewport->Pos.x + viewport->Size.x * 0.5f;
    const bool below = spaceBelow >= spaceAbove;

    const ImVec2 pos(right ? list->Pos.x + list->Size.x : list->Pos.x, below ? bottom + gap : top - gap);
    ShowDetailTooltip(detail, &pos, ImVec2(right ? 1.0f : 0.0f, below ? 0.0f : 1.0f));
}

void HelpMarker(const char* desc)
{
    ImGui::TextDisabled("%s", messages::popup::cDetailMarker);
    DetailTooltip(desc);
}

void InnerFile::drawUI()
{
    {
        static const char* sEndianTypes[] = {
            messages::innerfile::cBigEndian,
            messages::innerfile::cLittleEndian
        };

        sead::Endian::Types endian = mEndian;
        if (ComboScroll(messages::innerfile::cByteOrder, (s32*)&endian, sEndianTypes, IM_ARRAYSIZE(sEndianTypes)))
        {
            mEndian = endian;
            SetUnsavedChanges(true);
        }
    }

    {
        CenteredTextX(messages::common::cVersion);

        u32 version = mVersion;
        if (DrawInnerVersionUI(&version, GetArchiveVersionByteCount(mFormat)))
        {
            mVersion = version;
            SetUnsavedChanges(true);
        }
    }
}
