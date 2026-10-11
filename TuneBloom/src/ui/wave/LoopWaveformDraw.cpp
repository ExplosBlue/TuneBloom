#include <ui/wave/LoopWaveformInternal.h>

#include <ui/settings/Theme.h>
#include <ui/widgets/TimeUtil.h>

#include <imgui/imgui_internal.h>

#include <math/seadMathCalcCommon.h>

#include <algorithm>
#include <cmath>

namespace ImGui
{

constexpr u32    cPeakBlockSize    = 256;

constexpr float  cWaveAmpScale     = 0.92f;
constexpr float  cTickTargetPx     = 100.0f;
constexpr float  cSeekPlayedAlpha  = 0.18f;
constexpr int    cSeekLoopTintA    = 26;
constexpr float  cCropGripSize     = 8.0f;
constexpr int    cCropDimAlpha     = 190;

constexpr float  cWaveDimMix          = 0.5f;
constexpr float  cEdgeMix             = 0.14f;
constexpr int    cEdgeSoftAlpha       = 150;
constexpr int    cIntroGlowAlphaDark  = 26;
constexpr int    cIntroGlowAlphaLight = 35;

constexpr double cTickStepsMs[] =
{
    0.1,   0.2,   0.5,
    1,     2,     5,
    10,    20,    50,
    100,   200,   500,
    1000,  2000,  5000,
    10000, 20000, 30000, 60000,
};

ImU32 WithAlpha(ImU32 color, int alpha)
{
    return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(alpha) << IM_COL32_A_SHIFT);
}

const WaveformPalette& Palette()
{
    // Marker colors
    static constexpr WaveformPalette cDark =
    {
        .loop      = IM_COL32(0xff, 0xb3, 0x47, 255),
        .loopGlow  = IM_COL32(0xff, 0xb3, 0x47, 40),
        .xfadeFill = IM_COL32(0x9b, 0x6d, 0xff, 36),
        .good      = IM_COL32(0x5f, 0xd3, 0x8a, 255),
        .crop      = IM_COL32(0xff, 0x6b, 0x8b, 255),
    };
    static constexpr WaveformPalette cLight =
    {
        .loop      = IM_COL32(0xd9, 0x7a, 0x0a, 255),
        .loopGlow  = IM_COL32(0xd9, 0x7a, 0x0a, 55),
        .xfadeFill = IM_COL32(0x7a, 0x4a, 0xc9, 45),
        .good      = IM_COL32(0x1f, 0x8f, 0x52, 255),
        .crop      = IM_COL32(0xc9, 0x2a, 0x4d, 255),
    };

    static WaveformPalette sPalette;
    static int sPaletteFrame = -1;

    if (sPaletteFrame == GetFrameCount())
        return sPalette;

    sPaletteFrame = GetFrameCount();
    sPalette = gThemeIsDark ? cDark : cLight;

    // Theme colors
    ImVec4 bg = GetStyleColorVec4(ImGuiCol_ChildBg);
    ImVec4 ink = GetStyleColorVec4(ImGuiCol_Text);
    ImVec4 lit = GetStyleColorVec4(ImGuiCol_CheckMark);
    ImVec4 faint = GetStyleColorVec4(ImGuiCol_TextDisabled);
    bg.w = ink.w = lit.w = faint.w = 1.0f;

    sPalette.bg        = ColorConvertFloat4ToU32(bg);
    sPalette.edge      = ColorConvertFloat4ToU32(ImLerp(bg, ink, cEdgeMix));
    sPalette.edgeSoft  = WithAlpha(sPalette.edge, cEdgeSoftAlpha);
    sPalette.inkFaint  = ColorConvertFloat4ToU32(faint);
    sPalette.waveLit   = ColorConvertFloat4ToU32(lit);
    sPalette.wave      = ColorConvertFloat4ToU32(ImLerp(bg, lit, cWaveDimMix));
    sPalette.introGlow = WithAlpha(sPalette.waveLit, gThemeIsDark ? cIntroGlowAlphaDark : cIntroGlowAlphaLight);

    return sPalette;
}

void LoopWaveformBuildPeaks(const std::vector<float>& mono, std::vector<float>* peakMin, std::vector<float>* peakMax)
{
    const u32 blockCount = static_cast<u32>(mono.size()) / cPeakBlockSize;
    const float* samples = mono.data();

    peakMin->resize(blockCount);
    peakMax->resize(blockCount);

    for (u32 block = 0; block < blockCount; block++, samples += cPeakBlockSize)
    {
        float minValue = samples[0];
        float maxValue = samples[0];

        for (u32 i = 1; i < cPeakBlockSize; i++)
        {
            minValue = std::min(minValue, samples[i]);
            maxValue = std::max(maxValue, samples[i]);
        }

        (*peakMin)[block] = minValue;
        (*peakMax)[block] = maxValue;
    }
}

void LoopWaveformSetPeaks(LoopWaveformState& state, const std::vector<float>& mono, std::vector<float>& peakMin, std::vector<float>& peakMax)
{
    state.peakMin.swap(peakMin);
    state.peakMax.swap(peakMax);
    state.peakSource = mono.data();
    state.peakSampleCount = static_cast<u32>(mono.size());
    state.peakRevision = state.dataRevision;
}

void UpdatePeakCache(LoopWaveformState& state, const std::vector<float>& mono)
{
    if (state.peakSource == mono.data() && state.peakSampleCount == mono.size() && state.peakRevision == state.dataRevision)
        return;

    std::vector<float> peakMin, peakMax;
    LoopWaveformBuildPeaks(mono, &peakMin, &peakMax);
    LoopWaveformSetPeaks(state, mono, peakMin, peakMax);
}

static void RangeMinMax(const LoopWaveformState& state, const std::vector<float>& mono, u32 begin, u32 end, float* minValue, float* maxValue)
{
    const float* samples = mono.data();
    u32 sample = begin;

    auto scanTo = [&](u32 limit)
    {
        for (; sample < limit; sample++)
        {
            const float value = samples[sample];

            if (value < *minValue)
                *minValue = value;

            if (value > *maxValue)
                *maxValue = value;
        }
    };

    if (end > begin && end - begin >= cPeakBlockSize * 2)
    {
        const u32 firstBlock = (begin + cPeakBlockSize - 1) / cPeakBlockSize;
        const u32 lastBlock = end / cPeakBlockSize;

        scanTo(firstBlock * cPeakBlockSize);

        for (u32 block = firstBlock; block < lastBlock; block++)
        {
            if (state.peakMin[block] < *minValue)
                *minValue = state.peakMin[block];

            if (state.peakMax[block] > *maxValue)
                *maxValue = state.peakMax[block];
        }

        sample = lastBlock * cPeakBlockSize;
    }

    scanTo(end);
}

static void DrawHandle(ImDrawList* draw, ImVec2 origin, float x, float height, bool isStart, bool focused)
{
    const float triangleSize = 6.0f;
    draw->AddLine(ImVec2(origin.x + x, origin.y), ImVec2(origin.x + x, origin.y + height), Palette().loop, focused ? 2.0f : 1.0f);

    if (isStart)
        draw->AddTriangleFilled(ImVec2(origin.x + x, origin.y), ImVec2(origin.x + x + triangleSize, origin.y),
                                ImVec2(origin.x + x, origin.y + triangleSize), Palette().loop);
    else
        draw->AddTriangleFilled(ImVec2(origin.x + x, origin.y), ImVec2(origin.x + x - triangleSize, origin.y),
                                ImVec2(origin.x + x, origin.y + triangleSize), Palette().loop);
}

static void DrawCropHandle(ImDrawList* draw, ImVec2 origin, float x, float height, bool isStart, bool active)
{
    const float bottom = origin.y + height;
    const float gripX = isStart ? cCropGripSize : -cCropGripSize;

    draw->AddLine(ImVec2(origin.x + x, origin.y), ImVec2(origin.x + x, bottom), Palette().crop, active ? 2.0f : 1.0f);
    draw->AddTriangleFilled(ImVec2(origin.x + x, bottom), ImVec2(origin.x + x + gripX, bottom),
                            ImVec2(origin.x + x, bottom - cCropGripSize), Palette().crop);
}

static void DrawCrop(ImDrawList* draw, ImVec2 origin, float width, float height, const LoopWaveformState& state)
{
    const float cropStartX = SampleToX(state, state.cropStart, width);
    const float cropEndX = SampleToX(state, state.cropEnd, width);
    const float clampedStartX = sead::Mathf::clamp2(0.0f, cropStartX, width);
    const float clampedEndX = sead::Mathf::clamp2(0.0f, cropEndX, width);
    const ImU32 dim = WithAlpha(Palette().bg, cCropDimAlpha);

    if (clampedStartX > 0.0f)
        draw->AddRectFilled(origin, ImVec2(origin.x + clampedStartX, origin.y + height), dim);

    if (clampedEndX < width)
        draw->AddRectFilled(ImVec2(origin.x + clampedEndX, origin.y), ImVec2(origin.x + width, origin.y + height), dim);

    if (cropStartX >= 0.0f && cropStartX <= width)
        DrawCropHandle(draw, origin, cropStartX, height, true, state.dragging == LoopWaveformState::Dragging::CropStart);

    if (cropEndX >= 0.0f && cropEndX <= width)
        DrawCropHandle(draw, origin, cropEndX, height, false, state.dragging == LoopWaveformState::Dragging::CropEnd);
}

struct WaveformDraw
{
    ImDrawList* draw = nullptr;
    ImVec2 origin;
    float width = 0.0f;
    float height = 0.0f;
    float midY = 0.0f;
    float samplesPerPixel = 0.0f;
    u32 loopStart = 0;
    u32 loopEnd = 0;
    bool showLoop = false;
    bool showCurve = false;
};

static float ValueToY(const WaveformDraw& ctx, float value)
{
    return ctx.midY - sead::Mathf::clamp2(-1.0f, value, 1.0f) * (ctx.height * 0.5f) * cWaveAmpScale;
}

static float FadedSample(const WaveformDraw& ctx, const LoopWaveformState& state, const std::vector<float>& mono, u32 sample)
{
    const float value = mono[sample] * state.displayGain;

    return ctx.showCurve ? value * CurveGainAt(state, static_cast<float>(sample)) : value;
}

static void DrawLoopRegion(const WaveformDraw& ctx, const LoopWaveformState& state, u32 sampleRate, float loopStartX, float loopEndX)
{
    ImDrawList* draw = ctx.draw;
    const ImVec2 origin = ctx.origin;
    const float width = ctx.width;
    const float height = ctx.height;
    const u32 loopStart = ctx.loopStart;
    const u32 loopEnd = ctx.loopEnd;

    const float clampedStartX = sead::Mathf::clamp2(0.0f, loopStartX, width);
    const float clampedEndX = sead::Mathf::clamp2(0.0f, loopEndX, width);

    draw->AddRectFilled(origin, ImVec2(origin.x + clampedStartX, origin.y + height), Palette().introGlow);
    draw->AddRectFilled(ImVec2(origin.x + clampedStartX, origin.y), ImVec2(origin.x + clampedEndX, origin.y + height), Palette().loopGlow);

    const s32 loopLengthMinus1 = (loopEnd > loopStart) ? static_cast<s32>(loopEnd - loopStart - 1) : -1;
    const u32 leadStart = state.showCrop ? std::min(state.cropStart, loopStart) : 0u;
    s32 crossfadeSamples = static_cast<s32>(state.crossfadeMs / 1000.0f * sampleRate);
    crossfadeSamples = std::min({crossfadeSamples, static_cast<s32>(loopStart - leadStart), loopLengthMinus1});

    if (crossfadeSamples > 0)
    {
        const float crossfadeWidth = static_cast<float>(crossfadeSamples) / ViewSpan(state) * width;

        draw->AddRectFilled(ImVec2(origin.x + std::max(0.0f, loopStartX), origin.y), ImVec2(origin.x + std::max(0.0f, loopStartX) + crossfadeWidth, origin.y + height), Palette().xfadeFill);
        draw->AddRectFilled(ImVec2(origin.x + std::max(0.0f, loopEndX - crossfadeWidth), origin.y), ImVec2(origin.x + std::max(0.0f, loopEndX), origin.y + height), Palette().xfadeFill);
    }
}

static void DrawWaveSamples(const WaveformDraw& ctx, const LoopWaveformState& state, const std::vector<float>& mono)
{
    ImDrawList* draw = ctx.draw;
    const ImVec2 origin = ctx.origin;
    const float width = ctx.width;
    const u32 sampleCount = static_cast<u32>(mono.size());
    const u32 firstSample = state.viewStart;
    const u32 lastSample = std::min(sampleCount > 0 ? sampleCount - 1 : 0, state.viewEnd);

    for (u32 sample = firstSample; sample < lastSample; sample++)
    {
        const bool inLoop = ctx.showLoop && (sample >= ctx.loopStart && sample < ctx.loopEnd);
        const ImU32 color = inLoop ? Palette().waveLit : Palette().wave;
        const float x0 = SampleToX(state, sample, width), x1 = SampleToX(state, sample + 1, width);

        draw->AddLine(ImVec2(origin.x + x0, ValueToY(ctx, FadedSample(ctx, state, mono, sample))), ImVec2(origin.x + x1, ValueToY(ctx, FadedSample(ctx, state, mono, sample + 1))), color, 1.4f);
    }

    if (ctx.samplesPerPixel < 0.3f)
    {
        for (u32 sample = firstSample; sample <= lastSample; sample++)
        {
            const bool inLoop = ctx.showLoop && (sample >= ctx.loopStart && sample < ctx.loopEnd);
            const ImU32 color = inLoop ? Palette().waveLit : Palette().wave;
            const float x = SampleToX(state, sample, width);

            draw->AddCircleFilled(ImVec2(origin.x + x, ValueToY(ctx, FadedSample(ctx, state, mono, sample))), 1.7f, color);
        }
    }
}

static void DrawWavePeaks(const WaveformDraw& ctx, const LoopWaveformState& state, const std::vector<float>& mono)
{
    ImDrawList* draw = ctx.draw;
    const ImVec2 origin = ctx.origin;
    const float samplesPerPixel = ctx.samplesPerPixel;
    const u32 sampleCount = static_cast<u32>(mono.size());

    float prevX = 0.0f, prevMax = 0.0f, prevMin = 0.0f;
    bool havePrev = false;

    for (int xi = 0; xi < static_cast<int>(ctx.width); xi++)
    {
        const float sampleAtX = static_cast<float>(state.viewStart) + xi * samplesPerPixel;
        const u32 columnStart = static_cast<u32>(std::max(0.0f, std::floor(sampleAtX)));
        const u32 columnEnd = std::max(columnStart + 1, static_cast<u32>(std::floor(static_cast<float>(state.viewStart) + (xi + 1) * samplesPerPixel)));
        float minValue = 1.0f, maxValue = -1.0f;

        RangeMinMax(state, mono, columnStart, std::min(columnEnd, sampleCount), &minValue, &maxValue);

        if (maxValue < minValue)
            continue;

        const float gain = ctx.showCurve ? state.displayGain * CurveGainAt(state, sampleAtX + samplesPerPixel * 0.5f) : state.displayGain;

        minValue *= gain;
        maxValue *= gain;

        const bool inLoop = ctx.showLoop && (sampleAtX >= ctx.loopStart && sampleAtX <= ctx.loopEnd);
        const ImU32 color = inLoop ? Palette().waveLit : Palette().wave;
        const float x = xi + 0.5f;

        if (havePrev)
        {
            const float connectFromTop = std::fabs(prevMax - maxValue);
            const float connectFromBottom = std::fabs(prevMin - minValue);

            if (connectFromTop <= connectFromBottom)
                draw->AddLine(ImVec2(origin.x + prevX, ValueToY(ctx, prevMax)), ImVec2(origin.x + x, ValueToY(ctx, maxValue)), color, 1.0f);
            else
                draw->AddLine(ImVec2(origin.x + prevX, ValueToY(ctx, prevMin)), ImVec2(origin.x + x, ValueToY(ctx, minValue)), color, 1.0f);
        }

        draw->AddLine(ImVec2(origin.x + x, ValueToY(ctx, maxValue)), ImVec2(origin.x + x, ValueToY(ctx, minValue)), color, 1.0f);

        prevX = x;
        prevMax = maxValue;
        prevMin = minValue;
        havePrev = true;
    }
}

static void DrawLoopHandles(const WaveformDraw& ctx, const LoopWaveformState& state, float loopStartX, float loopEndX)
{
    const float samplesPerPixel = ctx.samplesPerPixel;
    const u32 loopStart = ctx.loopStart;
    const u32 loopEnd = ctx.loopEnd;

    if (static_cast<float>(loopStart) + samplesPerPixel >= state.viewStart && static_cast<float>(loopStart) <= state.viewEnd + samplesPerPixel)
        DrawHandle(ctx.draw, ctx.origin, loopStartX, ctx.height, true, state.focus == LoopWaveformState::Focus::Start);

    if (static_cast<float>(loopEnd) + samplesPerPixel >= state.viewStart && static_cast<float>(loopEnd) <= state.viewEnd + samplesPerPixel)
        DrawHandle(ctx.draw, ctx.origin, loopEndX, ctx.height, false, state.focus == LoopWaveformState::Focus::End);
}

static void DrawPlayhead(const WaveformDraw& ctx, const LoopWaveformState& state, float playheadSample)
{
    ImDrawList* draw = ctx.draw;
    const ImVec2 origin = ctx.origin;
    const float height = ctx.height;
    const float samplesPerPixel = ctx.samplesPerPixel;

    if (playheadSample >= 0.0f && playheadSample + samplesPerPixel >= state.viewStart && playheadSample <= state.viewEnd + samplesPerPixel)
    {
        const float playheadX = SampleToX(state, static_cast<u32>(playheadSample), ctx.width);

        draw->AddLine(ImVec2(origin.x + playheadX, origin.y), ImVec2(origin.x + playheadX, origin.y + height), Palette().good, 1.5f);
        draw->AddTriangleFilled(ImVec2(origin.x + playheadX - 4, origin.y + height), ImVec2(origin.x + playheadX + 4, origin.y + height), ImVec2(origin.x + playheadX, origin.y + height - 6), Palette().good);
    }
}

void DrawWaveform(ImDrawList* draw, ImVec2 origin, float width, float height,
                  LoopWaveformState& state, const std::vector<float>& mono, u32 sampleRate,
                  u32 loopStart, u32 loopEnd, float playheadSample, bool showLoop)
{
    WaveformDraw ctx;
    ctx.draw = draw;
    ctx.origin = origin;
    ctx.width = width;
    ctx.height = height;
    ctx.midY = origin.y + height * 0.5f;
    ctx.samplesPerPixel = static_cast<float>(ViewSpan(state)) / width;
    ctx.loopStart = loopStart;
    ctx.loopEnd = loopEnd;
    ctx.showLoop = showLoop;
    ctx.showCurve = state.showCurve && !AudioProcessing::isCurveFlat(state.curve);

    draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), Palette().bg);

    const float loopStartX = SampleToX(state, loopStart, width);
    const float loopEndX = SampleToX(state, loopEnd, width);

    if (showLoop)
        DrawLoopRegion(ctx, state, sampleRate, loopStartX, loopEndX);

    if (ctx.samplesPerPixel <= 1.0f)
        DrawWaveSamples(ctx, state, mono);
    else
        DrawWavePeaks(ctx, state, mono);

    if (state.showCrop)
        DrawCrop(draw, origin, width, height, state);

    if (state.showCurve)
        DrawCurve(draw, origin, width, height, state);

    if (showLoop)
        DrawLoopHandles(ctx, state, loopStartX, loopEndX);

    DrawPlayhead(ctx, state, playheadSample);
}

static void FormatTickLabel(char* buffer, size_t bufferSize, double timeMs, double majorStepMs)
{
    timeutil::FormatTimeLabel(buffer, bufferSize, timeMs / 1000.0, majorStepMs / 1000.0);
}

struct RulerDraw
{
    ImDrawList* draw = nullptr;
    float left = 0.0f;
    float top = 0.0f;
    float bottom = 0.0f;
    float width = 0.0f;
};

static double FindMajorTickStep(double roughStepMs)
{
    for (double stepMs : cTickStepsMs)
    {
        if (stepMs >= roughStepMs)
            return stepMs;
    }

    return cTickStepsMs[IM_ARRAYSIZE(cTickStepsMs) - 1];
}

static void DrawRulerTicks(const RulerDraw& ruler, const LoopWaveformState& state, u32 sampleRate)
{
    const WaveformPalette& palette = Palette();
    ImDrawList* draw = ruler.draw;
    const float left = ruler.left;
    const float top = ruler.top;
    const float bottom = ruler.bottom;
    const float width = ruler.width;

    const float majorTickLen = 13.0f;
    const float minorTickLen = 6.0f;

    const double startMs = timeutil::SamplesToSeconds(state.viewStart, sampleRate) * 1000.0;
    const double endMs = timeutil::SamplesToSeconds(state.viewEnd, sampleRate) * 1000.0;
    const double spanMs = endMs - startMs;

    if (spanMs <= 0.0)
        return;

    const double targetDivisions = std::max(4.0, static_cast<double>(width) / cTickTargetPx);
    const double majorStepMs = FindMajorTickStep(spanMs / targetDivisions);
    const double majorStepPx = majorStepMs / spanMs * width;
    int subdivisions = 1;

    if (majorStepPx / 5.0 >= 7.0)
        subdivisions = 5;
    else if (majorStepPx / 4.0 >= 7.0)
        subdivisions = 4;
    else if (majorStepPx / 2.0 >= 6.0)
        subdivisions = 2;

    const double minorStepMs = majorStepMs / subdivisions;
    char label[32];

    for (s64 i = static_cast<s64>(std::ceil(startMs / minorStepMs)); i * minorStepMs <= endMs; i++)
    {
        const double timeMs = i * minorStepMs;
        const float tickX = SampleToX(state, static_cast<u32>(timeMs / 1000.0 * sampleRate + 0.5), width);

        if (tickX < 0.0f || tickX > width)
            continue;

        if (i % subdivisions == 0)
        {
            draw->AddLine(ImVec2(left + tickX, bottom - majorTickLen), ImVec2(left + tickX, bottom), palette.inkFaint);
            FormatTickLabel(label, sizeof(label), timeMs, majorStepMs);
            draw->AddText(ImVec2(left + tickX + 3, top + 2), palette.inkFaint, label);
        }
        else
        {
            draw->AddLine(ImVec2(left + tickX, bottom - minorTickLen), ImVec2(left + tickX, bottom), palette.edgeSoft);
        }
    }
}

static void DrawRulerLoopEdge(const RulerDraw& ruler, float x, float footLen)
{
    const ImU32 color = Palette().loop;
    const float left = ruler.left;
    const float top = ruler.top;
    const float bottom = ruler.bottom;

    if (x < 0.0f || x > ruler.width)
        return;

    ruler.draw->AddLine(ImVec2(left + x, top), ImVec2(left + x, bottom), color, 2.0f);
    ruler.draw->AddLine(ImVec2(left + x, top + 1), ImVec2(left + x + footLen, top + 1), color, 2.0f);
    ruler.draw->AddLine(ImVec2(left + x, bottom - 1), ImVec2(left + x + footLen, bottom - 1), color, 2.0f);
}

static void DrawRulerPlayhead(const RulerDraw& ruler, const LoopWaveformState& state, float playheadSample)
{
    const ImU32 color = Palette().good;
    const float left = ruler.left;
    const float top = ruler.top;
    const float playheadX = SampleToX(state, static_cast<u32>(playheadSample), ruler.width);

    if (playheadX < 0.0f || playheadX > ruler.width)
        return;

    ruler.draw->AddLine(ImVec2(left + playheadX, top), ImVec2(left + playheadX, ruler.bottom), color, 2.0f);
    ruler.draw->AddTriangleFilled(ImVec2(left + playheadX - 5, top), ImVec2(left + playheadX + 5, top), ImVec2(left + playheadX, top + 6), color);
}

bool DrawSeekRuler(const char* idStr, ImVec2 origin, float width, float height,
                   const LoopWaveformState& state, u32 sampleRate,
                   float playheadSample, bool showLoop, u32 loopStart, u32 loopEnd,
                   double* outSeekSample)
{
    const WaveformPalette& palette = Palette();
    ImDrawList* draw = GetWindowDrawList();

    const float left = origin.x;
    const float top = origin.y;
    const float bottom = origin.y + height;
    const float span = static_cast<float>(ViewSpan(state));
    const bool playing = playheadSample >= 0.0f;
    const RulerDraw ruler{ draw, left, top, bottom, width };

    const float loopFootLen = 5.0f;

    draw->AddRectFilled(origin, ImVec2(left + width, bottom), palette.bg, 3.0f);
    draw->AddLine(origin, ImVec2(left + width, top), palette.edge);

    if (playing)
    {
        const float playheadX = sead::Mathf::clamp2(0.0f, SampleToX(state, static_cast<u32>(playheadSample), width), width);
        const ImU32 playedColor = ColorConvertFloat4ToU32(ImVec4(gAccentColor.x, gAccentColor.y, gAccentColor.z, cSeekPlayedAlpha));

        draw->AddRectFilled(ImVec2(left, top), ImVec2(left + playheadX, bottom), playedColor, 3.0f);
    }

    if (showLoop)
    {
        const float clampedLoopStartX = sead::Mathf::clamp2(0.0f, SampleToX(state, loopStart, width), width);
        const float clampedLoopEndX = sead::Mathf::clamp2(0.0f, SampleToX(state, loopEnd, width), width);

        if (clampedLoopEndX > clampedLoopStartX)
            draw->AddRectFilled(ImVec2(left + clampedLoopStartX, top), ImVec2(left + clampedLoopEndX, bottom), WithAlpha(palette.loop, cSeekLoopTintA));
    }

    if (sampleRate > 0)
        DrawRulerTicks(ruler, state, sampleRate);

    if (showLoop)
    {
        DrawRulerLoopEdge(ruler, SampleToX(state, loopStart, width), loopFootLen);
        DrawRulerLoopEdge(ruler, SampleToX(state, loopEnd, width), -loopFootLen);
    }

    if (playing)
        DrawRulerPlayhead(ruler, state, playheadSample);

    InvisibleButton(idStr, ImVec2(width, height));

    if (IsItemHovered())
        SetMouseCursor(ImGuiMouseCursor_Hand);

    if (IsItemActivated())
    {
        const float fraction = sead::Mathf::clamp2(0.0f, (GetIO().MousePos.x - left) / width, 1.0f);

        if (outSeekSample)
            *outSeekSample = static_cast<double>(state.viewStart) + static_cast<double>(fraction) * span;

        return true;
    }

    return false;
}

}
