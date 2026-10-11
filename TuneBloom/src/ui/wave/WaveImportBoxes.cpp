#include <ui/wave/WaveImportPanelInternal.h>

#include <ui/items/ItemColumns.h>
#include <ui/widgets/Knob.h>
#include <ui/Messages.h>
#include <ui/widgets/PanelLayout.h>
#include <ui/widgets/Properties.h>
#include <ui/wave/WaveFormatUI.h>
#include <ui/widgets/Widgets.h>

#include <bfsar/audio/LoopAnalysis.h>

#include <algorithm>
#include <cstdio>

namespace waveimport
{

constexpr f32 cColumnGapEm      = 0.6f;
constexpr f32 cLabelGapEm       = 0.6f;
constexpr f32 cFieldWidthEm     = 5.0f;
constexpr f32 cLoopModeWidthEm  = 9.0f;

constexpr f32 cSampleDragRange       = 2000.0f;
constexpr f32 cNormalizeMin          = -24.0f;
constexpr f32 cNormalizeMax          = 0.0f;
constexpr f32 cNormalizeDragSpeed    = 0.1f;
constexpr f32 cNormalizeShortMin     = -10.0f;
constexpr f32 cCrossfadeDragRange    = 200.0f;
constexpr f32 cCrossfadeDragSpeedMin = 0.1f;

static const char* const cPitchKnobs[]     = { messages::param::cCoarse, messages::param::cFine };
static const char* const cNormalizeKnobs[] = { messages::import::cWaveNormalizeLevel };

static void FormatRate(char* buffer, size_t size, u32 rate, bool isSource)
{
    if (isSource)
        snprintf(buffer, size, messages::import::cWaveRateSourceFormat, rate);
    else
        snprintf(buffer, size, messages::unit::cHertzFormat, rate);
}

static bool WheelAdjustIndex(int* index, int count)
{
    const s32 wheelSteps = GetItemWheelSteps();
    if (wheelSteps == 0)
        return false;

    int next = *index + wheelSteps;
    next = std::max(0, std::min(next, count - 1));

    if (next == *index)
        return false;

    *index = next;
    return true;
}

f32 CalcLabelWidth(std::initializer_list<const char*> labels)
{
    f32 width = 0.0f;

    for (const char* label : labels)
        width = sead::Mathf::max(width, ImGui::CalcTextSize(label).x);

    return width + ImFloor(ImGui::GetFontSize() * cLabelGapEm);
}

void RowLabel(const char* label, f32 labelWidth, const char* detail)
{
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImFloor(ImGui::GetFontSize() * cRowGapEm));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    DetailTooltip(detail);
    ImGui::SameLine(0.0f, labelWidth - ImGui::CalcTextSize(label).x);
    ImGui::SetNextItemWidth(-FLT_MIN);
}

void BeginBoxTools(f32 width)
{
    const f32 top = ImGui::GetItemRectMin().y;

    ImGui::SameLine();
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x - width, top));
}

void EndBoxTools()
{
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImFloor(ImGui::GetFontSize() * cTitleGapEm));
}

void FillBox(f32 height)
{
    if (height <= 0.0f)
        return;

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + height - ImGui::GetStyle().ItemSpacing.y);
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

bool SampleField(const char* id, u32* value, u32 min, u32 max, u32 reset, u32 rate, bool hasValue)
{
    ImGui::SetNextItemWidth(ImFloor(ImGui::GetFontSize() * cFieldWidthEm));
    SetNextKnobReset(reset);

    if (!hasValue)
        SetNextKnobText(messages::common::cNoValue);

    const bool changed = DragField(id, ImGuiDataType_U32, value, sead::Mathf::max(1.0f, static_cast<f32>(max) / cSampleDragRange), &min, &max);

    if (hasValue)
    {
        sead::FixedSafeString<32> time;
        FormatSampleTime(&time, *value, rate);
        KnobTooltip("%s", time.cstr());
    }

    ImGui::SameLine();
    ImGui::TextDisabled("%s", messages::common::cSamples);

    return changed;
}

void WaveImportPanel::drawSourceHeader(const char* header, u32 sampleRate, u32 channelCount, u32 sampleCount)
{
    sead::FixedSafeString<32> channels;

    if (channelCount == 1)
        channels.copy(messages::common::cMono);
    else if (channelCount == 2)
        channels.copy(messages::common::cStereo);
    else
        channels.format(messages::common::cChannelCountFormat, channelCount);

    sead::FixedSafeString<32> length;
    FormatSampleTime(&length, sampleCount, sampleRate);

    const sead::FormatFixedSafeString<96> summary(messages::import::cWaveSourceSummaryFormat, sampleRate, channels.cstr(), length.cstr());

    if (header)
    {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const f32 width = ImGui::GetContentRegionAvail().x;
        const f32 summaryWidth = ImGui::CalcTextSize(summary.cstr()).x;
        const f32 headerRight = pos.x + sead::Mathf::max(0.0f, width - summaryWidth - ImGui::GetFontSize());

        ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), pos, ImVec2(headerRight, pos.y + ImGui::GetTextLineHeight()), headerRight, headerRight, header, nullptr, nullptr);
        ImGui::SetCursorScreenPos(ImVec2(pos.x + sead::Mathf::max(0.0f, width - summaryWidth), pos.y));
    }

    ImGui::TextColored(GetItemValueColor(), "%s", summary.cstr());
}

void WaveImportPanel::drawUndecodableSource(const char* header, WaveFile::Encoding* encoding, WaveFile::RiffWaveInfo* info)
{
    drawSourceHeader(header, info->sampleRate, info->numChannels, info->sampleCount);

    ImGui::TextDisabled("%s", messages::import::cWaveNoPreview);

    const f32 labelWidth = CalcLabelWidth({ messages::common::cEncoding, messages::common::cStart, messages::common::cEnd });

    if (BeginGroupBox(messages::common::cFormat))
    {
        drawEncodingField(encoding, labelWidth);
        EndGroupBox();
    }

    const f32 checkSize = ImGui::GetFrameHeight();

    if (!BeginGroupBoxWithTools(messages::common::cLoop, checkSize))
        return;

    BeginBoxTools(checkSize);

    bool changed = ImGui::Checkbox("##Loop", &info->isLoop);

    EndBoxTools();

    const u32 total = info->sampleCount;

    ImGui::BeginDisabled(!info->isLoop);

    RowLabel(messages::common::cStart, labelWidth);
    changed |= SampleField("##LoopStart", &info->loopStartFrame, 0, total > 0 ? total - 1 : 0, 0, info->sampleRate, info->isLoop);

    RowLabel(messages::common::cEnd, labelWidth);
    changed |= SampleField("##LoopEnd", &info->loopEndFrame, 1, total, total, info->sampleRate, info->isLoop);

    ImGui::EndDisabled();

    if (changed && total > 0)
    {
        info->loopEndFrame = sead::MathCalcCommon<u32>::clamp2(1, info->loopEndFrame, total);
        info->loopStartFrame = sead::MathCalcCommon<u32>::min(info->loopStartFrame, info->loopEndFrame - 1);
        mTouched = true;
    }

    EndGroupBox();
}

void WaveImportPanel::drawColumns(FrameContext& ctx)
{
    const f32 gap = ImFloor(ImGui::GetFontSize() * cColumnGapEm);
    const f32 rightLabelWidth = CalcLabelWidth({ messages::common::cStart, messages::common::cEnd, messages::common::cLength });

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(gap * 0.5f, 0.0f));

    if (ImGui::BeginTable("##Columns", 2, ImGuiTableFlags_SizingStretchSame))
    {
        ImGui::TableNextColumn();

        const f32 leftFill = mColumnFill[0];
        const f32 leftTop = ImGui::GetCursorPosY();
        drawFormatBox(ctx);
        drawProcessingPanels(leftFill);
        const f32 leftHeight = ImGui::GetCursorPosY() - leftTop - leftFill;

        ImGui::TableNextColumn();

        const f32 rightFill = mColumnFill[1];
        const f32 rightTop = ImGui::GetCursorPosY();
        drawTrimBox(ctx, rightLabelWidth);
        drawLoopBox(ctx, rightLabelWidth, rightFill);
        const f32 rightHeight = ImGui::GetCursorPosY() - rightTop - rightFill;

        mColumnFill[0] = sead::Mathf::max(0.0f, rightHeight - leftHeight);
        mColumnFill[1] = sead::Mathf::max(0.0f, leftHeight - rightHeight);

        ImGui::EndTable();
    }

    ImGui::PopStyleVar();
}

void WaveImportPanel::drawFormatBox(FrameContext& ctx)
{
    if (!BeginGroupBox(messages::common::cFormat))
        return;

    EndBoxTools();

    const f32 labelWidth = CalcLabelWidth({ messages::common::cEncoding, messages::common::cSampleRate, messages::common::cChannels });

    drawEncodingField(ctx.encoding, labelWidth);
    drawSampleRateField(labelWidth);
    drawChannelsField(labelWidth);

    EndGroupBox();
}

static void OffsetPanelBody(const ImVec2& offset)
{
    const ImVec2 pos = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(pos.x + offset.x, pos.y + offset.y));
}

static f32 CalcNormalizePanelWidth()
{
    const f32 headerWidth = ImGui::CalcTextSize(messages::import::cWaveNormalize).x + ImGui::GetStyle().ItemSpacing.x * 2.0f + ImGui::GetFrameHeight();

    return sead::Mathf::max(headerWidth, CalcKnobWidth(cNormalizeKnobs, IM_ARRAYSIZE(cNormalizeKnobs)));
}

void WaveImportPanel::drawPitchPanel()
{
    if (!DrawPitchKnobs(&mSettings.speedMultiplier, ImVec2(CalcKnobWidth(cPitchKnobs, IM_ARRAYSIZE(cPitchKnobs)), CalcRegionStripHeight())))
        return;

    mSettings.markShapeDirty();
    mTouched = true;
}

void WaveImportPanel::drawNormalizePanel()
{
    const sead::FormatFixedSafeString<16> levelText(mSettings.normalizeTargetDb > cNormalizeShortMin ? "%.1f" : "%.0f", mSettings.normalizeTargetDb);

    SetNextKnobReset(cNormalizeMax);
    SetNextKnobText(levelText.cstr());

    if (KnobF32(messages::import::cWaveNormalizeLevel, &mSettings.normalizeTargetDb, cNormalizeMin, cNormalizeMax, "%.1f", cNormalizeDragSpeed, ImVec2(CalcKnobWidth(cNormalizeKnobs, IM_ARRAYSIZE(cNormalizeKnobs)), CalcRegionStripHeight())))
        mTouched = true;

    KnobTooltip(messages::import::cWaveNormalizeFormat, mSettings.normalizeTargetDb);
}

void WaveImportPanel::drawProcessingPanels(f32 fill)
{
    const f32 widths[] = { CalcKnobRowWidth(cPitchKnobs, IM_ARRAYSIZE(cPitchKnobs)), CalcNormalizePanelWidth() };
    const f32 normalizeInset = ImFloor((widths[1] - CalcKnobWidth(cNormalizeKnobs, IM_ARRAYSIZE(cNormalizeKnobs))) * 0.5f);
    const f32 topFill = ImFloor(fill * 0.5f);

    DrawSoundPanels("ProcessingPanels", widths, IM_ARRAYSIZE(widths), [&](s32 index, f32 offset)
    {
        TintGroupBoxCell();

        if (index == 0)
        {
            RegionHeader(messages::common::cPitch);
            DetailTooltip(messages::import::cWavePitchTip);

            OffsetPanelBody(ImVec2(offset, topFill));
            drawPitchPanel();
        }
        else
        {
            if (DrawPanelHeader(messages::import::cWaveNormalize, &mSettings.normalizeEnabled, messages::import::cWaveNormalizeTip))
                mTouched = true;

            ImGui::BeginDisabled(!mSettings.normalizeEnabled);
            OffsetPanelBody(ImVec2(offset + normalizeInset, topFill));
            drawNormalizePanel();
            ImGui::EndDisabled();
        }

        FillBox(fill - topFill);
    });

    GroupBoxGap();
}

void WaveImportPanel::drawEncodingField(WaveFile::Encoding* encoding, f32 labelWidth)
{
    RowLabel(messages::common::cEncoding, labelWidth);

    if (ComboScroll("##Encoding", reinterpret_cast<s32*>(encoding), WaveFile::sEncodingTypes, cWaveEncodingDetails, IM_ARRAYSIZE(cWaveEncodingDetails)))
        mTouched = true;
}

void WaveImportPanel::drawSampleRateField(f32 labelWidth)
{
    u32 rates[1 + IM_ARRAYSIZE(cImportSampleRates)];
    int rateCount = 0;

    rates[rateCount++] = mSettings.pcm.sampleRate;

    for (u32 preset : cImportSampleRates)
        if (preset != mSettings.pcm.sampleRate)
            rates[rateCount++] = preset;

    int current = 0;

    for (int i = 0; i < rateCount; i++)
        if (rates[i] == mSettings.targetSampleRate)
            current = i;

    char preview[40];
    FormatRate(preview, sizeof(preview), mSettings.targetSampleRate, current == 0);

    RowLabel(messages::common::cSampleRate, labelWidth, messages::import::cWaveSampleRateTip);

    if (ImGui::BeginCombo("##SampleRate", preview))
    {
        for (int i = 0; i < rateCount; i++)
        {
            char label[40];
            FormatRate(label, sizeof(label), rates[i], i == 0);

            if (ImGui::Selectable(label, i == current))
            {
                mSettings.targetSampleRate = rates[i];
                mSettings.markShapeDirty();
            }
        }

        ImGui::EndCombo();
    }

    if (WheelAdjustIndex(&current, rateCount))
    {
        mSettings.targetSampleRate = rates[current];
        mSettings.markShapeDirty();
    }
}

void WaveImportPanel::drawChannelsField(f32 labelWidth)
{
    static const char* const cModeNames[] = { messages::import::cWaveChannelsKeepAll, messages::import::cWaveChannelsLeft, messages::import::cWaveChannelsRight, messages::import::cWaveChannelsMix };

    const s32 modeNum = IM_ARRAYSIZE(cModeNames);

    const auto selectItem = [&](s32 index)
    {
        mSettings.channelMode = static_cast<AudioProcessing::ChannelMode>(index);
        mSettings.markShapeDirty();
    };

    s32 current = static_cast<s32>(mSettings.channelMode);

    RowLabel(messages::common::cChannels, labelWidth);

    if (ImGui::BeginCombo("##Channels", cModeNames[current]))
    {
        for (s32 i = 0; i < modeNum; i++)
            if (ImGui::Selectable(cModeNames[i], i == current) && i != current)
                selectItem(i);

        ImGui::EndCombo();
    }

    if (WheelAdjustIndex(&current, modeNum))
        selectItem(current);
}

void WaveImportPanel::drawTrimBox(FrameContext& ctx, f32 labelWidth)
{
    const u32 total = ctx.totalSamples;
    const u32 rate = mSettings.workingSampleRate;
    const bool editable = ctx.cropEditable && total > 0;
    bool trimSilence = false;

    ImGui::BeginDisabled(!editable);

    if (!BeginGroupBox(messages::import::cWaveTrimTitle, messages::import::cWaveTrimSilence, &trimSilence))
    {
        ImGui::EndDisabled();
        return;
    }

    ImGui::SetItemTooltip("%s", messages::import::cWaveTrimSilenceTip);
    EndBoxTools();

    u32 start = ctx.cropStart;
    u32 end = ctx.cropEnd;

    RowLabel(messages::common::cStart, labelWidth, messages::import::cWaveTrimStartTip);
    bool fieldsChanged = SampleField("##TrimStart", &start, 0, total > 0 ? total - 1 : 0, 0, rate);

    RowLabel(messages::common::cEnd, labelWidth, messages::import::cWaveTrimEndTip);
    fieldsChanged |= SampleField("##TrimEnd", &end, 1, total, total, rate);

    if (fieldsChanged && editable)
    {
        start = std::min(start, total - 1);
        end = std::max(start + 1, std::min(end, total));
        ctx.cropStart = start;
        ctx.cropEnd = end;
        ctx.changed = true;
    }

    if (trimSilence && editable)
    {
        const SampleRange audible = mSettings.audibleRange();

        if (audible.end > audible.start)
        {
            ctx.cropStart = audible.start;
            ctx.cropEnd = audible.end;
            ctx.changed = true;
        }
    }

    // Length
    const u32 keptLength = ctx.cropEnd > ctx.cropStart ? ctx.cropEnd - ctx.cropStart : 0;

    sead::FixedSafeString<32> time;
    FormatSampleTime(&time, keptLength, rate);

    RowLabel(messages::common::cLength, labelWidth);
    ImGui::TextUnformatted(time.cstr());
    ImGui::SameLine(0.0f, ImGui::GetFontSize());
    ImGui::TextDisabled(messages::wavefile::cSamplesFormat, keptLength);

    EndGroupBox();
    ImGui::EndDisabled();
}

void WaveImportPanel::drawLoopModeField(FrameContext& ctx, f32 width)
{
    static const char* const cModeDetails[] = { nullptr, nullptr, nullptr };

    BeginBoxTools(width);
    ImGui::SetNextItemWidth(width);

    const char* loopModeNames[] = { messages::import::cWaveLoopModeDetect, messages::common::cDisabled, messages::common::cManual };
    s32 loopModeIndex = static_cast<s32>(mSettings.loopMode);

    if (ComboScroll("##LoopMode", &loopModeIndex, loopModeNames, cModeDetails, IM_ARRAYSIZE(loopModeNames)))
    {
        mSettings.loopMode = static_cast<LoopMode>(loopModeIndex);
        ctx.changed = true;

        if (mSettings.loopMode == LoopMode::DetectFromWav)
        {
            const SampleRange detected = mSettings.detectFromWavLoop(ctx.totalSamples);
            ctx.loopStart = detected.start;
            ctx.loopEnd = detected.end;
        }
    }

    EndBoxTools();
}

void WaveImportPanel::drawLoopRangeFields(FrameContext& ctx, f32 labelWidth)
{
    const u32 total = ctx.totalSamples;
    const u32 rate = mSettings.workingSampleRate;
    const bool manual = mSettings.loopMode == LoopMode::Manual;
    const bool hasLoop = mSettings.hasLoop();

    u32 start = ctx.loopStart;
    u32 end = ctx.loopEnd;

    ImGui::BeginGroup();

    RowLabel(messages::common::cStart, labelWidth, manual ? messages::import::cWaveLoopStartTip : nullptr);
    bool fieldsChanged = SampleField("##LoopStart", &start, 0, total > 0 ? total - 1 : 0, ctx.cropStart, rate, hasLoop);

    RowLabel(messages::common::cEnd, labelWidth, manual && !mSettings.curveEnabled ? messages::import::cWaveLoopEndTip : nullptr);
    fieldsChanged |= SampleField("##LoopEnd", &end, 1, total, ctx.cropEnd, rate, hasLoop);

    ImGui::EndGroup();

    if (fieldsChanged && manual && total > 0)
    {
        start = std::min(start, total - 1);
        end = std::max(start + 1, std::min(end, total));
        ctx.loopStart = start;
        ctx.loopEnd = end;
        ctx.changed = true;
    }
}

void WaveImportPanel::drawCrossfadeKnob(FrameContext& ctx)
{
    static const char* const cKnobLabels[] = { messages::import::cWaveCrossfade };

    const u32 rate = mSettings.workingSampleRate;
    const u32 loopLength = (ctx.loopEnd > ctx.loopStart) ? (ctx.loopEnd - ctx.loopStart) : 0;
    const u32 crossfadeMaxSamples = std::min(ctx.loopStart - std::min(ctx.cropStart, ctx.loopStart), loopLength > 0 ? loopLength - 1 : 0);
    const f32 crossfadeMaxMs = std::max(1.0f, rate ? static_cast<f32>(crossfadeMaxSamples) / rate * 1000.0f : 1.0f);
    const ImVec2 knobSize(CalcKnobWidth(cKnobLabels, IM_ARRAYSIZE(cKnobLabels)), CalcRegionStripHeight());

    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + sead::Mathf::max(0.0f, ImGui::GetContentRegionAvail().x - knobSize.x));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImFloor(ImGui::GetFontSize() * cRowGapEm));
    SetNextKnobReset(0.0);

    if (KnobF32(messages::import::cWaveCrossfade, &mWaveformState.crossfadeMs, 0.0f, crossfadeMaxMs, "%.0f", sead::Mathf::max(cCrossfadeDragSpeedMin, crossfadeMaxMs / cCrossfadeDragRange), knobSize))
        ctx.changed = true;

    KnobTooltip(messages::common::cMillisecondsFormat, mWaveformState.crossfadeMs);
}

void WaveImportPanel::drawLoopTools(FrameContext& ctx)
{
    const ImGuiStyle& style = ImGui::GetStyle();

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImFloor(ImGui::GetFontSize() * cTitleGapEm));

    if (ImGui::Button(messages::import::cWaveSuggestLoop))
    {
        const std::vector<float> kept(mSettings.mono.begin() + ctx.cropStart, mSettings.mono.begin() + ctx.cropEnd);
        LoopAnalysis::SuggestResult suggestion = LoopAnalysis::suggestLoop(kept, mSettings.workingSampleRate);

        if (suggestion.ok)
        {
            ctx.loopStart = ctx.cropStart + suggestion.loopStart;
            ctx.loopEnd = ctx.cropStart + suggestion.loopEnd;
            ImGui::LoopWaveformZoomToLoop(mWaveformState, ctx.loopStart, ctx.loopEnd, ctx.totalSamples);
            ctx.changed = true;
        }
    }
    ImGui::SetItemTooltip("%s", messages::import::cWaveSuggestLoopTip);

    ImGui::SameLine();
    if (ImGui::Button(messages::import::cWaveSnapLoop))
    {
        const u32 base = std::min(ctx.cropStart, ctx.loopStart);
        LoopAnalysis::FrameSnapResult snap = LoopAnalysis::snapFramePeriod(ctx.loopStart - base, ctx.loopEnd - base, mSettings.period, ctx.cropEnd - base);
        ctx.loopStart = base + snap.loopStart;
        ctx.loopEnd = base + snap.loopEnd;
        ctx.changed = true;
    }
    ImGui::SetItemTooltip("%s", messages::import::cWaveSnapLoopTip);

    const f32 checkWidth = ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + ImGui::CalcTextSize(messages::import::cWaveEqualPower).x;

    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + sead::Mathf::max(0.0f, ImGui::GetContentRegionAvail().x - checkWidth));

    if (ImGui::Checkbox(messages::import::cWaveEqualPower, &mWaveformState.equalPowerCurve))
        ctx.changed = true;

    DetailTooltip(messages::import::cWaveEqualPowerTip);
}

void WaveImportPanel::drawLoopBox(FrameContext& ctx, f32 labelWidth, f32 fill)
{
    const f32 modeWidth = ImFloor(ImGui::GetFontSize() * cLoopModeWidthEm);

    if (!BeginGroupBoxWithTools(messages::common::cLoop, modeWidth))
        return;

    drawLoopModeField(ctx, modeWidth);

    ImGui::BeginDisabled(mSettings.loopMode != LoopMode::Manual || ctx.totalSamples == 0);

    drawLoopRangeFields(ctx, labelWidth);
    drawCrossfadeKnob(ctx);
    drawLoopTools(ctx);

    ImGui::EndDisabled();

    FillBox(fill);
    EndGroupBox();
}

}
