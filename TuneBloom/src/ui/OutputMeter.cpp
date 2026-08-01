#include "ui/OutputMeter.h"

#include <snd/HardwareMgr.h>

#include <icons/IconsLucide.h>
#include <imgui/imgui.h>

#include <cmath>
#include <algorithm>
#include <cstdio>

OutputMeter &OutputMeter::instance()
{
    static OutputMeter s;
    return s;
}

static float accumPeak_(const float *buf, u32 n)
{
    float p = 0.0f;
    for (u32 i = 0; i < n; i++)
    {
        float a = std::fabs(buf[i]);
        if (a > p)
            p = a;
    }
    return p;
}

void OutputMeter::onFinalMix(const snd::FinalMixData *d)
{
    if (!d || d->sampleCount == 0)
        return;

    bool mono = d->channelCount < 2 || !d->right;
    mMonoFlag.store(mono, std::memory_order_relaxed);

    const float *left = d->left;
    const float *right = mono ? d->left : d->right;

    const float cSampleScale = 1.0f / 32767.0f;

    float pl = left ? accumPeak_(left, d->sampleCount) * cSampleScale : 0.0f;
    float pr = right ? accumPeak_(right, d->sampleCount) * cSampleScale : 0.0f;

    float cur = mPeakL.load(std::memory_order_relaxed);
    while (pl > cur && !mPeakL.compare_exchange_weak(cur, pl, std::memory_order_relaxed))
    {
    }
    cur = mPeakR.load(std::memory_order_relaxed);
    while (pr > cur && !mPeakR.compare_exchange_weak(cur, pr, std::memory_order_relaxed))
    {
    }
}

void OutputMeter::install()
{
    if (mInstalled)
        return;

    snd::internal::driver::HardwareMgr::instance()->appendFinalMixCallback(this);
    mInstalled = true;
}

void OutputMeter::uninstall()
{
    if (!mInstalled)
        return;

    snd::internal::driver::HardwareMgr::instance()->eraseFinalMixCallback(this);
    mInstalled = false;
}

void OutputMeter::updateChannel_(ChannelState &c, float linPeak, float dt)
{
    const float releasePerSec = 0.003f;
    if (linPeak > c.level)
    {
        c.level = linPeak;
    }
    else
    {
        c.level *= std::pow(releasePerSec, dt);
        if (c.level < 1e-7f)
            c.level = 0.0f;
    }

    if (linPeak >= c.peakHold)
    {
        c.peakHold = linPeak;
        c.peakHoldAge = 0.0f;
    }
    else
    {
        c.peakHoldAge += dt;
        if (c.peakHoldAge > 1.0f)
        {
            c.peakHold *= std::pow(0.02f, dt);
            if (c.peakHold < 1e-7f)
                c.peakHold = 0.0f;
        }
    }

    if (linPeak >= 1.0f)
        c.clip = true;
}

void OutputMeter::update(float dt)
{
    float pl = mPeakL.exchange(0.0f, std::memory_order_relaxed);
    float pr = mPeakR.exchange(0.0f, std::memory_order_relaxed);
    mMono = mMonoFlag.load(std::memory_order_relaxed);

    updateChannel_(mLeft, pl, dt);
    updateChannel_(mRight, pr, dt);
}

namespace
{

    constexpr float cFloorDb = -48.0f;
    constexpr float cTickDbs[] = {0.0f, -3.0f, -6.0f, -9.0f, -12.0f, -18.0f, -24.0f, -36.0f, -48.0f};

    float toDbfs(float lin)
    {
        if (lin <= 1e-6f)
            return cFloorDb;
        float db = 20.0f * std::log10(lin);
        return db < cFloorDb ? cFloorDb : db;
    }

    struct MapPoint
    {
        float db;
        float n;
    };
    constexpr MapPoint cWarpMap[] = {
        {0.0f, 1.00f},
        {-3.0f, 0.90f},
        {-6.0f, 0.80f},
        {-12.0f, 0.63f},
        {-24.0f, 0.38f},
        {-36.0f, 0.18f},
        {cFloorDb, 0.0f},
    };

    float dbToNorm(float db)
    {
        if (db >= cWarpMap[0].db)
            return cWarpMap[0].n;

        constexpr size_t cCount = sizeof(cWarpMap) / sizeof(cWarpMap[0]);
        if (db <= cWarpMap[cCount - 1].db)
            return cWarpMap[cCount - 1].n;

        for (size_t i = 1; i < cCount; i++)
        {
            if (db >= cWarpMap[i].db)
            {
                const MapPoint &a = cWarpMap[i - 1];
                const MapPoint &b = cWarpMap[i];
                float t = (db - a.db) / (b.db - a.db);
                return a.n + (b.n - a.n) * t;
            }
        }
        return 0.0f;
    }

    struct ColorStop
    {
        float db;
        ImU32 col;
    };
    constexpr ColorStop cColorStops[] = {
        {cFloorDb, IM_COL32(40, 170, 90, 255)},
        {-12.0f, IM_COL32(90, 200, 80, 255)},
        {-9.0f, IM_COL32(225, 205, 60, 255)},
        {-4.0f, IM_COL32(230, 140, 40, 255)},
        {-1.5f, IM_COL32(228, 85, 45, 255)},
        {0.0f, IM_COL32(220, 45, 40, 255)},
    };

    ImU32 peakMarkerColor(float db)
    {
        if (db >= -1.5f)
            return IM_COL32(235, 90, 70, 255);
        if (db >= -4.0f)
            return IM_COL32(235, 155, 55, 255);
        if (db >= -9.0f)
            return IM_COL32(230, 210, 70, 255);
        return IM_COL32(235, 235, 240, 230);
    }

    void drawGradientHorizontal(ImDrawList *dl, float x0, float x1, float y0, float y1)
    {
        constexpr size_t cCount = sizeof(cColorStops) / sizeof(cColorStops[0]);
        for (size_t i = 1; i < cCount; i++)
        {
            const ColorStop &a = cColorStops[i - 1];
            const ColorStop &b = cColorStops[i];
            float xa = x0 + (x1 - x0) * dbToNorm(a.db);
            float xb = x0 + (x1 - x0) * dbToNorm(b.db);
            dl->AddRectFilledMultiColor(ImVec2(xa, y0), ImVec2(xb, y1), a.col, b.col, b.col, a.col);
        }
    }

    void drawGradientVertical(ImDrawList *dl, float x0, float x1, float y0, float y1)
    {
        constexpr size_t cCount = sizeof(cColorStops) / sizeof(cColorStops[0]);
        for (size_t i = 1; i < cCount; i++)
        {
            const ColorStop &a = cColorStops[i - 1];
            const ColorStop &b = cColorStops[i];
            float ya = y1 - (y1 - y0) * dbToNorm(a.db);
            float yb = y1 - (y1 - y0) * dbToNorm(b.db);
            dl->AddRectFilledMultiColor(ImVec2(x0, yb), ImVec2(x1, ya), b.col, b.col, a.col, a.col);
        }
    }

    void drawMeterFillGradient(ImDrawList *dl, float x0, float y0, float x1, float y1, bool horizontal, float levelNorm)
    {
        ImVec2 clipMin, clipMax;
        if (horizontal)
        {
            clipMin = ImVec2(x0, y0);
            clipMax = ImVec2(x0 + (x1 - x0) * levelNorm, y1);
        }
        else
        {
            clipMin = ImVec2(x0, y1 - (y1 - y0) * levelNorm);
            clipMax = ImVec2(x1, y1);
        }

        dl->PushClipRect(clipMin, clipMax, true);
        if (horizontal)
            drawGradientHorizontal(dl, x0, x1, y0, y1);
        else
            drawGradientVertical(dl, x0, x1, y0, y1);
        dl->PopClipRect();
    }

    bool wantsVerticalLayout(ImVec2 avail)
    {
        static bool sVertical = false;

        float ratio = avail.y > 0.0f ? avail.x / avail.y : 1.0f;

        if (sVertical)
        {
            if (ratio > 1.4f)
                sVertical = false;
        }
        else
        {
            if (ratio < 1.0f)
                sVertical = true;
        }

        return sVertical;
    }

    const float cVerticalGap = 10.0f;
    const float cHorizontalGap = 4.0f;
    const float cTickLen = 3.0f;
    const float cOverCellSize = 5.0f;
    const float cOverGap = 2.0f;

    ImU32 gridLineColor()
    {
        return ImGui::GetColorU32(ImGuiCol_Text, 0.14f);
    }

    char *formatDb(float db, char *buf, size_t bufSize)
    {
        std::snprintf(buf, bufSize, "%g", std::fabs(db));
        return buf;
    }

    void formatChannelLabel(char *buf, size_t bufSize, const char *prefix, const OutputMeter::ChannelState &c)
    {
        if (c.peakHold <= 0.0f)
            std::snprintf(buf, bufSize, "%s inf", prefix);
        else
            std::snprintf(buf, bufSize, "%s %.1f", prefix, std::fabs(toDbfs(c.peakHold)));
    }

    void drawClipResetButton(OutputMeter::Channel ch, ImVec2 rectMin, ImVec2 rectMax)
    {
        ImGui::PushID((int)ch);
        ImVec2 savedCursor = ImGui::GetCursorPos();
        ImGui::SetCursorScreenPos(rectMin);
        ImGui::InvisibleButton("##ClipReset", ImVec2(rectMax.x - rectMin.x, rectMax.y - rectMin.y));
        if (ImGui::IsItemClicked())
            OutputMeter::instance().resetClip(ch);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Click to clear clip indicator");
        ImGui::SetCursorPos(savedCursor);
        ImGui::PopID();
    }

    void drawHorizontalBar(ImDrawList *dl, const char *label, const OutputMeter::ChannelState &c, OutputMeter::Channel ch, ImVec2 pos, float width, float barH, float gutter)
    {
        ImU32 textCol = ImGui::GetColorU32(ImGuiCol_Text);
        ImU32 trackCol = ImGui::GetColorU32(ImGuiCol_FrameBg);

        ImVec2 labelSize = ImGui::CalcTextSize(label);

        float xFull1 = pos.x + width;
        float x0 = pos.x + gutter;
        float x1 = xFull1 - cOverCellSize - cOverGap;
        float y0 = pos.y;
        float y1 = pos.y + barH;

        dl->AddText(ImVec2(x0 - labelSize.x - 4.0f, y0 + (barH - labelSize.y) * 0.5f), textCol, label);
        dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), trackCol, 2.0f);

        ImU32 gridCol = gridLineColor();
        for (float db : cTickDbs)
        {
            float x = x0 + (x1 - x0) * dbToNorm(db);
            dl->AddLine(ImVec2(x, y0), ImVec2(x, y1), gridCol, 1.0f);
        }

        float levelNorm = dbToNorm(toDbfs(c.level));
        drawMeterFillGradient(dl, x0, y0, x1, y1, true, levelNorm);

        float peakDb = toDbfs(c.peakHold);
        float px = x0 + (x1 - x0) * dbToNorm(peakDb);
        dl->AddLine(ImVec2(px, y0), ImVec2(px, y1), peakMarkerColor(peakDb), 1.0f);

        ImVec2 overMin(xFull1 - cOverCellSize, y0);
        ImU32 overCol = c.clip ? IM_COL32(255, 45, 35, 255) : trackCol;
        dl->AddRectFilled(overMin, ImVec2(xFull1, y1), overCol, 2.0f);
        if (c.clip)
            drawClipResetButton(ch, ImVec2(x0 - labelSize.x - 4.0f, y0), ImVec2(xFull1, y1));
    }

    void drawVerticalBar(ImDrawList *dl, const char *label, const OutputMeter::ChannelState &c, OutputMeter::Channel ch, ImVec2 pos, float barW, float height)
    {
        ImU32 textCol = ImGui::GetColorU32(ImGuiCol_Text);
        ImU32 trackCol = ImGui::GetColorU32(ImGuiCol_FrameBg);

        float x0 = pos.x;
        float x1 = pos.x + barW;
        float yFull0 = pos.y;
        float y0 = pos.y + cOverCellSize + cOverGap;
        float y1 = pos.y + height;

        dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), trackCol, 2.0f);

        ImU32 gridCol = gridLineColor();
        for (float db : cTickDbs)
        {
            float y = y1 - (y1 - y0) * dbToNorm(db);
            dl->AddLine(ImVec2(x0, y), ImVec2(x1, y), gridCol, 1.0f);
        }

        float levelNorm = dbToNorm(toDbfs(c.level));
        drawMeterFillGradient(dl, x0, y0, x1, y1, false, levelNorm);

        float peakDb = toDbfs(c.peakHold);
        float py = y1 - (y1 - y0) * dbToNorm(peakDb);
        dl->AddLine(ImVec2(x0, py), ImVec2(x1, py), peakMarkerColor(peakDb), 1.0f);

        ImU32 overCol = c.clip ? IM_COL32(255, 45, 35, 255) : trackCol;
        dl->AddRectFilled(ImVec2(x0, yFull0), ImVec2(x1, yFull0 + cOverCellSize), overCol, 2.0f);
        if (c.clip)
            drawClipResetButton(ch, ImVec2(x0, yFull0), ImVec2(x1, y1));

        ImVec2 labelSize = ImGui::CalcTextSize(label);
        dl->AddText(ImVec2(x0 + (barW - labelSize.x) * 0.5f, y1 + 2.0f), textCol, label);
    }

    void drawDbScaleRow(ImDrawList *dl, ImVec2 pos, float gutter, float width)
    {
        ImU32 col = ImGui::GetColorU32(ImGuiCol_Text, 0.65f);
        float x0 = pos.x + gutter;
        float x1 = pos.x + width;
        float minSpacing = ImGui::CalcTextSize("48").x + 4.0f;

        float lastX = 1e9f;
        for (float db : cTickDbs)
        {
            float x = x0 + (x1 - x0) * dbToNorm(db);
            if (lastX - x < minSpacing)
                continue;

            char buf[8];
            ImVec2 ts = ImGui::CalcTextSize(formatDb(db, buf, sizeof(buf)));
            dl->AddLine(ImVec2(x, pos.y), ImVec2(x, pos.y + cTickLen), col, 1.0f);
            dl->AddText(ImVec2(std::clamp(x - ts.x * 0.5f, x0, std::max(x0, x1 - ts.x)), pos.y + cTickLen + 1.0f), col, buf);
            lastX = x;
        }
    }

    void drawDbScaleColumn(ImDrawList *dl, ImVec2 pos, float height)
    {
        ImU32 col = ImGui::GetColorU32(ImGuiCol_Text, 0.65f);
        float y0 = pos.y;
        float y1 = pos.y + height;
        float minSpacing = ImGui::GetTextLineHeight();

        float lastY = -1e9f;
        for (float db : cTickDbs)
        {
            float y = y1 - (y1 - y0) * dbToNorm(db);
            if (y - lastY < minSpacing)
                continue;

            char buf[8];
            ImVec2 ts = ImGui::CalcTextSize(formatDb(db, buf, sizeof(buf)));
            dl->AddLine(ImVec2(pos.x, y), ImVec2(pos.x + cTickLen, y), col, 1.0f);
            dl->AddText(ImVec2(pos.x + cTickLen + 2.0f, std::clamp(y - ts.y * 0.5f, y0, std::max(y0, y1 - ts.y))), col, buf);
            lastY = y;
        }
    }

    void drawOutputMeterFill(ImVec2 avail)
    {
        OutputMeter &meter = OutputMeter::instance();
        const OutputMeter::ChannelState &chL = meter.getLeft();
        const OutputMeter::ChannelState &chR = meter.getRight();
        bool mono = meter.isMono();

        char lLabel[24], rLabel[24], mLabel[24];
        formatChannelLabel(lLabel, sizeof(lLabel), "L", chL);
        formatChannelLabel(rLabel, sizeof(rLabel), "R", chR);
        formatChannelLabel(mLabel, sizeof(mLabel), "M", chL);

        ImDrawList *dl = ImGui::GetWindowDrawList();
        ImVec2 p0 = ImGui::GetCursorScreenPos();

        if (wantsVerticalLayout(avail))
        {
            float labelH = ImGui::GetTextLineHeight() + 4.0f;
            float scaleW = cTickLen + 2.0f + ImGui::CalcTextSize("48").x + 4.0f;
            float barsAreaW = std::max(10.0f, avail.x - scaleW);
            float barH = std::max(10.0f, avail.y - labelH);

            const float cSideInset = 6.0f;

            if (mono)
            {
                float barW = std::min(160.0f, std::max(16.0f, barsAreaW - cSideInset * 2.0f));
                float x0 = p0.x + (barsAreaW - barW) * 0.5f;
                drawVerticalBar(dl, mLabel, chL, OutputMeter::Channel::Left, ImVec2(x0, p0.y), barW, barH);
            }
            else
            {
                float usableW = std::max(20.0f, barsAreaW - cSideInset * 2.0f);
                float barW = std::min(160.0f, std::max(12.0f, (usableW - cVerticalGap) * 0.5f));
                float totalW = barW * 2.0f + cVerticalGap;
                float x0 = p0.x + cSideInset + (usableW - totalW) * 0.5f;
                drawVerticalBar(dl, lLabel, chL, OutputMeter::Channel::Left, ImVec2(x0, p0.y), barW, barH);
                drawVerticalBar(dl, rLabel, chR, OutputMeter::Channel::Right, ImVec2(x0 + barW + cVerticalGap, p0.y), barW, barH);
            }

            drawDbScaleColumn(dl, ImVec2(p0.x + barsAreaW, p0.y), barH);
        }
        else
        {
            float gutter = std::max(ImGui::CalcTextSize(lLabel).x, ImGui::CalcTextSize(mLabel).x) + 6.0f;
            float scaleH = cTickLen + 1.0f + ImGui::GetTextLineHeight();

            if (mono)
            {
                float barH = std::max(10.0f, avail.y - scaleH);
                drawHorizontalBar(dl, mLabel, chL, OutputMeter::Channel::Left, p0, avail.x, barH, gutter);
                drawDbScaleRow(dl, ImVec2(p0.x, p0.y + barH), gutter, avail.x);
            }
            else
            {
                float barsH = std::max(20.0f, avail.y - scaleH);
                float barH = std::max(10.0f, (barsH - cHorizontalGap) * 0.5f);
                drawHorizontalBar(dl, lLabel, chL, OutputMeter::Channel::Left, p0, avail.x, barH, gutter);
                drawHorizontalBar(dl, rLabel, chR, OutputMeter::Channel::Right, ImVec2(p0.x, p0.y + barH + cHorizontalGap), avail.x, barH, gutter);
                drawDbScaleRow(dl, ImVec2(p0.x, p0.y + barH * 2.0f + cHorizontalGap), gutter, avail.x);
            }
        }

        ImGui::Dummy(avail);
    }

}

void DrawOutputMeterWindow()
{
    if (ImGui::Begin(ICON_LC_GAUGE " Output Meter###OutputMeterWindow"))
    {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.x > 1.0f && avail.y > 1.0f)
            drawOutputMeterFill(avail);
    }
    ImGui::End();
}
