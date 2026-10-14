#include <ui/player/OutputMeter.h>

#include <ui/widgets/ContextMenu.h>
#include <ui/Messages.h>
#include <ui/settings/UiColors.h>

#include <snd/HardwareMgr.h>

#include <prim/seadSafeString.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

static constexpr f32 cSampleScale   = 1.0f / 32767.0f;
static constexpr f32 cReleasePerSec = 0.05f;
static constexpr f32 cPeakHoldSec   = 1.5f;
static constexpr f32 cSilentLevel   = 1e-7f;
static constexpr f32 cFloorDb       = -48.0f;

static constexpr f32 cTickDbs[] = { 0.0f, -3.0f, -6.0f, -9.0f, -12.0f, -18.0f, -24.0f, -36.0f, -48.0f };

struct MapPoint
{
    f32 db;
    f32 position;
};

static constexpr MapPoint cWarpMap[] = {
    { 0.0f, 1.00f },
    { -3.0f, 0.90f },
    { -6.0f, 0.80f },
    { -12.0f, 0.63f },
    { -24.0f, 0.38f },
    { -36.0f, 0.18f },
    { cFloorDb, 0.0f },
};

struct ColorStop
{
    f32 db;
    ImU32 color;
};

static constexpr ColorStop cColorStops[] = {
    { cFloorDb, uicolors::meter::cLevelFloor },
    { -12.0f, uicolors::meter::cLevelLow },
    { -9.0f, uicolors::meter::cLevelMid },
    { -4.0f, uicolors::meter::cLevelHigh },
    { -1.5f, uicolors::meter::cLevelHot },
    { 0.0f, uicolors::meter::cLevelClip },
};

static constexpr f32 cTickLen        = 4.0f;
static constexpr f32 cTickPad        = 7.0f;
static constexpr f32 cPeakMarkerSize = 2.0f;
static constexpr f32 cUnlitAlpha     = 0.16f;
static constexpr f32 cDimTextAlpha   = 0.55f;
static constexpr f32 cTickAlpha      = 0.30f;
static constexpr f32 cReadoutPadX    = 4.0f;
static constexpr f32 cReadoutPadY    = 2.0f;
static constexpr f32 cReadoutGap     = 6.0f;
static constexpr f32 cNameGap        = 4.0f;
static constexpr f32 cNamePadX       = 6.0f;
static constexpr f32 cMinBarWidth    = 22.0f;
static constexpr f32 cMaxBarWidth    = 44.0f;
static constexpr f32 cMinBarHeight   = 10.0f;
static constexpr f32 cMaxBarHeight   = 28.0f;
static constexpr f32 cVerticalBelow  = 1.0f;
static constexpr f32 cHorizontalFrom = 1.4f;

static bool sMergeChannels = false;
static bool sWindowVisible = false;

OutputMeter& OutputMeter::instance()
{
    static OutputMeter sInstance;
    return sInstance;
}

static f32 CalcPeak(const f32* samples, u32 sampleNum)
{
    f32 peak = 0.0f;

    for (u32 i = 0; i < sampleNum; i++)
    {
        const f32 value = std::fabs(samples[i]);
        if (value > peak)
            peak = value;
    }

    return peak;
}

static void StorePeak(std::atomic<f32>& stored, f32 peak)
{
    f32 current = stored.load(std::memory_order_relaxed);

    while (peak > current && !stored.compare_exchange_weak(current, peak, std::memory_order_relaxed))
    {
    }
}

void OutputMeter::onFinalMix(const snd::FinalMixData* data)
{
    if (!data || data->sampleCount == 0)
        return;

    const bool mono = data->channelCount < 2 || !data->right;
    mMonoFlag.store(mono, std::memory_order_relaxed);

    const f32* left = data->left;
    const f32* right = mono ? data->left : data->right;

    const f32 peakL = left ? CalcPeak(left, data->sampleCount) * cSampleScale : 0.0f;
    const f32 peakR = right ? CalcPeak(right, data->sampleCount) * cSampleScale : 0.0f;

    StorePeak(mPeakL, peakL);
    StorePeak(mPeakR, peakR);
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

void OutputMeter::updateChannel(ChannelState& channel, f32 peak, f32 dt)
{
    const f32 release = std::pow(cReleasePerSec, dt);

    if (peak > channel.level)
    {
        channel.level = peak;
    }
    else
    {
        channel.level *= release;
        if (channel.level < cSilentLevel)
            channel.level = 0.0f;
    }

    if (peak >= channel.peakHold)
    {
        channel.peakHold = peak;
        channel.peakHoldAge = 0.0f;
    }
    else
    {
        channel.peakHoldAge += dt;
        if (channel.peakHoldAge > cPeakHoldSec)
        {
            channel.peakHold = std::max(channel.level, channel.peakHold * release);
            if (channel.peakHold < cSilentLevel)
                channel.peakHold = 0.0f;
        }
    }

    if (peak >= 1.0f)
        channel.clip = true;
}

void OutputMeter::resetPeaks()
{
    for (ChannelState* channel : { &mLeft, &mRight })
    {
        channel->peakHold = channel->level;
        channel->peakHoldAge = 0.0f;
        channel->clip = false;
    }
}

void OutputMeter::update(f32 dt)
{
    const f32 peakL = mPeakL.exchange(0.0f, std::memory_order_relaxed);
    const f32 peakR = mPeakR.exchange(0.0f, std::memory_order_relaxed);
    mMono = mMonoFlag.load(std::memory_order_relaxed);

    updateChannel(mLeft, peakL, dt);
    updateChannel(mRight, peakR, dt);
}

static f32 ToDbfs(f32 level)
{
    if (level <= 1e-6f)
        return cFloorDb;

    const f32 db = 20.0f * std::log10(level);
    return db < cFloorDb ? cFloorDb : db;
}

static f32 DbToPosition(f32 db)
{
    const s32 pointNum = IM_ARRAYSIZE(cWarpMap);

    if (db >= cWarpMap[0].db)
        return cWarpMap[0].position;

    if (db <= cWarpMap[pointNum - 1].db)
        return cWarpMap[pointNum - 1].position;

    for (s32 i = 1; i < pointNum; i++)
    {
        if (db < cWarpMap[i].db)
            continue;

        const MapPoint& a = cWarpMap[i - 1];
        const MapPoint& b = cWarpMap[i];
        const f32 t = (db - a.db) / (b.db - a.db);

        return a.position + (b.position - a.position) * t;
    }

    return 0.0f;
}

static f32 PositionToDb(f32 position)
{
    const s32 pointNum = IM_ARRAYSIZE(cWarpMap);

    if (position >= cWarpMap[0].position)
        return cWarpMap[0].db;

    if (position <= cWarpMap[pointNum - 1].position)
        return cWarpMap[pointNum - 1].db;

    for (s32 i = 1; i < pointNum; i++)
    {
        if (position < cWarpMap[i].position)
            continue;

        const MapPoint& a = cWarpMap[i - 1];
        const MapPoint& b = cWarpMap[i];
        const f32 t = (position - a.position) / (b.position - a.position);

        return a.db + (b.db - a.db) * t;
    }

    return cFloorDb;
}

static ImU32 GetPeakMarkerColor(f32 db)
{
    if (db >= -1.5f)
        return uicolors::meter::cPeakHot;

    if (db >= -4.0f)
        return uicolors::meter::cPeakHigh;

    if (db >= -9.0f)
        return uicolors::meter::cPeakMid;

    return uicolors::meter::cPeakSafe;
}

static bool WantsVerticalLayout(const ImVec2& avail)
{
    static bool sVerticalLayout = false;

    const f32 ratio = avail.y > 0.0f ? avail.x / avail.y : 1.0f;

    if (sVerticalLayout)
    {
        if (ratio > cHorizontalFrom)
            sVerticalLayout = false;
    }
    else if (ratio < cVerticalBelow)
    {
        sVerticalLayout = true;
    }

    return sVerticalLayout;
}

static const char* FormatPeakDb(char* buffer, size_t size, const OutputMeter::ChannelState& channel)
{
    const f32 db = ToDbfs(channel.peakHold);

    if (db <= cFloorDb)
        std::snprintf(buffer, size, "%s", messages::common::cSilent);
    else
        std::snprintf(buffer, size, "%.1f", db);

    return buffer;
}

static ImU32 MultiplyAlpha(ImU32 color, f32 alpha)
{
    ImVec4 value = ImGui::ColorConvertU32ToFloat4(color);
    value.w *= alpha;

    return ImGui::ColorConvertFloat4ToU32(value);
}

static void DrawGradient(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, bool horizontal, f32 alpha)
{
    for (s32 i = 1; i < IM_ARRAYSIZE(cColorStops); i++)
    {
        const ColorStop& a = cColorStops[i - 1];
        const ColorStop& b = cColorStops[i];
        const ImU32 colorA = MultiplyAlpha(a.color, alpha);
        const ImU32 colorB = MultiplyAlpha(b.color, alpha);

        if (horizontal)
        {
            const f32 xa = min.x + (max.x - min.x) * DbToPosition(a.db);
            const f32 xb = min.x + (max.x - min.x) * DbToPosition(b.db);

            drawList->AddRectFilledMultiColor(ImVec2(xa, min.y), ImVec2(xb, max.y), colorA, colorB, colorB, colorA);
        }
        else
        {
            const f32 ya = max.y - (max.y - min.y) * DbToPosition(a.db);
            const f32 yb = max.y - (max.y - min.y) * DbToPosition(b.db);

            drawList->AddRectFilledMultiColor(ImVec2(min.x, yb), ImVec2(max.x, ya), colorB, colorB, colorA, colorA);
        }
    }
}

static void DrawPeakMarker(ImDrawList* drawList, f32 peakHold, const ImVec2& min, const ImVec2& max, bool horizontal)
{
    const f32 peakDb = ToDbfs(peakHold);
    if (peakDb <= cFloorDb)
        return;

    const f32 position = DbToPosition(peakDb);
    const ImU32 color = GetPeakMarkerColor(peakDb);

    if (horizontal)
    {
        const f32 x = std::min(std::floor(min.x + (max.x - min.x) * position), max.x - cPeakMarkerSize);
        drawList->AddRectFilled(ImVec2(x, min.y), ImVec2(x + cPeakMarkerSize, max.y), color);
    }
    else
    {
        const f32 y = std::min(std::floor(max.y - (max.y - min.y) * position), max.y - cPeakMarkerSize);
        drawList->AddRectFilled(ImVec2(min.x, y), ImVec2(max.x, y + cPeakMarkerSize), color);
    }
}

static void DrawBar(ImDrawList* drawList, const OutputMeter::ChannelState& channel, const ImVec2& min, const ImVec2& max, bool horizontal)
{
    drawList->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_FrameBg));
    DrawGradient(drawList, min, max, horizontal, cUnlitAlpha);

    // Level
    const f32 level = DbToPosition(ToDbfs(channel.level));
    if (level > 0.0f)
    {
        ImVec2 fillMin = min;
        ImVec2 fillMax = max;

        if (horizontal)
            fillMax.x = min.x + (max.x - min.x) * level;
        else
            fillMin.y = max.y - (max.y - min.y) * level;

        drawList->PushClipRect(fillMin, fillMax, true);
        DrawGradient(drawList, min, max, horizontal, 1.0f);
        drawList->PopClipRect();
    }

    // Peak hold
    DrawPeakMarker(drawList, channel.peakHold, min, max, horizontal);
}

static ImVec2 CalcPeakReadoutSize()
{
    return ImVec2(ImGui::CalcTextSize("-88.8").x + cReadoutPadX * 2.0f, ImGui::GetTextLineHeight() + cReadoutPadY * 2.0f);
}

static void DrawPeakReadout(ImDrawList* drawList, const OutputMeter::ChannelState& channel, const ImVec2& center)
{
    char text[16];
    const ImVec2 textSize = ImGui::CalcTextSize(FormatPeakDb(text, sizeof(text), channel));
    const ImVec2 size = CalcPeakReadoutSize();
    const ImVec2 min(std::floor(center.x - size.x * 0.5f), std::floor(center.y - size.y * 0.5f));

    drawList->AddRectFilled(min, ImVec2(min.x + size.x, min.y + size.y), channel.clip ? uicolors::meter::cLevelClip : ImGui::GetColorU32(ImGuiCol_FrameBg), 3.0f);
    drawList->AddText(ImVec2(std::floor(min.x + (size.x - textSize.x) * 0.5f), min.y + cReadoutPadY), channel.clip ? uicolors::meter::cClipText : ImGui::GetColorU32(ImGuiCol_Text), text);
}

static void DrawChannelName(ImDrawList* drawList, const char* name, const ImVec2& center)
{
    const ImVec2 textSize = ImGui::CalcTextSize(name);

    drawList->AddText(ImVec2(std::floor(center.x - textSize.x * 0.5f), std::floor(center.y - textSize.y * 0.5f)), ImGui::GetColorU32(ImGuiCol_Text, cDimTextAlpha), name);
}

static f32 CalcScaleColumnWidth()
{
    return ImGui::CalcTextSize("48").x + (cTickLen + cTickPad) * 2.0f;
}

static f32 CalcScaleRowHeight()
{
    return ImGui::GetTextLineHeight() + (cTickLen + 2.0f) * 2.0f;
}

static void DrawScaleColumn(ImDrawList* drawList, f32 x0, f32 x1, f32 y0, f32 y1, bool bothSides)
{
    const ImU32 tickColor = ImGui::GetColorU32(ImGuiCol_Text, cTickAlpha);
    const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text, cDimTextAlpha);
    const f32 minSpacing = ImGui::GetTextLineHeight() + 2.0f;

    f32 lastY = -1e9f;

    for (f32 db : cTickDbs)
    {
        const f32 y = std::floor(y1 - (y1 - y0) * DbToPosition(db));
        if (y - lastY < minSpacing)
            continue;

        char text[8];
        std::snprintf(text, sizeof(text), "%g", std::fabs(db));
        const ImVec2 textSize = ImGui::CalcTextSize(text);

        drawList->AddRectFilled(ImVec2(x0 + 1.0f, y), ImVec2(x0 + 1.0f + cTickLen, y + 1.0f), tickColor);

        if (bothSides)
            drawList->AddRectFilled(ImVec2(x1 - 1.0f - cTickLen, y), ImVec2(x1 - 1.0f, y + 1.0f), tickColor);

        drawList->AddText(ImVec2(std::floor((x0 + x1 - textSize.x) * 0.5f), std::floor(y - textSize.y * 0.5f)), textColor, text);
        lastY = y;
    }
}

static void DrawScaleRow(ImDrawList* drawList, f32 x0, f32 x1, f32 y0, f32 y1, bool bothSides)
{
    const ImU32 tickColor = ImGui::GetColorU32(ImGuiCol_Text, cTickAlpha);
    const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text, cDimTextAlpha);
    const f32 minSpacing = ImGui::CalcTextSize("48").x + 8.0f;

    f32 lastX = 1e9f;

    for (f32 db : cTickDbs)
    {
        const f32 x = std::floor(x0 + (x1 - x0) * DbToPosition(db));
        if (lastX - x < minSpacing)
            continue;

        char text[8];
        std::snprintf(text, sizeof(text), "%g", std::fabs(db));
        const ImVec2 textSize = ImGui::CalcTextSize(text);

        drawList->AddRectFilled(ImVec2(x, y0 + 1.0f), ImVec2(x + 1.0f, y0 + 1.0f + cTickLen), tickColor);

        if (bothSides)
            drawList->AddRectFilled(ImVec2(x, y1 - 1.0f - cTickLen), ImVec2(x + 1.0f, y1 - 1.0f), tickColor);

        drawList->AddText(ImVec2(std::floor(x - textSize.x * 0.5f), std::floor((y0 + y1 - textSize.y) * 0.5f)), textColor, text);
        lastX = x;
    }
}

static void DrawVerticalMeter(ImDrawList* drawList, const ImVec2& pos, const ImVec2& avail, const OutputMeter::ChannelState& left, const OutputMeter::ChannelState& right, bool mono)
{
    const OutputMeter::ChannelState* channels[] = { &left, &right };
    const char* names[] = { mono ? messages::playback::cMeterMono : messages::common::cLeft, messages::common::cRight };
    const s32 barNum = mono ? 1 : 2;

    const f32 readoutHeight = CalcPeakReadoutSize().y;
    const f32 nameHeight = ImGui::GetTextLineHeight();
    const f32 scaleWidth = CalcScaleColumnWidth();

    const f32 barWidth = std::floor(std::clamp((avail.x - scaleWidth) / barNum, cMinBarWidth, cMaxBarWidth));
    const f32 totalWidth = barWidth * barNum + scaleWidth;
    const f32 x = std::floor(pos.x + (avail.x - totalWidth) * 0.5f);
    const f32 y0 = std::floor(pos.y + readoutHeight + cReadoutGap);
    const f32 y1 = std::max(y0 + cMinBarHeight, std::floor(pos.y + avail.y - nameHeight - cNameGap));

    for (s32 i = 0; i < barNum; i++)
    {
        const f32 barX = x + (barWidth + scaleWidth) * i;
        const f32 centerX = barX + barWidth * 0.5f;

        DrawPeakReadout(drawList, *channels[i], ImVec2(centerX, pos.y + readoutHeight * 0.5f));
        DrawBar(drawList, *channels[i], ImVec2(barX, y0), ImVec2(barX + barWidth, y1), false);
        DrawChannelName(drawList, names[i], ImVec2(centerX, y1 + cNameGap + nameHeight * 0.5f));
    }

    DrawScaleColumn(drawList, x + barWidth, x + barWidth + scaleWidth, y0, y1, !mono);
}

static void DrawHorizontalMeter(ImDrawList* drawList, const ImVec2& pos, const ImVec2& avail, const OutputMeter::ChannelState& left, const OutputMeter::ChannelState& right, bool mono)
{
    const OutputMeter::ChannelState* channels[] = { &left, &right };
    const char* names[] = { mono ? messages::playback::cMeterMono : messages::common::cLeft, messages::common::cRight };
    const s32 barNum = mono ? 1 : 2;

    const f32 nameWidth = ImGui::CalcTextSize(messages::playback::cMeterMono).x + cNamePadX;
    const f32 readoutWidth = CalcPeakReadoutSize().x;
    const f32 scaleHeight = CalcScaleRowHeight();

    const f32 barHeight = std::floor(std::clamp((avail.y - scaleHeight) / barNum, cMinBarHeight, cMaxBarHeight));
    const f32 totalHeight = barHeight * barNum + scaleHeight;
    const f32 y = std::floor(pos.y + std::max(0.0f, (avail.y - totalHeight) * 0.5f));
    const f32 x0 = std::floor(pos.x + nameWidth);
    const f32 x1 = std::max(x0 + cMinBarWidth, std::floor(pos.x + avail.x - readoutWidth - cReadoutGap));

    for (s32 i = 0; i < barNum; i++)
    {
        const f32 barY = y + (barHeight + scaleHeight) * i;
        const f32 centerY = barY + barHeight * 0.5f;

        DrawChannelName(drawList, names[i], ImVec2(pos.x + (nameWidth - cNamePadX) * 0.5f, centerY));
        DrawBar(drawList, *channels[i], ImVec2(x0, barY), ImVec2(x1, barY + barHeight), true);
        DrawPeakReadout(drawList, *channels[i], ImVec2(x1 + cReadoutGap + readoutWidth * 0.5f, centerY));
    }

    DrawScaleRow(drawList, x0, x1, y + barHeight, y + barHeight + scaleHeight, !mono);
}

static void SetMergeChannels(bool merge)
{
    if (sMergeChannels == merge)
        return;

    sMergeChannels = merge;
    ImGui::MarkIniSettingsDirty();
}

static void DrawOutputMeter(const ImVec2& avail)
{
    OutputMeter& meter = OutputMeter::instance();
    const OutputMeter::ChannelState& left = meter.getLeft();
    const OutputMeter::ChannelState& right = meter.getRight();
    const bool single = meter.isMono() || sMergeChannels;

    OutputMeter::ChannelState merged;
    merged.level = std::max(left.level, right.level);
    merged.peakHold = std::max(left.peakHold, right.peakHold);
    merged.clip = left.clip || right.clip;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    if (WantsVerticalLayout(avail))
        DrawVerticalMeter(drawList, pos, avail, single ? merged : left, right, single);
    else
        DrawHorizontalMeter(drawList, pos, avail, single ? merged : left, right, single);

    ImGui::InvisibleButton("##PeakReset", avail);

    if (ImGui::IsItemClicked())
        meter.resetPeaks();

    if (ImGui::IsItemHovered() && merged.clip)
        ImGui::SetTooltip("%s", messages::playback::cMeterResetTooltip);

    if (BeginItemContextMenu("##MeterMenu"))
    {
        if (ContextMenuCheckItem(messages::common::cStereo, !sMergeChannels))
            SetMergeChannels(false);

        if (ContextMenuCheckItem(messages::playback::cMeterMerged, sMergeChannels))
            SetMergeChannels(true);

        EndContextMenu();
    }
}

static void* MeterSettingsReadOpen(ImGuiContext*, ImGuiSettingsHandler*, const char*)
{
    return &sMergeChannels;
}

static void MeterSettingsReadLine(ImGuiContext*, ImGuiSettingsHandler*, void*, const char* line)
{
    int value = 0;

    if (sscanf(line, "Merged=%d", &value) == 1)
        sMergeChannels = value != 0;
    else if (sscanf(line, "Visible=%d", &value) == 1)
        sWindowVisible = value != 0;
}

static void MeterSettingsWriteAll(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buf)
{
    buf->appendf("[%s][Meter]\n", handler->TypeName);
    buf->appendf("Merged=%d\n", sMergeChannels ? 1 : 0);
    buf->appendf("Visible=%d\n\n", sWindowVisible ? 1 : 0);
}

void RegisterOutputMeterSettings()
{
    ImGuiSettingsHandler handler;
    handler.TypeName = "OutputMeter";
    handler.TypeHash = ImHashStr("OutputMeter");
    handler.ReadOpenFn = MeterSettingsReadOpen;
    handler.ReadLineFn = MeterSettingsReadLine;
    handler.WriteAllFn = MeterSettingsWriteAll;

    ImGui::AddSettingsHandler(&handler);
}

void DrawOutputMeterBar(const OutputMeter::ChannelState& state, const ImVec2& min, const ImVec2& max, bool horizontal)
{
    DrawBar(ImGui::GetWindowDrawList(), state, min, max, horizontal);
}

void DrawOutputMeterRows(const ImVec2& min, f32 width, f32 rowHeight, f32 gap)
{
    const OutputMeter& meter = OutputMeter::instance();
    const ImVec2 max(min.x + width, min.y + rowHeight * 2.0f + gap);

    if (meter.isMono())
    {
        OutputMeter::ChannelState merged;
        merged.level = std::max(meter.getLeft().level, meter.getRight().level);
        merged.peakHold = std::max(meter.getLeft().peakHold, meter.getRight().peakHold);

        DrawOutputMeterBar(merged, min, max, true);
        return;
    }

    DrawOutputMeterBar(meter.getLeft(), min, ImVec2(max.x, min.y + rowHeight), true);
    DrawOutputMeterBar(meter.getRight(), ImVec2(min.x, max.y - rowHeight), max, true);
}

f32 OutputMeterDbToPosition(f32 db)
{
    return DbToPosition(db);
}

f32 OutputMeterPositionToDb(f32 position)
{
    return PositionToDb(position);
}

bool IsOutputMeterWindowVisible()
{
    return sWindowVisible;
}

void SetOutputMeterWindowVisible(bool visible)
{
    if (sWindowVisible == visible)
        return;

    sWindowVisible = visible;
    ImGui::MarkIniSettingsDirty();
}

void DrawOutputMeterWindow()
{
    if (!sWindowVisible)
        return;

    bool open = true;

    if (ImGui::Begin(sead::FormatFixedSafeString<64>(ICON_LC_GAUGE " %s###OutputMeterWindow", messages::mainmenu::cOutputMeter).cstr(), &open))
    {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.x > 1.0f && avail.y > 1.0f)
            DrawOutputMeter(avail);
    }

    ImGui::End();

    SetOutputMeterWindowVisible(open);
}
