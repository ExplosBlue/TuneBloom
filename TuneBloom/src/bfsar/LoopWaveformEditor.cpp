#include <bfsar/LoopWaveformEditor.h>
#include <math/seadMathCalcCommon.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ImGui {

constexpr double kPi = 3.14159265358979323846;

using s64 = int64_t;
using s32 = int32_t;

static constexpr ImU32 kColWave      = IM_COL32(0x3d, 0x5a, 0x73, 255);
static constexpr ImU32 kColWaveLit   = IM_COL32(0x7f, 0xd1, 0xff, 255);
static constexpr ImU32 kColLoop      = IM_COL32(0xff, 0xb3, 0x47, 255);
static constexpr ImU32 kColLoopGlow  = IM_COL32(0xff, 0xb3, 0x47, 40);
static constexpr ImU32 kColIntroGlow = IM_COL32(0x4a, 0x9f, 0xd4, 26);
static constexpr ImU32 kColXfadeFill = IM_COL32(0x9b, 0x6d, 0xff, 36);
static constexpr ImU32 kColGood      = IM_COL32(0x5f, 0xd3, 0x8a, 255);
static constexpr ImU32 kColEdge      = IM_COL32(0x2a, 0x33, 0x40, 255);
static constexpr ImU32 kColEdgeSoft  = IM_COL32(0x2a, 0x33, 0x40, 153);
static constexpr ImU32 kColInkFaint  = IM_COL32(0x5a, 0x66, 0x75, 255);
static constexpr ImU32 kColBg        = IM_COL32(0x16, 0x1b, 0x22, 255);
static constexpr ImU32 kColSeamBg    = IM_COL32(0x0c, 0x10, 0x14, 255);

static u32 ViewSpan(const LoopWaveformState& st)
{
    return st.viewEnd > st.viewStart ? (st.viewEnd - st.viewStart) : 1u;
}

static float S2X(const LoopWaveformState& st, u32 sample, float canvasWidth)
{
    const float span = static_cast<float>(ViewSpan(st));
    return (static_cast<float>(sample) - static_cast<float>(st.viewStart)) / span * canvasWidth;
}

static double X2S(const LoopWaveformState& st, float x, float canvasWidth)
{
    const double span = static_cast<double>(ViewSpan(st));
    return static_cast<double>(st.viewStart) + (static_cast<double>(x) / canvasWidth) * span;
}

static void ClampView(LoopWaveformState& st, u32 totalSamples)
{
    s64 viewStart = static_cast<s64>(st.viewStart);
    s64 viewEnd = static_cast<s64>(st.viewEnd);
    const s64 total = static_cast<s64>(totalSamples);

    if (viewStart < 0) { viewEnd -= viewStart; viewStart = 0; }
    if (viewEnd > total) { viewStart -= (viewEnd - total); viewEnd = total; }
    if (viewStart < 0) viewStart = 0;
    if (viewEnd - viewStart < 16) viewEnd = std::min(total, viewStart + 16);

    st.viewStart = static_cast<u32>(std::max<s64>(0, viewStart));
    st.viewEnd = static_cast<u32>(std::max<s64>(static_cast<s64>(st.viewStart), viewEnd));
}

static void ClampRangeD(double& vs, double& ve, u32 totalSamples)
{
    const double total = static_cast<double>(totalSamples);
    if (vs < 0.0) { ve -= vs; vs = 0.0; }
    if (ve > total) { vs -= (ve - total); ve = total; }
    if (vs < 0.0) vs = 0.0;
    if (ve - vs < 16.0) ve = std::min(total, vs + 16.0);
    if (ve < vs) ve = vs;
}

static void SnapView(LoopWaveformState& st)
{
    st.viewStartTarget = st.viewStartAnim = static_cast<double>(st.viewStart);
    st.viewEndTarget = st.viewEndAnim = static_cast<double>(st.viewEnd);
}

static void ResetView(LoopWaveformState& st, u32 totalSamples)
{
    st.viewStartTarget = 0.0;
    st.viewEndTarget = static_cast<double>(totalSamples);
    st.viewInitialized = true;
}

static void ZoomAt(LoopWaveformState& st, double sampleCenter, double factor, u32 totalSamples)
{
    const double curStart = st.viewStartTarget;
    const double curSpan = std::max(1.0, st.viewEndTarget - st.viewStartTarget);
    const double newSpan = std::max(16.0, std::min(static_cast<double>(totalSamples), curSpan * factor));
    const double frac = (sampleCenter - curStart) / curSpan;

    double ns = sampleCenter - frac * newSpan;
    double ne = ns + newSpan;
    ClampRangeD(ns, ne, totalSamples);
    st.viewStartTarget = ns;
    st.viewEndTarget = ne;
}

static void ZoomToLoop(LoopWaveformState& st, u32 loopStart, u32 loopEnd, u32 totalSamples)
{
    const u32 len = loopEnd > loopStart ? (loopEnd - loopStart) : 0;
    const u32 pad = std::max<u32>(64, static_cast<u32>(std::lround(len * 0.3)));

    double vs = static_cast<double>(loopStart) - static_cast<double>(pad);
    double ve = static_cast<double>(loopEnd) + static_cast<double>(pad);
    ClampRangeD(vs, ve, totalSamples);
    st.viewStartTarget = vs;
    st.viewEndTarget = ve;
}

static inline u32 ClampIdx(s64 i, u32 n)
{
    if (n == 0) return 0;
    if (i < 0) return 0;
    if (i >= static_cast<s64>(n)) return n - 1;
    return static_cast<u32>(i);
}

static void DrawTimeRuler(ImDrawList* draw, ImVec2 origin, float W,
                           const LoopWaveformState& st, u32 sampleRate)
{
    if (sampleRate == 0) return;
    const double startMs = static_cast<double>(st.viewStart) / sampleRate * 1000.0;
    const double endMs = static_cast<double>(st.viewEnd) / sampleRate * 1000.0;
    const double spanMs = endMs - startMs;
    if (spanMs <= 0.0) return;

    const double rough = spanMs / 8.0;
    static const double nice[] = { 0.1, 0.2, 0.5, 1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000 };
    double stepMs = nice[13];
    for (double n : nice) { if (n >= rough) { stepMs = n; break; } }

    const double first = std::ceil(startMs / stepMs) * stepMs;
    char label[32];

    for (double t = first; t <= endMs; t += stepMs)
    {
        const double sample = t / 1000.0 * sampleRate;
        if (sample < 0) continue;
        const float x = S2X(st, static_cast<u32>(sample), W);
        if (x < 0 || x > W) continue;

        draw->AddLine(ImVec2(origin.x + x, origin.y), ImVec2(origin.x + x, origin.y + 8), kColEdgeSoft);

        if (stepMs < 1.0) snprintf(label, sizeof(label), "%.1fms", t);
        else if (t >= 1000.0) snprintf(label, sizeof(label), "%.*fs", std::fmod(t, 1000.0) != 0 ? 2 : 0, t / 1000.0);
        else snprintf(label, sizeof(label), "%.0fms", t);

        draw->AddText(ImVec2(origin.x + x + 3, origin.y + 1), kColInkFaint, label);
    }
}

static void DrawHandle(ImDrawList* draw, ImVec2 origin, float x, float H, bool isStart, bool focused)
{
    draw->AddLine(ImVec2(origin.x + x, origin.y), ImVec2(origin.x + x, origin.y + H), kColLoop, focused ? 2.0f : 1.0f);
    const float tri = 6.0f;
    if (isStart)
        draw->AddTriangleFilled(ImVec2(origin.x + x, origin.y), ImVec2(origin.x + x + tri, origin.y),
                                 ImVec2(origin.x + x, origin.y + tri), kColLoop);
    else
        draw->AddTriangleFilled(ImVec2(origin.x + x, origin.y), ImVec2(origin.x + x - tri, origin.y),
                                 ImVec2(origin.x + x, origin.y + tri), kColLoop);
}

static void DrawWaveform(ImDrawList* draw, ImVec2 origin, float W, float H,
                          LoopWaveformState& st, const std::vector<float>& mono, u32 sampleRate,
                          u32 loopStart, u32 loopEnd, float playheadSample, bool showLoop)
{
    const u32 n = static_cast<u32>(mono.size());
    const float mid = origin.y + H * 0.5f;
    draw->AddRectFilled(origin, ImVec2(origin.x + W, origin.y + H), kColBg);

    const float xS = S2X(st, loopStart, W);
    const float xE = S2X(st, loopEnd, W);
    if (showLoop)
    {
        const float cS = sead::Mathf::clamp2(0.0f, xS, W);
        const float cE = sead::Mathf::clamp2(0.0f, xE, W);
        draw->AddRectFilled(origin, ImVec2(origin.x + cS, origin.y + H), kColIntroGlow);
        draw->AddRectFilled(ImVec2(origin.x + cS, origin.y), ImVec2(origin.x + cE, origin.y + H), kColLoopGlow);

        const s32 loopLenMinus1 = (loopEnd > loopStart) ? static_cast<s32>(loopEnd - loopStart - 1) : -1;
        s32 xfSamp = static_cast<s32>(st.crossfadeMs / 1000.0f * sampleRate);
        xfSamp = std::min({ xfSamp, static_cast<s32>(loopStart), loopLenMinus1 });
        if (xfSamp > 0)
        {
            const float xfPx = static_cast<float>(xfSamp) / ViewSpan(st) * W;
            draw->AddRectFilled(ImVec2(origin.x + std::max(0.0f, xS), origin.y), ImVec2(origin.x + std::max(0.0f, xS) + xfPx, origin.y + H), kColXfadeFill);
            draw->AddRectFilled(ImVec2(origin.x + std::max(0.0f, xE - xfPx), origin.y), ImVec2(origin.x + std::max(0.0f, xE), origin.y + H), kColXfadeFill);
        }
    }

    auto yOf = [mid, H](float v) { return mid - v * (H * 0.5f) * 0.92f; };

    const float spp = static_cast<float>(ViewSpan(st)) / W;
    if (spp <= 1.0f)
    {
        const u32 s0 = st.viewStart;
        const u32 s1 = std::min(n > 0 ? n - 1 : 0, st.viewEnd);
        for (u32 s = s0; s < s1; s++)
        {
            const bool inLoop = showLoop && (s >= loopStart && s < loopEnd);
            const ImU32 col = inLoop ? kColWaveLit : kColWave;
            const float x0 = S2X(st, s, W), x1 = S2X(st, s + 1, W);
            draw->AddLine(ImVec2(origin.x + x0, yOf(mono[s])), ImVec2(origin.x + x1, yOf(mono[s + 1])), col, 1.4f);
        }
        if (spp < 0.3f)
        {
            for (u32 s = s0; s <= s1; s++)
            {
                const bool inLoop = showLoop && (s >= loopStart && s < loopEnd);
                const ImU32 col = inLoop ? kColWaveLit : kColWave;
                const float x = S2X(st, s, W);
                draw->AddCircleFilled(ImVec2(origin.x + x, yOf(mono[s])), 1.7f, col);
            }
        }
    }
    else
    {
        float prevX = 0.0f, prevHi = 0.0f, prevLo = 0.0f;
        bool havePrev = false;
        for (int xi = 0; xi < static_cast<int>(W); xi++)
        {
            const float sampleAtXf = static_cast<float>(st.viewStart) + xi * spp;
            const u32 a = static_cast<u32>(std::max(0.0f, std::floor(sampleAtXf)));
            const u32 b = std::max(a + 1, static_cast<u32>(std::floor(static_cast<float>(st.viewStart) + (xi + 1) * spp)));
            float lo = 1.0f, hi = -1.0f;
            for (u32 i = a; i < b && i < n; i++) { const float v = mono[i]; if (v < lo) lo = v; if (v > hi) hi = v; }
            if (hi < lo) continue;

            const bool inLoop = showLoop && (sampleAtXf >= loopStart && sampleAtXf <= loopEnd);
            const ImU32 col = inLoop ? kColWaveLit : kColWave;
            const float x = xi + 0.5f;

            if (havePrev)
            {
                const float connectFromTop = std::fabs(prevHi - hi);
                const float connectFromBot = std::fabs(prevLo - lo);

                if (connectFromTop <= connectFromBot)
                    draw->AddLine(ImVec2(origin.x + prevX, yOf(prevHi)), ImVec2(origin.x + x, yOf(hi)), col, 1.0f);
                else
                    draw->AddLine(ImVec2(origin.x + prevX, yOf(prevLo)), ImVec2(origin.x + x, yOf(lo)), col, 1.0f);
            }

            draw->AddLine(ImVec2(origin.x + x, yOf(hi)), ImVec2(origin.x + x, yOf(lo)), col, 1.0f);

            prevX = x; prevHi = hi; prevLo = lo; havePrev = true;
        }
    }

    const float spp2 = spp;
    if (showLoop)
    {
        if (static_cast<float>(loopStart) + spp2 >= st.viewStart && static_cast<float>(loopStart) <= st.viewEnd + spp2)
            DrawHandle(draw, origin, xS, H, true, st.focus == LoopWaveformState::Focus::Start);
        
        if (static_cast<float>(loopEnd) + spp2 >= st.viewStart && static_cast<float>(loopEnd) <= st.viewEnd + spp2)
            DrawHandle(draw, origin, xE, H, false, st.focus == LoopWaveformState::Focus::End);
    }

    if (playheadSample >= 0.0f && playheadSample + spp2 >= st.viewStart && playheadSample <= st.viewEnd + spp2)
    {
        const float px = S2X(st, static_cast<u32>(playheadSample), W);
        draw->AddLine(ImVec2(origin.x + px, origin.y), ImVec2(origin.x + px, origin.y + H), kColGood, 1.5f);
        draw->AddTriangleFilled(ImVec2(origin.x + px - 4, origin.y + H), ImVec2(origin.x + px + 4, origin.y + H), ImVec2(origin.x + px, origin.y + H - 6), kColGood);
    }

    DrawTimeRuler(draw, origin, W, st, sampleRate);
}

static bool HandleInteraction(LoopWaveformState& st, const std::vector<float>& mono,
                               float canvasWidth, ImVec2 canvasOrigin,
                               u32& loopStart, u32& loopEnd, bool editable)
{
    bool changed = false;
    const u32 n = static_cast<u32>(mono.size());
    if (n == 0) return false;

    ImGuiIO& io = GetIO();

    const bool hovered = IsItemHovered();
    const float mouseXRel = sead::Mathf::clamp2(0.0f, io.MousePos.x - canvasOrigin.x, canvasWidth);

    if (hovered && st.dragging == LoopWaveformState::Dragging::None)
    {
        if (editable && IsMouseClicked(ImGuiMouseButton_Left))
        {
            st.dragging = LoopWaveformState::Dragging::Start;
            st.focus = LoopWaveformState::Focus::Start;
        }
        else if (editable && IsMouseClicked(ImGuiMouseButton_Right))
        {
            st.dragging = LoopWaveformState::Dragging::End;
            st.focus = LoopWaveformState::Focus::End;
        }
        else if (IsMouseClicked(ImGuiMouseButton_Middle))
        {
            st.dragging = LoopWaveformState::Dragging::Pan;
            st.dragAnchorMouseX = io.MousePos.x;
            st.dragAnchorViewStart = st.viewStart;
            st.dragAnchorViewEnd = st.viewEnd;
        }
    }

    if (st.dragging == LoopWaveformState::Dragging::Pan)
    {
        if (IsMouseDown(ImGuiMouseButton_Middle))
        {
            const double dxSamp = static_cast<double>(io.MousePos.x - st.dragAnchorMouseX) / canvasWidth *
                                   (static_cast<double>(st.dragAnchorViewEnd) - st.dragAnchorViewStart);
            const s64 newStart = static_cast<s64>(st.dragAnchorViewStart) - static_cast<s64>(std::lround(dxSamp));
            const s64 newEnd = static_cast<s64>(st.dragAnchorViewEnd) - static_cast<s64>(std::lround(dxSamp));
            st.viewStart = static_cast<u32>(std::max<s64>(0, newStart));
            st.viewEnd = static_cast<u32>(std::max<s64>(static_cast<s64>(st.viewStart), newEnd));
            ClampView(st, n);
            SnapView(st);
        }
        else
        {
            st.dragging = LoopWaveformState::Dragging::None;
        }
    }
    else if (st.dragging == LoopWaveformState::Dragging::Start || st.dragging == LoopWaveformState::Dragging::End)
    {
        const bool stillDown = (st.dragging == LoopWaveformState::Dragging::Start) ? IsMouseDown(ImGuiMouseButton_Left) : IsMouseDown(ImGuiMouseButton_Right);

        if (stillDown)
        {
            u32 s = static_cast<u32>(std::max(0.0, std::round(X2S(st, mouseXRel, canvasWidth))));
            s = std::min(s, n);

            if (st.dragging == LoopWaveformState::Dragging::Start)
            {
                const u32 newStart = std::min(s, loopEnd > 0 ? loopEnd - 1 : 0);
                if (newStart != loopStart) { loopStart = newStart; changed = true; }
            }
            else
            {
                const u32 newEnd = std::max(s, loopStart + 1);
                if (newEnd != loopEnd) { loopEnd = newEnd; changed = true; }
            }
        }
        else
        {
            st.dragging = LoopWaveformState::Dragging::None;
        }
    }

    if (hovered && io.MouseWheel != 0.0f)
    {
        const double center = X2S(st, mouseXRel, canvasWidth);
        if (io.KeyShift)
        {
            const double dx = -io.MouseWheel * 0.05 * ViewSpan(st);
            const s64 newStart = static_cast<s64>(st.viewStart) + static_cast<s64>(std::lround(dx));
            const s64 newEnd = static_cast<s64>(st.viewEnd) + static_cast<s64>(std::lround(dx));

            st.viewStart = static_cast<u32>(std::max<s64>(0, newStart));
            st.viewEnd = static_cast<u32>(std::max<s64>(static_cast<s64>(st.viewStart), newEnd));
            ClampView(st, n);
            SnapView(st);
        }
        else
        {
            const double factor = io.MouseWheel > 0 ? (1.0 / 1.2) : 1.2;
            ZoomAt(st, center, factor, n);
        }
    }

    return changed;
}

bool LoopWaveformEditor(const char* idStr, LoopWaveformState& state,
                         const std::vector<float>& mono, u32 sampleRate,
                         u32& loopStart, u32& loopEnd,
                         float playheadSample,
                         bool showLoop, bool editable,
                         const char* readoutText,
                         ImVec2 size)
{
    bool changed = false;
    const u32 n = static_cast<u32>(mono.size());

    if (!state.viewInitialized && n > 0)
    {
        state.viewStart = 0; state.viewEnd = n;
        state.viewStartAnim = 0.0; state.viewEndAnim = static_cast<double>(n);
        state.viewStartTarget = 0.0; state.viewEndTarget = static_cast<double>(n);
        state.viewInitialized = true;
        state.viewAnimInit = true;
    }
    if (!state.viewAnimInit)
    {
        state.viewStartAnim = state.viewStartTarget = static_cast<double>(state.viewStart);
        state.viewEndAnim = state.viewEndTarget = static_cast<double>(state.viewEnd);
        state.viewAnimInit = true;
    }

    if (n > 0)
    {
        const double dt = GetIO().DeltaTime > 0.0f ? GetIO().DeltaTime : (1.0 / 60.0);
        const double a = 1.0 - std::exp(-dt * 40.0);
        state.viewStartAnim += (state.viewStartTarget - state.viewStartAnim) * a;
        state.viewEndAnim += (state.viewEndTarget - state.viewEndAnim) * a;
        if (std::fabs(state.viewStartTarget - state.viewStartAnim) < 0.75 &&
            std::fabs(state.viewEndTarget - state.viewEndAnim) < 0.75)
        {
            state.viewStartAnim = state.viewStartTarget;
            state.viewEndAnim = state.viewEndTarget;
        }
        s64 vs = std::llround(state.viewStartAnim);
        s64 ve = std::llround(state.viewEndAnim);
        if (vs < 0) vs = 0;
        if (ve > static_cast<s64>(n)) ve = n;
        if (ve <= vs) ve = std::min<s64>(n, vs + 1);
        if (vs >= ve) vs = ve > 0 ? ve - 1 : 0;
        state.viewStart = static_cast<u32>(vs);
        state.viewEnd = static_cast<u32>(ve);
    }

    if (size.x <= 0.0f) size.x = GetContentRegionAvail().x;
    if (size.x < 1.0f) size.x = 1.0f;
    if (size.y <= 0.0f) size.y = 240.0f;
    if (size.y < 1.0f) size.y = 1.0f;

    PushID(idStr);

    ImVec2 origin = GetCursorScreenPos();
    ImDrawList* draw = GetWindowDrawList();

    if (n > 0)
        DrawWaveform(draw, origin, size.x, size.y, state, mono, sampleRate, loopStart, loopEnd, playheadSample, showLoop);
    else
        draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), kColBg);

    if (n > 0 && readoutText && readoutText[0])
        draw->AddText(ImVec2(origin.x + 9.0f, origin.y + 6.0f), kColInkFaint, readoutText);

    InvisibleButton("##canvas", size);

    if (n > 0)
        changed = HandleInteraction(state, mono, size.x, origin, loopStart, loopEnd, showLoop && editable);

    if (n > 0)
    {
        const float trackH = 14.0f;
        ImVec2 trackOrigin = GetCursorScreenPos();
        draw->AddRectFilled(trackOrigin, ImVec2(trackOrigin.x + size.x, trackOrigin.y + trackH), kColEdge);

        const float thumbLeft = (static_cast<float>(state.viewStart) / n) * size.x;
        const float thumbWidth = std::max(8.0f, (static_cast<float>(ViewSpan(state)) / n) * size.x);
        const ImU32 thumbCol = (state.dragging == LoopWaveformState::Dragging::Scrollbar) ? kColWaveLit : kColLoop;
        draw->AddRectFilled(ImVec2(trackOrigin.x + thumbLeft, trackOrigin.y + 1),
                             ImVec2(trackOrigin.x + std::min(size.x, thumbLeft + thumbWidth), trackOrigin.y + trackH - 1),
                             thumbCol, 3.0f);

        InvisibleButton("##scrollbar", ImVec2(size.x, trackH));
        if (IsItemHovered() || state.dragging == LoopWaveformState::Dragging::Scrollbar)
            SetMouseCursor(ImGuiMouseCursor_Hand);

        ImGuiIO& sbIo = GetIO();
        if (IsItemHovered() && IsMouseClicked(ImGuiMouseButton_Left))
        {
            const float clickX = sbIo.MousePos.x - trackOrigin.x;
            const double clickedSample = (clickX / size.x) * n;
            const double half = ViewSpan(state) / 2.0;

            state.viewStart = static_cast<u32>(std::max(0.0, clickedSample - half));
            state.viewEnd = state.viewStart + ViewSpan(state);
            ClampView(state, n);
            SnapView(state);

            state.dragging = LoopWaveformState::Dragging::Scrollbar;
            state.dragAnchorMouseX = sbIo.MousePos.x;
            state.dragAnchorViewStart = state.viewStart;
            state.dragAnchorViewEnd = state.viewEnd;
        }
        else if (state.dragging == LoopWaveformState::Dragging::Scrollbar)
        {
            if (IsMouseDown(ImGuiMouseButton_Left))
            {
                const double dxSamp = static_cast<double>(sbIo.MousePos.x - state.dragAnchorMouseX) / size.x * n;
                const s64 newStart = static_cast<s64>(state.dragAnchorViewStart) + static_cast<s64>(std::lround(dxSamp));
                const s64 span = static_cast<s64>(state.dragAnchorViewEnd) - static_cast<s64>(state.dragAnchorViewStart);
                state.viewStart = static_cast<u32>(std::max<s64>(0, newStart));
                state.viewEnd = state.viewStart + static_cast<u32>(span);
                ClampView(state, n);
                SnapView(state);
            }
            else
            {
                state.dragging = LoopWaveformState::Dragging::None;
            }
        }
    }

    {
        const float z = n > 0 ? static_cast<float>(n) / ViewSpan(state) : 1.0f;
        char zoomLabel[16];
        if (z < 1.05f) snprintf(zoomLabel, sizeof(zoomLabel), "fit");
        else snprintf(zoomLabel, sizeof(zoomLabel), z < 10.0f ? "%.1f\xc3\x97" : "%.0f\xc3\x97", z);

        if (SmallButton("-")) ZoomAt(state, (state.viewStart + state.viewEnd) / 2.0, 1.4, n);
        SameLine(); Text("zoom %s", zoomLabel);
        SameLine();
        if (SmallButton("+")) ZoomAt(state, (state.viewStart + state.viewEnd) / 2.0, 1.0 / 1.4, n);
        SameLine();
        if (Button("Fit loop")) ZoomToLoop(state, loopStart, loopEnd, n);
        SameLine();
        if (Button("Fit all")) ResetView(state, n);
    }

    PopID();
    return changed;
}

void LoopSeamPreview(const std::vector<float>& mono, u32 loopStart, u32 loopEnd, u32 period, u32 sampleRate, float crossfadeMs, bool equalPowerCurve, ImVec2 size)
{
    const u32 n = static_cast<u32>(mono.size());
    ImVec2 origin = GetCursorScreenPos();
    ImDrawList* draw = GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), kColSeamBg);
    Dummy(size);

    if (n == 0 || loopEnd <= loopStart) return;

    const s32 loopLenMinus1 = static_cast<s32>(loopEnd - loopStart - 1);
    s32 xf = static_cast<s32>(crossfadeMs / 1000.0f * sampleRate);
    xf = std::min({ xf, static_cast<s32>(loopStart), loopLenMinus1 });

    if (xf < 0) xf = 0;

    const u32 xfu = static_cast<u32>(xf);
    const u32 view = std::max({ xfu * 2, period * 2, 256u });
    const u32 half = view / 2;
    const float mid = origin.y + size.y * 0.5f;

    draw->AddLine(ImVec2(origin.x, mid), ImVec2(origin.x + size.x, mid), kColEdge);

    auto plot = [&](auto fn, ImU32 color) {
        for (u32 i = 0; i + 1 < view; i++)
        {
            const float x0 = static_cast<float>(i) / view * size.x;
            const float x1 = static_cast<float>(i + 1) / view * size.x;
            const float y0 = mid - fn(i) * (size.y * 0.5f) * 0.9f;
            const float y1 = mid - fn(i + 1) * (size.y * 0.5f) * 0.9f;
            draw->AddLine(ImVec2(origin.x + x0, y0), ImVec2(origin.x + x1, y1), color);
        }
    };

    plot([&](u32 idx) { return mono[ClampIdx(static_cast<s64>(loopEnd) - static_cast<s64>(half) + idx, n)]; }, kColWaveLit);
    plot([&](u32 idx) { return mono[ClampIdx(static_cast<s64>(loopStart) - static_cast<s64>(half) + idx, n)]; }, kColLoop);

    for (u32 i = 0; i + 1 < view; i++)
    {
        auto valueAt = [&](u32 ii) -> float {
            if (ii < half)
            {
                const s64 tailIdx = static_cast<s64>(loopEnd) - static_cast<s64>(half) + ii;
                
                if (tailIdx >= static_cast<s64>(loopEnd) - xfu && xfu > 0)
                {
                    const s64 k = tailIdx - (static_cast<s64>(loopEnd) - xfu);
                    const double t = static_cast<double>(k + 1) / (xfu + 1);
                    const double gOut = equalPowerCurve ? std::cos(t * kPi / 2.0) : (1.0 - t);
                    const double gIn = equalPowerCurve ? std::sin(t * kPi / 2.0) : t;
                    return static_cast<float>(mono[ClampIdx(tailIdx, n)] * gOut + mono[ClampIdx(static_cast<s64>(loopStart) - xfu + k, n)] * gIn);
                }

                return mono[ClampIdx(tailIdx, n)];
            }

            return mono[ClampIdx(static_cast<s64>(loopStart) + (static_cast<s64>(ii) - half), n)];
        };

        const float x0 = static_cast<float>(i) / view * size.x;
        const float x1 = static_cast<float>(i + 1) / view * size.x;
        const float y0 = mid - valueAt(i) * (size.y * 0.5f) * 0.9f;
        const float y1 = mid - valueAt(i + 1) * (size.y * 0.5f) * 0.9f;
        draw->AddLine(ImVec2(origin.x + x0, y0), ImVec2(origin.x + x1, y1), kColGood, 1.6f);
    }

    draw->AddLine(ImVec2(origin.x + size.x * 0.5f, origin.y), ImVec2(origin.x + size.x * 0.5f, origin.y + size.y), IM_COL32(0x5a, 0x66, 0x75, 128));
}

void LoopWaveformZoomToLoop(LoopWaveformState& state, u32 loopStart, u32 loopEnd, u32 totalSamples)
{
    ZoomToLoop(state, loopStart, loopEnd, totalSamples);
}

}
