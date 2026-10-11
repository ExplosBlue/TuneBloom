#include <ui/widgets/ZoomScrollBar.h>

#include <ui/bank/PianoRollPalette.h>

#include <imgui/imgui_custom.h>
#include <imgui/imgui_internal.h>

#include <math/seadMathCalcCommon.h>

#include <algorithm>

static constexpr f32 cPianoScrollHeightEm = 0.7f;
static constexpr f32 cPianoScrollHandleEm = 0.5f;
static constexpr f32 cPianoScrollThumbEm  = 2.0f;

ZoomScrollResult ZoomScrollBar(const char* id, f32 width, f32* left, f32* right, f32 minSpan, f32 maxSpan)
{
    enum class ScrollPart
    {
        None,
        Thumb,
        HandleL,
        HandleR
    };

    static ScrollPart sScrollDrag = ScrollPart::None;
    static f32 sScrollGrab = 0.0f;

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const f32 em = ImGui::GetFontSize();
    const f32 trackH = ImFloor(em * cPianoScrollHeightEm);
    const f32 handleW = ImFloor(em * cPianoScrollHandleEm);
    const f32 trackW = width;
    const ImVec2 trackOrigin = ImGui::GetCursorScreenPos();
    const f32 mouseX = ImGui::GetIO().MousePos.x;

    f32 leftNorm = *left;
    f32 rightNorm = *right;
    ZoomScrollResult result = ZoomScrollResult::None;

    ImGui::InvisibleButton(id, ImVec2(trackW, trackH));
    const bool scrollHovered = ImGui::IsItemHovered();
    const bool scrollActive = ImGui::IsItemActive();

    const f32 thumbMinW = std::min(trackW, ImFloor(em * cPianoScrollThumbEm));
    f32 thumbW = std::max((rightNorm - leftNorm) * trackW, thumbMinW);
    f32 thumbTravel = trackW - thumbW;
    f32 thumbX = (rightNorm - leftNorm < 1.0f) ? leftNorm / (1.0f - (rightNorm - leftNorm)) * thumbTravel : 0.0f;

    ScrollPart part = ScrollPart::None;
    {
        const f32 x0 = trackOrigin.x + thumbX;
        const f32 x1 = x0 + thumbW;

        if (scrollHovered && mouseX >= x0 && mouseX <= x1)
        {
            if (mouseX < x0 + handleW)
                part = ScrollPart::HandleL;
            else if (mouseX > x1 - handleW)
                part = ScrollPart::HandleR;
            else
                part = ScrollPart::Thumb;
        }

        if (ImGui::IsItemActivated())
        {
            sScrollDrag = (part != ScrollPart::None) ? part : ScrollPart::Thumb;

            if (part == ScrollPart::None)
                sScrollGrab = thumbW * 0.5f;
            else if (part == ScrollPart::HandleR)
                sScrollGrab = mouseX - (trackOrigin.x + rightNorm * trackW);
            else if (part == ScrollPart::HandleL)
                sScrollGrab = mouseX - (trackOrigin.x + leftNorm * trackW);
            else
                sScrollGrab = mouseX - x0;
        }
    }

    if (scrollHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
        sScrollDrag = ScrollPart::None;
        result = ZoomScrollResult::Reset;
    }

    if (scrollActive && sScrollDrag != ScrollPart::None)
    {
        const f32 pos = (mouseX - sScrollGrab - trackOrigin.x) / trackW;
        const f32 span = rightNorm - leftNorm;

        if (sScrollDrag == ScrollPart::Thumb)
        {
            const f32 maxLeft = std::max(0.0f, 1.0f - span);
            const f32 thumbPos = (thumbTravel > 0.0f) ? pos * trackW / thumbTravel * maxLeft : 0.0f;

            leftNorm = sead::MathCalcCommon<f32>::clamp2(0.0f, thumbPos, maxLeft);
            rightNorm = leftNorm + span;
            result = ZoomScrollResult::Scrolled;
        }
        else
        {
            const f32 handleMinSpan = std::min(span, std::max(minSpan, thumbMinW / trackW));

            if (sScrollDrag == ScrollPart::HandleL)
                leftNorm = sead::MathCalcCommon<f32>::clamp2(std::max(0.0f, rightNorm - maxSpan), pos, rightNorm - handleMinSpan);
            else
                rightNorm = sead::MathCalcCommon<f32>::clamp2(leftNorm + handleMinSpan, pos, std::min(1.0f, leftNorm + maxSpan));

            result = ZoomScrollResult::Zoomed;
        }

        thumbW = std::max((rightNorm - leftNorm) * trackW, thumbMinW);
        thumbTravel = trackW - thumbW;
        thumbX = (rightNorm - leftNorm < 1.0f) ? leftNorm / (1.0f - (rightNorm - leftNorm)) * thumbTravel : 0.0f;
    }

    const ScrollPart hotPart = scrollActive ? sScrollDrag : part;
    const bool fitsAll = rightNorm - leftNorm >= 0.999f;

    if (hotPart == ScrollPart::HandleL || hotPart == ScrollPart::HandleR)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    else if (hotPart == ScrollPart::Thumb && !fitsAll)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    const PianoRollPalette& palette = GetPianoRollPalette();
    const ImVec2 thumbMin(ImFloor(trackOrigin.x + thumbX), trackOrigin.y + 1.0f);
    const ImVec2 thumbMax(ImFloor(trackOrigin.x + thumbX + thumbW), trackOrigin.y + trackH - 1.0f);
    const ImGuiID scrollId = ImGui::GetItemID();
    const ImU32 restCol = fitsAll ? palette.hScrollThumbFits : palette.hScrollThumbOverflow;

    ImU32 thumbCol = ImGui::GetHoverColorU32(scrollId + 1, hotPart == ScrollPart::Thumb && !fitsAll, restCol, palette.vScrollThumbHover);

    if (scrollActive && sScrollDrag == ScrollPart::Thumb && !fitsAll)
        thumbCol = palette.hScrollThumbActive;

    ImU32 handleLCol = ImGui::GetHoverColorU32(scrollId + 2, hotPart == ScrollPart::HandleL, thumbCol, palette.vScrollThumbHover);
    ImU32 handleRCol = ImGui::GetHoverColorU32(scrollId + 3, hotPart == ScrollPart::HandleR, thumbCol, palette.vScrollThumbHover);

    if (scrollActive && sScrollDrag == ScrollPart::HandleL)
        handleLCol = palette.hScrollThumbActive;
    else if (scrollActive && sScrollDrag == ScrollPart::HandleR)
        handleRCol = palette.hScrollThumbActive;

    drawList->AddRectFilled(trackOrigin, ImVec2(trackOrigin.x + trackW, trackOrigin.y + trackH), palette.hScrollTrack, 3.0f);

    if (handleLCol == thumbCol && handleRCol == thumbCol)
    {
        drawList->AddRectFilled(thumbMin, thumbMax, thumbCol, 3.0f);
    }
    else
    {
        const f32 bodyX0 = std::min(thumbMin.x + handleW, thumbMax.x);
        const f32 bodyX1 = std::max(thumbMax.x - handleW, bodyX0);

        drawList->AddRectFilled(thumbMin, ImVec2(bodyX0, thumbMax.y), handleLCol, 3.0f, ImDrawFlags_RoundCornersLeft);
        drawList->AddRectFilled(ImVec2(bodyX0, thumbMin.y), ImVec2(bodyX1, thumbMax.y), thumbCol);
        drawList->AddRectFilled(ImVec2(bodyX1, thumbMin.y), thumbMax, handleRCol, 3.0f, ImDrawFlags_RoundCornersRight);
    }

    // Zoom handle grips
    const f32 gripX = ImFloor(handleW * 0.5f);
    for (s32 i = 0; i < 2; i++)
    {
        const f32 offset = gripX - 1.0f + i * 2.0f;
        drawList->AddRectFilled(ImVec2(thumbMin.x + offset, thumbMin.y + 3.0f), ImVec2(thumbMin.x + offset + 1.0f, thumbMax.y - 3.0f), palette.hScrollTrack);
        drawList->AddRectFilled(ImVec2(thumbMax.x - offset - 1.0f, thumbMin.y + 3.0f), ImVec2(thumbMax.x - offset, thumbMax.y - 3.0f), palette.hScrollTrack);
    }

    *left = leftNorm;
    *right = rightNorm;

    return result;
}
