#include <ui/widgets/RangeStrip.h>

#include <ui/settings/UiColors.h>

#include <imgui/imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <vector>

static constexpr u8  cSegmentAlpha       = 160;
static constexpr u8  cSegmentStrongAlpha = 255;
static constexpr u8  cTextDimAlpha       = 190;
static constexpr u8  cHoverAlpha         = 35;
static constexpr u8  cOutlineAlpha       = 160;
static constexpr f32 cMarkHeight         = 3.0f;
static constexpr f32 cOutlineWidth       = 1.5f;
static constexpr f32 cDividerMinWidth    = 3.0f;
static constexpr f32 cLabelPad           = 6.0f;
static constexpr f32 cShortLabelPad      = 4.0f;
static constexpr f32 cDarkInkLuminance   = 0.179f;

static f32 LinearChannel(f32 channel)
{
    return channel <= 0.04045f ? channel / 12.92f : std::pow((channel + 0.055f) / 1.055f, 2.4f);
}

static ImU32 CalcInkOnFill(const ImVec4& fill)
{
    const f32 luminance = 0.2126f * LinearChannel(fill.x) + 0.7152f * LinearChannel(fill.y) + 0.0722f * LinearChannel(fill.z);

    return uicolors::ToU32(luminance > cDarkInkLuminance ? uicolors::cInkOnLight : uicolors::cInkOnDark);
}

static void DrawSegmentText(ImDrawList* draw, const RangeSegment& segment, f32 left, f32 right, const char* text, f32 textWidth, ImU32 color)
{
    const f32 bottom = segment.marked ? segment.max.y - cMarkHeight : segment.max.y;
    const f32 x = ImFloor(left + (right - left - textWidth) * 0.5f);

    draw->AddText(ImVec2(x, ImFloor(segment.min.y + (bottom - segment.min.y - ImGui::GetFontSize()) * 0.5f)), color, text);
}

static void DrawSegmentDivider(ImDrawList* draw, const RangeSegment& segment, f32 x, ImU32 color)
{
    draw->AddRectFilled(ImVec2(x, segment.min.y), ImVec2(x + 1.0f, segment.max.y), color);
}

static void DrawSegmentLabel(ImDrawList* draw, const RangeSegment& segment, ImU32 color)
{
    const bool hasLabelSpan = segment.labelRight > segment.labelLeft;
    const f32 left = hasLabelSpan ? segment.labelLeft : segment.min.x;
    const f32 right = hasLabelSpan ? segment.labelRight : segment.max.x;
    const f32 width = right - left;

    if (width <= 0.0f)
        return;

    if (segment.label)
    {
        const f32 labelWidth = ImGui::CalcTextSize(segment.label).x;

        if (width > labelWidth + cLabelPad)
        {
            DrawSegmentText(draw, segment, left, right, segment.label, labelWidth, color);
            return;
        }
    }

    if (segment.shortLabel)
    {
        const f32 shortWidth = ImGui::CalcTextSize(segment.shortLabel).x;

        if (width > shortWidth + cShortLabelPad)
            DrawSegmentText(draw, segment, left, right, segment.shortLabel, shortWidth, color);
    }
}

static bool IsSpanShown(const RangeSpan& span)
{
    return span.right > span.left;
}

static void FindUncoveredSpan(const RangeSpan* spans, s32 spanNum, s32 index, f32* spanLeft, f32* spanRight)
{
    const RangeSpan& own = spans[index];

    std::vector<RangeSpan> covers;
    for (s32 upper = index + 1; upper < spanNum; upper++)
    {
        if (IsSpanShown(spans[upper]) && spans[upper].right > own.left && spans[upper].left < own.right)
            covers.push_back(spans[upper]);
    }

    std::sort(covers.begin(), covers.end(), [](const RangeSpan& a, const RangeSpan& b) { return a.left < b.left; });

    *spanLeft = 0.0f;
    *spanRight = 0.0f;
    f32 cursor = own.left;

    for (const RangeSpan& cover : covers)
    {
        if (cover.left - cursor > *spanRight - *spanLeft)
        {
            *spanLeft = cursor;
            *spanRight = cover.left;
        }

        cursor = std::max(cursor, cover.right);
    }

    if (own.right - cursor > *spanRight - *spanLeft)
    {
        *spanLeft = cursor;
        *spanRight = own.right;
    }
}

static bool IsEndInsideLowerSpan(const RangeSpan* spans, s32 index)
{
    const f32 end = spans[index].right;

    for (s32 lower = 0; lower < index; lower++)
    {
        if (IsSpanShown(spans[lower]) && spans[lower].left < end && spans[lower].right > end)
            return true;
    }

    return false;
}

bool SetRangeSegmentCover(RangeSegment* segment, const RangeSpan* spans, s32 spanNum, s32 index)
{
    FindUncoveredSpan(spans, spanNum, index, &segment->labelLeft, &segment->labelRight);
    segment->endDivider = IsEndInsideLowerSpan(spans, index);

    return segment->labelRight > segment->labelLeft;
}

void DrawRangeSegment(ImDrawList* draw, const RangeSegment& segment)
{
    const f32 width = segment.max.x - segment.min.x;
    const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);
    const ImVec4 fill = uicolors::BlendOver(segment.backdrop, segment.color, (segment.strong ? cSegmentStrongAlpha : cSegmentAlpha) / 255.0f);

    ImU32 textColor = uicolors::WithAlpha(ink, cTextDimAlpha);
    if (segment.strong)
        textColor = CalcInkOnFill(fill);
    else if (segment.marked)
        textColor = ink;

    draw->AddRectFilled(segment.min, segment.max, ImGui::GetColorU32(fill));

    if (segment.marked)
        draw->AddRectFilled(ImVec2(segment.min.x, segment.max.y - cMarkHeight), segment.max, textColor);

    if (width > cDividerMinWidth)
    {
        const ImU32 divider = ImGui::GetColorU32(uicolors::Opaque(segment.backdrop));

        DrawSegmentDivider(draw, segment, segment.min.x, divider);

        if (segment.endDivider)
            DrawSegmentDivider(draw, segment, segment.max.x - 1.0f, divider);
    }

    DrawSegmentLabel(draw, segment, textColor);
}

void DrawRangeSegmentHover(ImDrawList* draw, const ImVec2& min, const ImVec2& max)
{
    const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);

    draw->AddRectFilled(min, max, uicolors::WithAlpha(ink, cHoverAlpha));
    draw->AddRect(min, max, uicolors::WithAlpha(ink, cOutlineAlpha), 0.0f, 0, cOutlineWidth);
}
