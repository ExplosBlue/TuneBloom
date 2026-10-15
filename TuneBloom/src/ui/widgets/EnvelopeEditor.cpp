#include <ui/widgets/Knob.h>

#include <ui/Messages.h>
#include <ui/app/UI.h>
#include <ui/settings/UiColors.h>

#include <imgui/imgui_internal.h>

#include <snd/CurveAdshr.h>

#include <algorithm>

static constexpr s32 cAdshrCurveStageSamples = 24;
static constexpr f32 cAdshrCurveSustainWidth = 0.15f;
static constexpr f32 cAdshrCurveStageWidth   = 0.4f;
static constexpr f32 cAdshrCurveKneeMsec     = 5.0f;
static constexpr f32 cAdshrInstantOctaves    = 1.0f;

static constexpr uicolors::Themed<f32> cAdshrCurveFillAlpha = { 0.25f, 0.14f };

static constexpr u8 cAdsrMin = 0;
static constexpr u8 cAdsrMax = 127;

static const char* cRegionAdshrKnobs[] = { messages::bank::cKnobAttack, messages::common::cHold, messages::bank::cKnobDecay, messages::bank::cKnobSustain, messages::bank::cKnobRelease };

static f32 CalcAdshrLevel(f32 decibel, f32 floorDecibel)
{
    return sead::Mathf::clamp2(0.0f, 1.0f - decibel / floorDecibel, 1.0f);
}

static f32 CalcAdshrFallRate(u8 value)
{
    using snd::internal::CurveAdshr;

    CurveAdshr curve;
    curve.setRelease(value);
    curve.reset(0.0f);
    curve.setStatus(CurveAdshr::Status::Release);
    curve.update(1);

    return -curve.getValue();
}

static f32 CalcAdshrAttackRatio(u8 value)
{
    snd::internal::CurveAdshr curve;
    curve.setAttack(value);

    const f32 floorDecibel = curve.getValue();
    curve.update(1);

    return curve.getValue() / floorDecibel;
}

static f32 CalcAdshrFallScale(u8 value)
{
    if (value == cAdsrMax)
        return CalcAdshrFallScale(cAdsrMax - 1) - cAdshrInstantOctaves;

    return -log2f(CalcAdshrFallRate(value));
}

static f32 CalcAdshrAttackScale(u8 value)
{
    if (value == cAdsrMax)
        return CalcAdshrAttackScale(cAdsrMax - 1) - cAdshrInstantOctaves;

    return -log2f(-logf(CalcAdshrAttackRatio(value)));
}

struct AdshrTimes
{
    f32 floorDecibel = 0.0f;
    f32 attackRatio = 0.0f;
    s32 attackSteps = 0;
    f32 attackMsec = 0.0f;
    f32 holdMsec = 0.0f;
    f32 decayMsec = 0.0f;
    f32 sustainDecibel = 0.0f;
    f32 releaseMsec = 0.0f;
};

static AdshrTimes CalcAdshrTimes(const snd::AdshrCurve& adshrCurve)
{
    using snd::internal::CurveAdshr;

    AdshrTimes times;

    CurveAdshr curve;
    curve.setAttack(0);
    times.floorDecibel = curve.getValue();

    curve.setAttack(adshrCurve.attack);
    curve.setHold(adshrCurve.hold);
    curve.setDecay(adshrCurve.decay);
    curve.setSustain(adshrCurve.sustain);
    curve.setRelease(adshrCurve.release);

    // Attack
    while (curve.getStatus() == CurveAdshr::Status::Attack)
    {
        curve.update(1);
        times.attackSteps++;

        if (times.attackSteps == 1 && curve.getStatus() == CurveAdshr::Status::Attack)
            times.attackRatio = curve.getValue() / times.floorDecibel;
    }

    times.attackMsec = times.attackRatio > 0.0f ? static_cast<f32>(times.attackSteps) : 0.0f;

    // Hold
    s32 holdMsec = 0;
    while (curve.getStatus() == CurveAdshr::Status::Hold)
    {
        curve.update(1);
        holdMsec++;
    }

    times.holdMsec = adshrCurve.hold == 0 ? 0.0f : static_cast<f32>(holdMsec);

    // Decay
    f32 decayRate = 0.0f;
    {
        const f32 decibel = curve.getValue();
        curve.update(1);

        if (curve.getStatus() == CurveAdshr::Status::Decay)
            decayRate = decibel - curve.getValue();
    }

    curve.update(0x7FFFFFFF);
    times.sustainDecibel = curve.getValue();
    times.decayMsec = decayRate > 0.0f ? -times.sustainDecibel / decayRate : 0.0f;

    // Release
    times.releaseMsec = (times.sustainDecibel - times.floorDecibel) / CalcAdshrFallRate(adshrCurve.release);

    return times;
}

static sead::FixedSafeString<16> FormatAdshrTime(f32 msec)
{
    if (msec < 1000.0f)
        return sead::FormatFixedSafeString<16>(messages::common::cMillisecondsFormat, msec);

    return sead::FormatFixedSafeString<16>(messages::common::cSecondsFormat, msec / 1000.0f);
}

static f32 CalcAdshrStageRatio(f32 msec, f32 maxMsec)
{
    return log2f(1.0f + msec / cAdshrCurveKneeMsec) / log2f(1.0f + maxMsec / cAdshrCurveKneeMsec);
}

static void DrawAdshrCurve(const AdshrTimes& times, const ImVec2& size)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);

    ImGui::RenderFrame(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::GetColorU32(ImGuiCol_FrameBg), true, style.FrameRounding);

    const f32 left = pos.x + style.FramePadding.x;
    const f32 right = pos.x + size.x - style.FramePadding.x;
    const f32 top = pos.y + style.FramePadding.y;
    const f32 bottom = pos.y + size.y - style.FramePadding.y;

    const f32 maxMsec = -times.floorDecibel / CalcAdshrFallRate(cAdsrMin);
    const f32 attackRatio = CalcAdshrStageRatio(times.attackMsec, maxMsec);
    const f32 holdRatio = CalcAdshrStageRatio(times.holdMsec, maxMsec);
    const f32 decayRatio = CalcAdshrStageRatio(times.decayMsec, maxMsec);
    const f32 releaseRatio = CalcAdshrStageRatio(times.releaseMsec, maxMsec);

    const f32 sustainWidth = (right - left) * cAdshrCurveSustainWidth;
    const f32 stageWidth = std::min((right - left) * cAdshrCurveStageWidth, (right - left - sustainWidth) / std::max(attackRatio + holdRatio + decayRatio + releaseRatio, FLT_EPSILON));
    const f32 attackWidth = stageWidth * attackRatio;
    const f32 holdWidth = stageWidth * holdRatio;
    const f32 decayWidth = stageWidth * decayRatio;
    const f32 releaseWidth = stageWidth * releaseRatio;

    const f32 sustainY = bottom - (bottom - top) * CalcAdshrLevel(times.sustainDecibel, times.floorDecibel);

    ImVec2 points[cAdshrCurveStageSamples + 6];
    s32 pointNum = 0;
    f32 x = left;

    points[pointNum++] = ImVec2(x, bottom);

    for (s32 i = 1; i <= cAdshrCurveStageSamples; i++)
    {
        const f32 t = static_cast<f32>(i) / cAdshrCurveStageSamples;
        const f32 decibel = times.floorDecibel * powf(times.attackRatio, times.attackSteps * t);
        points[pointNum++] = ImVec2(x + attackWidth * t, bottom - (bottom - top) * CalcAdshrLevel(decibel, times.floorDecibel));
    }

    x += attackWidth + holdWidth;
    points[pointNum++] = ImVec2(x, top);

    x += decayWidth;
    points[pointNum++] = ImVec2(x, sustainY);

    x += sustainWidth;
    points[pointNum++] = ImVec2(x, sustainY);

    x += releaseWidth;
    points[pointNum++] = ImVec2(x, bottom);
    points[pointNum++] = ImVec2(right, bottom);

    ImDrawList* draw = ImGui::GetWindowDrawList();

    // Fill below the curve
    {
        ImVec4 fill = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
        fill.w *= uicolors::Pick(cAdshrCurveFillAlpha);
        const ImU32 fillColor = ImGui::GetColorU32(fill);

        const ImDrawListFlags flags = draw->Flags;
        draw->Flags &= ~ImDrawListFlags_AntiAliasedFill;

        for (s32 i = 0; i + 1 < pointNum; i++)
        {
            if (points[i + 1].x > points[i].x)
                draw->AddQuadFilled(points[i], points[i + 1], ImVec2(points[i + 1].x, bottom), ImVec2(points[i].x, bottom), fillColor);
        }

        draw->Flags = flags;
    }

    draw->AddPolyline(points, pointNum, ImGui::GetColorU32(ImGuiCol_PlotLines), ImDrawFlags_None, 1.5f);
}

f32 CalcAdshrEditorWidth(bool stacked)
{
    const f32 knobsWidth = CalcKnobRowWidth(cRegionAdshrKnobs, IM_ARRAYSIZE(cRegionAdshrKnobs));

    return stacked ? knobsWidth : knobsWidth + ImGui::GetFontSize() * (cRegionGapEm + cRegionCurveEm);
}

bool DrawAdshrEditor(snd::AdshrCurve& adshrCurve, f32 height, bool stacked)
{
    static const u8 cAdshrDefaults[] = { 127, 0, 127, 127, 127 };
    static const KnobScale cAdshrScales[] = { CalcAdshrAttackScale, nullptr, CalcAdshrFallScale, nullptr, CalcAdshrFallScale };

    u8* values[] = { &adshrCurve.attack, &adshrCurve.hold, &adshrCurve.decay, &adshrCurve.sustain, &adshrCurve.release };

    const f32 em = ImGui::GetFontSize();
    const f32 gap = em * cRegionGapEm;
    const f32 knobWidth = CalcKnobWidth(cRegionAdshrKnobs, IM_ARRAYSIZE(cRegionAdshrKnobs));
    const f32 knobsWidth = CalcKnobRowWidth(cRegionAdshrKnobs, IM_ARRAYSIZE(cRegionAdshrKnobs));
    const f32 knobGap = sead::Mathf::clamp2(0.0f, em * cRegionKnobGapEm - (knobsWidth - ImGui::GetContentRegionAvail().x) / (IM_ARRAYSIZE(cRegionAdshrKnobs) - 1), em * cRegionKnobGapEm);
    const f32 preferredCurveWidth = em * (IsRegionPanelFill() ? cRegionCurveMaxEm : cRegionCurveEm);
    const f32 freeWidth = ImGui::GetContentRegionAvail().x - knobsWidth - gap;
    const f32 curveWidth = ImFloor(std::min(freeWidth, preferredCurveWidth));

    const AdshrTimes times = CalcAdshrTimes(adshrCurve);
    const f32 stageMsec[] = { times.attackMsec, times.holdMsec, times.decayMsec, 0.0f, times.releaseMsec };
    const bool drawCurve = stacked || curveWidth >= em * cRegionCurveMinEm;

    if (drawCurve)
    {
        if (!stacked)
            CenterRegionKnobs(curveWidth + gap + knobsWidth);

        DrawAdshrCurve(times, stacked ? ImVec2(std::min(ImGui::GetContentRegionAvail().x, CalcAdshrEditorWidth(true)), height) : ImVec2(curveWidth, height));

        if (IsItemHoveredForDetail())
        {
            ImGui::SetTooltip(messages::bank::cEnvelopeCurveTooltipFormat,
                FormatAdshrTime(times.attackMsec).cstr(), FormatAdshrTime(times.holdMsec).cstr(), FormatAdshrTime(times.decayMsec).cstr(),
                times.sustainDecibel, FormatAdshrTime(times.releaseMsec).cstr());
        }

        if (!stacked)
            ImGui::SameLine(0.0f, gap);
    }
    else
    {
        CenterRegionKnobs(knobsWidth);
    }

    bool edited = false;

    for (s32 i = 0; i < IM_ARRAYSIZE(cRegionAdshrKnobs); i++)
    {
        if (i > 0)
            ImGui::SameLine(0.0f, knobGap);

        const bool isSustain = values[i] == &adshrCurve.sustain;
        const bool reversed = cAdshrScales[i] != nullptr;

        SetNextKnobReset(cAdshrDefaults[i]);
        SetNextKnobScale(cAdshrScales[i]);
        if (KnobU8(cRegionAdshrKnobs[i], values[i], reversed ? cAdsrMax : cAdsrMin, reversed ? cAdsrMin : cAdsrMax, ImVec2(knobWidth, height)))
            edited = true;

        if (isSustain)
            KnobTooltip(messages::bank::cEnvelopeLevelTooltipFormat, cRegionAdshrKnobs[i], times.sustainDecibel);
        else
            KnobTooltip(messages::bank::cEnvelopeTimeTooltipFormat, cRegionAdshrKnobs[i], FormatAdshrTime(stageMsec[i]).cstr());
    }

    return edited;
}
