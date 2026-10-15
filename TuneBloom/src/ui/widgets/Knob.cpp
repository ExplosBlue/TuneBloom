#include <ui/widgets/Knob.h>

#include <ui/Messages.h>
#include <ui/settings/Theme.h>
#include <ui/app/UI.h>

#include <imgui/imgui_custom.h>
#include <imgui/imgui_internal.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <math/seadMathCalcCommon.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>

static constexpr f32 cRegionStripEm = 3.5f;

static constexpr f32 cKnobArcMin         = IM_PI * 0.75f;
static constexpr f32 cKnobArcMax         = IM_PI * 2.25f;
static constexpr f32 cKnobDragSpeed      = 0.5f;
static constexpr f32 cKnobCoarseSpeed    = 0.1f;
static constexpr f32 cKnobFineSpeed      = 0.25f;
static constexpr f32 cKnobShiftRatio     = 0.1f;
static constexpr f32 cKnobHoverTint      = 0.3f;
static constexpr f32 cKnobActiveTint     = 0.65f;
static constexpr f32 cKnobLabelGap       = 2.0f;
static constexpr f32 cKnobLabelMinScale  = 0.7f;

static bool sRegionPanelFill = false;

void SetRegionPanelFill(bool fill)
{
    sRegionPanelFill = fill;
}

bool IsRegionPanelFill()
{
    return sRegionPanelFill;
}

void RegionHeader(const char* title, f32 reservedWidth)
{
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const f32 frameHeight = ImGui::GetFrameHeight();
    const f32 right = pos.x + ImGui::GetContentRegionAvail().x - reservedWidth;
    const f32 textRight = sead::Mathf::max(pos.x, sead::Mathf::min(pos.x + ImGui::CalcTextSize(title).x, right));
    const f32 left = textRight + ImGui::GetStyle().ItemSpacing.x;
    const f32 y = ImFloor(pos.y + frameHeight * 0.5f);

    ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), ImVec2(pos.x, pos.y + ImGui::GetStyle().FramePadding.y), ImVec2(textRight, pos.y + frameHeight), textRight, textRight, title, nullptr, nullptr);
    ImGui::Dummy(ImVec2(textRight - pos.x, frameHeight));

    if (left < right)
        ImGui::GetWindowDrawList()->AddLine(ImVec2(left, y), ImVec2(right, y), ImGui::GetColorU32(ImGuiCol_Separator));
}

void CenterRegionKnobs(f32 rowWidth)
{
    if (!IsRegionPanelFill())
        return;

    const f32 offset = ImFloor((ImGui::GetContentRegionAvail().x - rowWidth) * 0.5f);

    if (offset > 0.0f)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
}

f32 CalcRegionStripHeight()
{
    return ImFloor(ImGui::GetFontSize() * cRegionStripEm);
}

f32 CalcKnobGap()
{
    return ImFloor(ImGui::GetFontSize() * cRegionKnobGapEm);
}

f32 CalcKnobWidth(const char* const* labels, s32 labelNum)
{
    f32 width = CalcRegionStripHeight() - ImGui::GetFontSize();
    for (s32 i = 0; i < labelNum; i++)
        width = std::max(width, ImGui::CalcTextSize(labels[i]).x);

    return width;
}

f32 CalcKnobFitWidth(const char* const* labels, s32 labelNum)
{
    f32 width = CalcRegionStripHeight() - ImGui::GetFontSize();
    for (s32 i = 0; i < labelNum; i++)
        width = std::max(width, ImFloor(ImGui::CalcTextSize(labels[i]).x * cKnobLabelMinScale));

    return width;
}

f32 CalcKnobRowWidth(const char* const* labels, s32 labelNum)
{
    return CalcKnobWidth(labels, labelNum) * labelNum + ImGui::GetFontSize() * cRegionKnobGapEm * (labelNum - 1);
}

sead::FixedSafeString<8> FormatPanText(u8 pan, u8 center)
{
    if (pan == center)
        return sead::FixedSafeString<8>(messages::bank::cPanCenter);

    if (pan < center)
        return sead::FormatFixedSafeString<8>(messages::bank::cPanLeftFormat, center - pan);

    return sead::FormatFixedSafeString<8>(messages::bank::cPanRightFormat, pan - center);
}

static f32 CalcKnobRatio(ImGuiDataType dataType, const void* value, const void* min, const void* max)
{
    if (dataType == ImGuiDataType_Float)
        return (*static_cast<const f32*>(value) - *static_cast<const f32*>(min)) / (*static_cast<const f32*>(max) - *static_cast<const f32*>(min));

    const s32 base = *static_cast<const u8*>(min);

    return static_cast<f32>(*static_cast<const u8*>(value) - base) / static_cast<f32>(*static_cast<const u8*>(max) - base);
}

static f32 CalcKnobScaleRatio(KnobScale scale, u8 value, u8 min, u8 max)
{
    const f32 low = scale(min);

    return (scale(value) - low) / (scale(max) - low);
}

static f32 CalcKnobArcRatio(bool logScale, KnobScale scale, ImGuiDataType dataType, const void* value, const void* min, const void* max)
{
    if (scale)
        return CalcKnobScaleRatio(scale, *static_cast<const u8*>(value), *static_cast<const u8*>(min), *static_cast<const u8*>(max));

    if (!logScale)
        return CalcKnobRatio(dataType, value, min, max);

    const f32 low = log2f(*static_cast<const f32*>(min));
    const f32 high = log2f(*static_cast<const f32*>(max));
    const f32 octaves = log2f(sead::Mathf::clamp2(*static_cast<const f32*>(min), *static_cast<const f32*>(value), *static_cast<const f32*>(max)));

    return (octaves - low) / (high - low);
}

static bool DragLogKnob(ImGuiID id, f32* value, f32 min, f32 max, f32 speed, ImGuiSliderFlags flags)
{
    const f32 low = log2f(min);
    const f32 high = log2f(max);
    f32 octaves = log2f(sead::Mathf::clamp2(min, *value, max));

    if (!ImGui::DragBehavior(id, ImGuiDataType_Float, &octaves, speed, &low, &high, "%.3f", flags))
        return false;

    *value = exp2f(octaves);
    return true;
}

static ImGuiID sKnobScaleDragId = 0;
static f32 sKnobScaleDragRatio = 0.0f;

static bool DragScaleKnob(ImGuiID id, KnobScale scale, u8* value, u8 min, u8 max, f32 speed, ImGuiSliderFlags flags)
{
    static const f32 cRatioMin = 0.0f;
    static const f32 cRatioMax = 1.0f;

    if (GImGui->ActiveId != id)
        return false;

    if (sKnobScaleDragId != id || GImGui->ActiveIdIsJustActivated)
    {
        sKnobScaleDragId = id;
        sKnobScaleDragRatio = CalcKnobScaleRatio(scale, *value, min, max);
    }

    const s32 range = abs(static_cast<s32>(max) - static_cast<s32>(min));

    if (!ImGui::DragBehavior(id, ImGuiDataType_Float, &sKnobScaleDragRatio, speed / range, &cRatioMin, &cRatioMax, "%.4f", flags))
        return false;

    // Nearest value
    const s32 step = min < max ? 1 : -1;
    u8 nearest = min;
    f32 nearestDistance = FLT_MAX;

    for (s32 i = 0; i <= range; i++)
    {
        const u8 candidate = static_cast<u8>(min + i * step);
        const f32 distance = fabsf(CalcKnobScaleRatio(scale, candidate, min, max) - sKnobScaleDragRatio);

        if (distance < nearestDistance)
        {
            nearest = candidate;
            nearestDistance = distance;
        }
    }

    if (nearest == *value)
        return false;

    *value = nearest;
    return true;
}

static ImGuiID sKnobCursorId = 0;
static ImVec2 sKnobTooltipPos;

static void SetKnobCursorLock(ImGuiID id)
{
    GLFWwindow* glfwWindow = static_cast<GLFWwindow*>(ImGui::GetMainViewport()->PlatformHandle);

    if (id == sKnobCursorId || !glfwWindow)
        return;

    glfwSetInputMode(glfwWindow, GLFW_CURSOR, id != 0 ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    sKnobCursorId = id;
    const ImVec2& mousePos = ImGui::GetIO().MousePos;
    const f32 cursorScale = ImGui::GetStyle().MouseCursorScale;
    sKnobTooltipPos = ImVec2(mousePos.x + 16.0f * cursorScale, mousePos.y + 8.0f * cursorScale);
}

void KnobTooltip(const char* format, ...)
{
    if (!ImGui::IsItemHovered() && !ImGui::IsItemActive())
        return;

    if (sKnobCursorId != 0)
        ImGui::SetNextWindowPos(sKnobTooltipPos);

    va_list args;
    va_start(args, format);
    ImGui::SetTooltipV(format, args);
    va_end(args);
}

void UpdateKnobCursor()
{
    if (sKnobCursorId != 0 && GImGui->ActiveId != sKnobCursorId)
        SetKnobCursorLock(0);
}

static bool sNextKnobHasReset = false;
static f64 sNextKnobReset = 0.0;
static const char* sNextKnobText = nullptr;
static bool sNextKnobLogScale = false;
static KnobScale sNextKnobScale = nullptr;
static sead::FixedSafeString<64> sNextKnobTextBuffer;

void SetNextKnobReset(f64 value)
{
    sNextKnobHasReset = true;
    sNextKnobReset = value;
}

void SetNextKnobText(const char* text)
{
    if (!text)
    {
        sNextKnobText = nullptr;
        return;
    }

    sNextKnobTextBuffer.cutOffCopy(text);
    sNextKnobText = sNextKnobTextBuffer.cstr();
}

void SetNextKnobScale(KnobScale scale)
{
    sNextKnobScale = scale;
}

static bool SetKnobValue(ImGuiDataType dataType, void* value, f64 newValue, const void* min, const void* max)
{
    switch (dataType)
    {
    case ImGuiDataType_U8:
    {
        const u8 v = static_cast<u8>(sead::MathCalcCommon<f64>::clamp2(*static_cast<const u8*>(min), newValue, *static_cast<const u8*>(max)));
        const bool changed = *static_cast<u8*>(value) != v;
        *static_cast<u8*>(value) = v;
        return changed;
    }
    case ImGuiDataType_U16:
    {
        const u16 v = static_cast<u16>(sead::MathCalcCommon<f64>::clamp2(*static_cast<const u16*>(min), newValue, *static_cast<const u16*>(max)));
        const bool changed = *static_cast<u16*>(value) != v;
        *static_cast<u16*>(value) = v;
        return changed;
    }
    case ImGuiDataType_S32:
    {
        const s32 v = static_cast<s32>(sead::MathCalcCommon<f64>::clamp2(*static_cast<const s32*>(min), newValue, *static_cast<const s32*>(max)));
        const bool changed = *static_cast<s32*>(value) != v;
        *static_cast<s32*>(value) = v;
        return changed;
    }
    case ImGuiDataType_U32:
    {
        const u32 v = static_cast<u32>(sead::MathCalcCommon<f64>::clamp2(*static_cast<const u32*>(min), newValue, *static_cast<const u32*>(max)));
        const bool changed = *static_cast<u32*>(value) != v;
        *static_cast<u32*>(value) = v;
        return changed;
    }
    case ImGuiDataType_Float:
    {
        const f32 v = static_cast<f32>(sead::MathCalcCommon<f64>::clamp2(*static_cast<const f32*>(min), newValue, *static_cast<const f32*>(max)));
        const bool changed = *static_cast<f32*>(value) != v;
        *static_cast<f32*>(value) = v;
        return changed;
    }
    default:
        return false;
    }
}

static f32 CalcKnobDragSpeed(f32 speed)
{
    const ImGuiIO& io = ImGui::GetIO();

    if (io.KeyShift)
        speed *= cKnobShiftRatio / 10.0f;

    if (io.KeyAlt)
        speed *= 100.0f;

    return speed;
}

bool DragField(const char* label, ImGuiDataType dataType, void* value, f32 speed, const void* min, const void* max, const char* format, ImGuiSliderFlags flags)
{
    const bool hasReset = sNextKnobHasReset && min && max;
    const f64 reset = sNextKnobReset;
    const char* text = sNextKnobText;
    sNextKnobHasReset = false;
    sNextKnobText = nullptr;

    if (text && !ImGui::TempInputIsActive(ImGui::GetID(label)))
        format = text;

    bool changed = ImGui::DragScalar(label, dataType, value, CalcKnobDragSpeed(speed), min, max, format, flags | static_cast<ImGuiSliderFlags>(ImGuiSliderFlags_Vertical));
    const ImGuiID id = ImGui::GetItemID();

    const bool altClicked = ImGui::IsItemActivated() && ImGui::GetIO().KeyAlt;

    if (hasReset && (altClicked || ImGui::IsItemClicked(ImGuiMouseButton_Right)) && !ImGui::TempInputIsActive(id))
    {
        if (SetKnobValue(dataType, value, reset, min, max))
        {
            ImGui::MarkItemEdited(id);
            changed = true;
        }

        if (altClicked)
        {
            ImGui::ClearActiveID();
            GImGui->NavWindowingToggleLayer = false;
        }
    }

    if (ImGui::IsItemActive() && !ImGui::TempInputIsActive(id))
        SetKnobCursorLock(id);
    else if (sKnobCursorId == id)
        SetKnobCursorLock(0);

    return changed;
}

static void DrawKnobFace(ImGuiID id, const ImVec2& center, f32 diameter, bool hovered, bool active, f32 ratio, f32 originRatio, const char* text)
{
    const f32 thickness = std::max(3.0f, ImFloor(diameter * 0.12f));
    const f32 radius = (diameter - thickness) * 0.5f;

    const ImVec4& accent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
    const ImVec4& bright = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    const f32 hoverRatio = ImGui::GetHoverRatio(id, hovered || active);
    const ImVec4 valueColor = ImLerp(accent, bright, active ? cKnobActiveTint : cKnobHoverTint * hoverRatio);

    ImDrawList* draw = ImGui::GetWindowDrawList();

    draw->PathArcTo(center, radius, cKnobArcMin, cKnobArcMax);
    draw->PathStroke(GetTrackColorU32(ImGui::GetHoverColorU32(id, hovered, active, ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive)), ImDrawFlags_None, thickness);

    if (ratio != originRatio)
    {
        draw->PathArcTo(center, radius, cKnobArcMin + (cKnobArcMax - cKnobArcMin) * std::min(ratio, originRatio), cKnobArcMin + (cKnobArcMax - cKnobArcMin) * std::max(ratio, originRatio));
        draw->PathStroke(ImGui::GetColorU32(valueColor), ImDrawFlags_None, thickness);
    }

    const ImVec2 textSize = ImGui::CalcTextSize(text);
    ImGui::RenderText(ImVec2(ImFloor(center.x - textSize.x * 0.5f), ImFloor(center.y - textSize.y * 0.5f)), text);
}

static bool Knob(const char* label, ImGuiDataType dataType, void* value, const void* min, const void* max, const void* origin, const char* format, f32 speed, const ImVec2& size)
{
    const bool hasReset = sNextKnobHasReset || CalcKnobRatio(dataType, origin, min, max) != 0.0f;
    const f64 reset = sNextKnobHasReset ? sNextKnobReset : dataType == ImGuiDataType_Float ? *static_cast<const f32*>(origin) : *static_cast<const u8*>(origin);
    const char* valueText = sNextKnobText;
    const bool logScale = sNextKnobLogScale && dataType == ImGuiDataType_Float;
    const KnobScale scale = dataType == ImGuiDataType_U8 ? sNextKnobScale : nullptr;
    sNextKnobHasReset = false;
    sNextKnobText = nullptr;
    sNextKnobLogScale = false;
    sNextKnobScale = nullptr;

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;

    ImGui::PushID(label);
    const ImGuiID id = window->GetID("##Knob");
    ImGui::PopID();

    const ImVec2 pos = window->DC.CursorPos;
    const ImVec2 labelSize = ImGui::CalcTextSize(label, nullptr, true);
    const f32 diameter = ImFloor(labelSize.x > 0.0f ? size.y - g.FontSize - cKnobLabelGap : size.y);
    const ImVec2 center(ImFloor(pos.x + size.x * 0.5f), pos.y + diameter * 0.5f);
    const ImRect totalBb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
    const ImRect knobBb(ImVec2(center.x - diameter * 0.5f, pos.y), ImVec2(center.x + diameter * 0.5f, pos.y + diameter));

    ImGui::ItemSize(totalBb);
    if (!ImGui::ItemAdd(totalBb, id, &knobBb, ImGuiItemFlags_Inputable))
        return false;

    const f32 labelScale = labelSize.x > size.x ? std::max(size.x / labelSize.x, cKnobLabelMinScale) : 1.0f;
    const f32 labelAscent = g.Font->Ascent * g.FontSize / g.Font->FontSize;
    const ImVec2 labelPos(ImFloor(pos.x + (size.x - labelSize.x * labelScale) * 0.5f), ImFloor(totalBb.Max.y - g.FontSize + labelAscent * (1.0f - labelScale)));
    window->DrawList->AddText(g.Font, g.FontSize * labelScale, labelPos, ImGui::GetColorU32(ImGuiCol_Text), label, ImGui::FindRenderedTextEnd(label));

    const bool hovered = ImGui::ItemHoverable(knobBb, id, g.LastItemData.InFlags);
    const bool reversed = dataType == ImGuiDataType_U8 && *static_cast<const u8*>(min) > *static_cast<const u8*>(max);
    const void* low = reversed ? max : min;
    const void* high = reversed ? min : max;

    bool tempInput = ImGui::TempInputIsActive(id);
    bool resetClicked = false;
    if (!tempInput)
    {
        const bool clicked = hovered && ImGui::IsMouseClicked(0, id);
        const bool doubleClicked = hovered && g.IO.MouseClickedCount[0] == 2 && ImGui::TestKeyOwner(ImGuiKey_MouseLeft, id);
        const bool makeActive = clicked || doubleClicked || g.NavActivateId == id;

        if (clicked || doubleClicked)
            ImGui::SetKeyOwner(ImGuiKey_MouseLeft, id);

        const bool rightClicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        resetClicked = hasReset && (((clicked || doubleClicked) && g.IO.KeyAlt && !g.IO.KeyCtrl) || rightClicked);

        if (resetClicked)
            g.NavWindowingToggleLayer = false;
        else if ((clicked && g.IO.KeyCtrl) || doubleClicked || (g.NavActivateId == id && (g.NavActivateFlags & ImGuiActivateFlags_PreferInput)))
            tempInput = true;

        if (makeActive && !tempInput && !resetClicked)
        {
            ImGui::SetActiveID(id, window);
            ImGui::SetFocusID(id, window);
            ImGui::FocusWindow(window);
            g.ActiveIdUsingNavDirMask = (1 << ImGuiDir_Up) | (1 << ImGuiDir_Down);
        }
    }

    if (tempInput)
    {
        if (sKnobCursorId == id)
            SetKnobCursorLock(0);

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(g.Style.FramePadding.x, ImFloor((diameter - g.FontSize) * 0.5f)));
        ImGui::PushID(label);
        const bool changed = ImGui::TempInputScalar(knobBb, id, "##Knob", dataType, value, format, low, high);
        ImGui::PopID();
        ImGui::PopStyleVar();

        window->DC.CursorPos = pos;
        ImGui::ItemSize(totalBb);

        return changed;
    }

    const ImGuiSliderFlags dragFlags = static_cast<ImGuiSliderFlags>(ImGuiSliderFlags_Vertical) | ImGuiSliderFlags_AlwaysClamp;
    const f32 dragSpeed = CalcKnobDragSpeed(speed);
    bool changed = false;

    if (resetClicked)
    {
        changed = SetKnobValue(dataType, value, reset, low, high);
    }
    else if (scale)
    {
        changed = DragScaleKnob(id, scale, static_cast<u8*>(value), *static_cast<const u8*>(min), *static_cast<const u8*>(max), dragSpeed, dragFlags);
    }
    else if (reversed)
    {
        const u8 sum = *static_cast<const u8*>(low) + *static_cast<const u8*>(high);
        u8 mirrored = sum - *static_cast<u8*>(value);

        changed = ImGui::DragBehavior(id, dataType, &mirrored, dragSpeed, low, high, format, dragFlags);
        if (changed)
            *static_cast<u8*>(value) = sum - mirrored;
    }
    else if (logScale)
    {
        changed = DragLogKnob(id, static_cast<f32*>(value), *static_cast<const f32*>(low), *static_cast<const f32*>(high), dragSpeed, dragFlags);
    }
    else
    {
        changed = ImGui::DragBehavior(id, dataType, value, dragSpeed, low, high, format, dragFlags);
    }

    if (changed)
        ImGui::MarkItemEdited(id);

    const bool active = g.ActiveId == id;

    if (active)
        SetKnobCursorLock(id);
    else if (sKnobCursorId == id)
        SetKnobCursorLock(0);

    char text[16];
    if (valueText)
        ImFormatString(text, IM_ARRAYSIZE(text), "%s", valueText);
    else
        ImGui::DataTypeFormatString(text, IM_ARRAYSIZE(text), dataType, value, format);

    ImGui::RenderNavHighlight(knobBb, id);
    DrawKnobFace(id, center, diameter, hovered, active, CalcKnobArcRatio(logScale, scale, dataType, value, min, max), CalcKnobArcRatio(logScale, scale, dataType, origin, min, max), text);

    return changed;
}

bool DrawPitchKnobs(f32* pitch, const ImVec2& knobSize)
{
    static const f32 cCoarseMin = -36.0f;
    static const f32 cCoarseMax = 36.0f;
    static const f32 cFineMin = -50.0f;
    static const f32 cFineMax = 50.0f;
    static const f32 cCenter = 0.0f;
    static const f32 cRatioMin = 0.125f;
    static const f32 cRatioMax = 8.0f;

    const f32 semitones = 12.0f * log2f(sead::Mathf::clamp2(cRatioMin, *pitch, cRatioMax));
    f32 coarse = roundf(semitones) + 0.0f;
    f32 fine = roundf((semitones - coarse) * 100.0f) + 0.0f;
    bool edited = false;

    if (Knob(messages::param::cCoarse,ImGuiDataType_Float, &coarse, &cCoarseMin, &cCoarseMax, &cCenter, "%+.0f", cKnobCoarseSpeed, knobSize))
        edited = true;

    KnobTooltip(messages::bank::cCoarseTooltipFormat, coarse, *pitch);

    ImGui::SameLine(0.0f, CalcKnobGap());
    if (Knob(messages::param::cFine,ImGuiDataType_Float, &fine, &cFineMin, &cFineMax, &cCenter, "%+.0f", cKnobFineSpeed, knobSize))
        edited = true;

    KnobTooltip(messages::bank::cFineTooltipFormat, fine, *pitch);

    if (edited)
        *pitch = sead::Mathf::clamp2(cRatioMin, powf(2.0f, (coarse + fine / 100.0f) / 12.0f), cRatioMax);

    return edited;
}

bool KnobU8(const char* label, u8* value, u8 min, u8 max, const ImVec2& size)
{
    return Knob(label, ImGuiDataType_U8, value, &min, &max, &min, "%d", cKnobDragSpeed, size);
}

bool KnobU8(const char* label, u8* value, u8 min, u8 max, u8 origin, const ImVec2& size)
{
    return Knob(label, ImGuiDataType_U8, value, &min, &max, &origin, "%d", cKnobDragSpeed, size);
}

bool KnobF32(const char* label, f32* value, f32 min, f32 max, const char* format, f32 speed, const ImVec2& size)
{
    return Knob(label, ImGuiDataType_Float, value, &min, &max, &min, format, speed, size);
}

bool KnobF32(const char* label, f32* value, f32 min, f32 max, f32 origin, const char* format, f32 speed, const ImVec2& size)
{
    return Knob(label, ImGuiDataType_Float, value, &min, &max, &origin, format, speed, size);
}

bool KnobRatio(const char* label, f32* value, f32 min, f32 max, const char* format, f32 speed, const ImVec2& size)
{
    static const f32 cOrigin = 1.0f;

    sNextKnobLogScale = true;
    return Knob(label, ImGuiDataType_Float, value, &min, &max, &cOrigin, format, speed, size);
}
