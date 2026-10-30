#include <ui/wave/WaveImportPanelInternal.h>

#include <aal/Amta.h>
#include <ui/items/ItemColumns.h>
#include <messages/Messages.h>
#include <ui/widgets/Properties.h>
#include <ui/widgets/Widgets.h>

#include <algorithm>

namespace waveimport
{

namespace
{

struct KeepRow
{
    const char* label = nullptr;
    const char* detail = nullptr;
    const char* keepDetail = nullptr;
    const char* clearDetail = nullptr;
    const char* reason = nullptr;
    const char* fact = nullptr;
    bool* isCleared = nullptr;
    s32 reasonChoice = 1;
};

}

static constexpr s32 cKeepRowMax = 4;
static constexpr s32 cChoiceKeep = 0;
static constexpr s32 cChoiceClear = 1;

static u32 CountEndsAfter(const std::vector<u32>& ends, u32 sampleCount)
{
    return static_cast<u32>(std::count_if(ends.begin(), ends.end(), [sampleCount](u32 end) { return end > sampleCount; }));
}

static void FormatCount(sead::BufferedSafeString* out, size_t count, const char* format, const char* oneFormat)
{
    out->format(count == 1 ? oneFormat : format, static_cast<u32>(count));
}

static void DrawKeepRow(const KeepRow& row, f32 labelWidth)
{
    static const char* const cChoices[] = { messages::common::cKeep, messages::common::cClear };

    const char* details[] = { row.keepDetail, row.clearDetail };
    s32 selected = row.reason ? row.reasonChoice : *row.isCleared ? cChoiceClear : cChoiceKeep;

    RowLabel(row.label, labelWidth, row.detail);

    ImGui::PushID(row.label);
    ImGui::BeginDisabled(row.reason != nullptr);
    ImGui::BeginGroup();

    if (DrawChoiceRow(&selected, cChoices, IM_ARRAYSIZE(cChoices), row.reason ? nullptr : details))
        *row.isCleared = selected == cChoiceClear;

    ImGui::EndGroup();
    ImGui::EndDisabled();

    if (row.reason)
        ImGui::SetItemTooltip("%s", row.reason);

    if (row.fact)
    {
        const f32 factWidth = ImGui::CalcTextSize(row.fact).x;

        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + sead::Mathf::max(0.0f, ImGui::GetContentRegionAvail().x - factWidth));
        ImGui::TextColored(GetItemValueColor(), "%s", row.fact);
    }

    ImGui::PopID();
}

u32 WaveImportPanel::calcOutputChannelCount(const WaveFile::RiffWaveInfo& info) const
{
    if (!mSettings.pcm.isValid())
        return info.numChannels;

    return mSettings.channelMode == AudioProcessing::ChannelMode::Stereo ? static_cast<u32>(mSettings.pcm.channels.size()) : 1;
}

void WaveImportPanel::drawOldDataBox(const WaveFile::RiffWaveInfo& info)
{
    if (!mOldData || !mOldClear || mOldData->isEmpty())
        return;

    sead::FixedSafeString<32> markerFact;
    sead::FixedSafeString<32> regionFact;
    sead::FixedSafeString<32> tempoFact;

    KeepRow rows[cKeepRowMax];
    s32 rowCount = 0;
    const char* metaReason = mOldData->canRewriteMeta ? nullptr : messages::resource::cMarkersLayoutUnknown;

    if (!mOldData->markerEnds.empty())
    {
        FormatCount(&markerFact, mOldData->markerEnds.size(), messages::import::cWaveMarkerCountFormat, messages::import::cWaveMarkerCountOneFormat);
        rows[rowCount++] = { messages::resource::cMarkers, messages::import::cWaveMarkersDetail, messages::import::cWaveMarkersKeepDetail, nullptr, metaReason, markerFact.cstr(), &mOldClear->markers, cChoiceKeep };
    }

    if (!mOldData->regionEnds.empty())
    {
        FormatCount(&regionFact, mOldData->regionEnds.size(), messages::import::cWaveRegionCountFormat, messages::import::cWaveRegionCountOneFormat);
        rows[rowCount++] = { messages::common::cRegions, messages::import::cWaveRegionsDetail, messages::import::cWaveRegionsKeepDetail, nullptr, nullptr, regionFact.cstr(), &mOldClear->regions };
    }

    if (mOldData->hasBeatGrid)
    {
        if (mOldData->tempo > 0.0f)
            tempoFact.format(messages::unit::cBpmFormat, mOldData->tempo);

        rows[rowCount++] = { messages::import::cWaveBeatGrid, messages::import::cWaveBeatGridDetail, messages::import::cWaveBeatGridKeepDetail, nullptr, metaReason, tempoFact.isEmpty() ? nullptr : tempoFact.cstr(), &mOldClear->beatGrid, cChoiceKeep };
    }

    if (mOldData->isMidSide)
    {
        const char* reason = calcOutputChannelCount(info) != 2 ? messages::import::cWaveMidSideStereoReason : nullptr;
        rows[rowCount++] = { messages::import::cWaveMidSide, messages::import::cWaveMidSideDetail, messages::import::cWaveMidSideKeepDetail, messages::import::cWaveMidSideClearDetail, reason, nullptr, &mOldClear->midSide };
    }

    if (!BeginGroupBox(messages::import::cWaveOldDataTitle))
        return;

    EndBoxTools();

    const f32 labelWidth = CalcLeftLabelWidth();

    for (s32 i = 0; i < rowCount; i++)
        DrawKeepRow(rows[i], labelWidth);

    EndGroupBox();
}

const char* WaveImportPanel::formatOldDataWarning(const WaveFile::RiffWaveInfo& info, sead::BufferedSafeString* out) const
{
    if (!mOldData || !mOldClear)
        return nullptr;

    const u32 outputLength = calcOutputLength(info);
    const u32 markerLimit = aal::CalcAmtaMarkerLimit(mOldData->hasSourceRate, { info.sampleRate, info.sampleCount }, calcOutputSampleRate(info), outputLength);
    const u32 regionsPastEnd = mOldClear->regions ? 0 : CountEndsAfter(mOldData->regionEnds, outputLength);
    const u32 markersPastEnd = mOldClear->markers || !mOldData->canRewriteMeta ? 0 : CountEndsAfter(mOldData->markerEnds, markerLimit);

    if (regionsPastEnd != 0)
    {
        FormatCount(out, regionsPastEnd, messages::stream::cRegionsPastEndFormat, messages::stream::cRegionsPastEndOneFormat);
        return out->cstr();
    }

    if (markersPastEnd != 0)
    {
        FormatCount(out, markersPastEnd, messages::import::cWaveMarkersPastEndFormat, messages::import::cWaveMarkersPastEndOneFormat);
        return out->cstr();
    }

    const bool keepsMarkers = !mOldData->markerEnds.empty() && !mOldClear->markers;
    const bool keepsRegions = !mOldData->regionEnds.empty() && !mOldClear->regions;
    const bool keepsBeatGrid = mOldData->hasBeatGrid && !mOldClear->beatGrid;

    return keepsMarkers || keepsRegions || keepsBeatGrid ? messages::import::cWaveOldDataWarning : nullptr;
}

}
