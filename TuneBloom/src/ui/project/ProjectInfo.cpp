#include <ui/project/ProjectViews.h>

#include <ui/file/DocumentState.h>
#include <ui/file/File.h>
#include <ui/widgets/Knob.h>
#include <messages/Messages.h>
#include <ui/widgets/PanelLayout.h>
#include <ui/widgets/Properties.h>
#include <ui/file/UnsavedChanges.h>
#include <ui/widgets/Widgets.h>

#include <imgui/imgui_internal.h>

#include <cstdio>

static constexpr f32 cProjectLimitFieldEm     = 3.0f;
static constexpr f32 cProjectCardGapEm        = 1.0f;
static constexpr f32 cProjectLabelGapEm       = 1.0f;
static constexpr f32 cProjectRowFieldEm       = 6.0f;
static constexpr s32 cProjectFormatRowNum     = 4;
static constexpr u16 cProjectLimitMax         = 999;
static constexpr u16 cProjectStreamChannelMax = 32;
static constexpr u16 cProjectBufferTimesMin   = 1;
static constexpr u16 cProjectBufferTimesMax   = 4;

static constexpr const char* cOptionsWidthSample = "4294967295";

namespace
{

enum class LimitPanel
{
    Sequence,
    Stream,
    Wave,

    Num
};

struct LimitLayout
{
    static constexpr s32 cLabelMax = 4;

    SoundArchivePlayerInfo* playerInfo;
    const char* labels[cLabelMax];
    s32 labelNum;
    bool hasBufferTimes;
    f32 fieldWidth;
    f32 slotWidth;
    f32 slotHeight;
    f32 gap;

    s32 getSlotNum(LimitPanel panel) const
    {
        return panel == LimitPanel::Stream ? labelNum : 2;
    }

    f32 calcPanelWidth(LimitPanel panel) const
    {
        const s32 slotNum = getSlotNum(panel);

        return slotWidth * slotNum + gap * (slotNum - 1);
    }
};

struct LimitLine
{
    const LimitPanel* panels;
    s32 panelNum;
};

struct LimitForm
{
    const LimitLine* lines;
    s32 lineNum;
};

}

static constexpr s32 cLimitPanelNum = static_cast<s32>(LimitPanel::Num);

static const LimitPanel cAllPanels[]     = { LimitPanel::Sequence, LimitPanel::Stream, LimitPanel::Wave };
static const LimitPanel cPairPanels[]    = { LimitPanel::Sequence, LimitPanel::Wave };
static const LimitPanel cSequencePanel[] = { LimitPanel::Sequence };
static const LimitPanel cStreamPanel[]   = { LimitPanel::Stream };
static const LimitPanel cWavePanel[]     = { LimitPanel::Wave };

static const LimitLine cOneLine[]    = { { cAllPanels, 3 } };
static const LimitLine cTwoLines[]   = { { cPairPanels, 2 }, { cStreamPanel, 1 } };
static const LimitLine cThreeLines[] = { { cSequencePanel, 1 }, { cStreamPanel, 1 }, { cWavePanel, 1 } };

static const LimitForm cLimitForms[] = {
    { cOneLine, 1 },
    { cTwoLines, 2 },
    { cThreeLines, 3 }
};

static const char* const cFormatLabels[cProjectFormatRowNum] = {
    messages::project::cPlatform,
    messages::common::cVersion,
    messages::project::cStringTable,
    messages::common::cOptions
};

static void DrawFormatRow(const char* label, f32 labelWidth, bool stackRows)
{
    if (!stackRows)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(-FLT_MIN);
        return;
    }

    ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(-FLT_MIN);
}

static void CardGap(f32 gap)
{
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + gap - ImGui::GetStyle().ItemSpacing.y);
}

static u32 sVersionForEntry[IM_ARRAYSIZE(cFormatTable)] = {};
static u32 sVersionDocument = 0;

static void SyncEntryVersionsToDocument()
{
    if (sVersionDocument == ActiveDocumentState().serial)
        return;

    sVersionDocument = ActiveDocumentState().serial;

    for (int i = 0; i < IM_ARRAYSIZE(cFormatTable); i++)
        sVersionForEntry[i] = cFormatTable[i].defaultVersion;
}

static void SetArchiveVersion(Bfsar& archive, u32 version)
{
    archive.setVersion(version);

    u8& bufferTimes = archive.getSoundArchivePlayerInfo().streamBufferTimes;

    if (archive.isStreamPrefetchAvailable() && bufferTimes < cProjectBufferTimesMin)
        bufferTimes = cProjectBufferTimesMin;
}

static const char* const* GetPlatformLabels()
{
    static char sLabels[IM_ARRAYSIZE(cFormatTable)][64];
    static const char* sPointers[IM_ARRAYSIZE(cFormatTable)];

    static const bool sInit = []
    {
        for (int i = 0; i < IM_ARRAYSIZE(cFormatTable); i++)
        {
            snprintf(sLabels[i], sizeof(sLabels[i]), "%s (%s)", cFormatTable[i].fmtName, cFormatTable[i].systemName);
            sPointers[i] = sLabels[i];
        }

        return true;
    }();

    (void)sInit;

    return sPointers;
}

static f32 CalcPlatformComboWidth()
{
    const char* const* labels = GetPlatformLabels();

    f32 width = 0.0f;
    for (int i = 0; i < IM_ARRAYSIZE(cFormatTable); i++)
        width = sead::Mathf::max(width, ImGui::CalcTextSize(labels[i]).x);

    return width + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2.0f;
}

static f32 CalcOptionsFieldWidth()
{
    return ImGui::CalcTextSize(cOptionsWidthSample).x + ImGui::GetStyle().FramePadding.x * 2.0f;
}

static f32 CalcFormatFieldWidth()
{
    return sead::Mathf::max(CalcPlatformComboWidth(), CalcVersionUIWidth());
}

static f32 CalcLabelColumnWidth(const char* const* labels, s32 labelNum)
{
    f32 width = 0.0f;
    for (s32 i = 0; i < labelNum; i++)
        width = sead::Mathf::max(width, ImGui::CalcTextSize(labels[i]).x);

    return width + ImFloor(ImGui::GetFontSize() * cProjectLabelGapEm);
}

static f32 CalcFormatCellLabelWidth(s32 columnNum, s32 cell)
{
    if (columnNum == 1)
        return CalcLabelColumnWidth(cFormatLabels, cProjectFormatRowNum);

    return CalcLabelColumnWidth(cFormatLabels + cell * 2, 2);
}

static void CalcFormatCellWidths(s32 columnNum, f32* widths)
{
    const f32 padding = CalcCardPadX() * 2.0f;

    if (columnNum == 1)
    {
        widths[0] = CalcFormatCellLabelWidth(1, 0) + sead::Mathf::max(CalcFormatFieldWidth(), CalcOptionsFieldWidth()) + padding;
        return;
    }

    widths[0] = CalcFormatCellLabelWidth(2, 0) + CalcFormatFieldWidth() + padding;
    widths[1] = CalcFormatCellLabelWidth(2, 1) + sead::Mathf::max(ImGui::GetFrameHeight(), CalcOptionsFieldWidth()) + padding;
}

static f32 CalcFormatCardWidth(s32 columnNum)
{
    f32 widths[2];
    CalcFormatCellWidths(columnNum, widths);

    f32 width = columnNum + 1.0f;
    for (s32 i = 0; i < columnNum; i++)
        width += widths[i];

    return width;
}

static bool IsFormatCellStacked(f32 labelWidth)
{
    return ImGui::GetContentRegionAvail().x < labelWidth + ImFloor(ImGui::GetFontSize() * cProjectRowFieldEm);
}

static f32 CalcFormatContentHeight()
{
    return ImGui::GetFrameHeight() * cProjectFormatRowNum + ImGui::GetStyle().ItemSpacing.y * (cProjectFormatRowNum - 1);
}

static void DrawPlatformRow(f32 labelWidth, bool stackRows)
{
    Bfsar& archive = ActiveSoundArchive();

    SyncEntryVersionsToDocument();

    const ArchiveFormatInfo* current = getFormatInfo(archive.getFormat(), archive.getPlatform());

    s32 selected = current ? static_cast<s32>(current - cFormatTable) : 0;
    sVersionForEntry[selected] = archive.getVersion();

    DrawFormatRow(messages::project::cPlatform, labelWidth, stackRows);

    if (!stackRows)
        ImGui::SetNextItemWidth(CalcFormatFieldWidth());

    if (!ComboScroll("##Platform", &selected, GetPlatformLabels(), IM_ARRAYSIZE(cFormatTable)))
        return;

    const ArchiveFormatInfo& info = cFormatTable[selected];

    archive.setFormat(info.format);
    archive.setPlatform(info.platform);
    archive.setEndian(info.endian);
    SetArchiveVersion(archive, sVersionForEntry[selected]);

    SetUnsavedChanges(true);
}

static void DrawFormatCard(s32 columnNum, f32 minContentHeight)
{
    f32 weights[2];
    CalcFormatCellWidths(columnNum, weights);

    if (!BeginCard(messages::common::cFormat, nullptr, columnNum, weights, minContentHeight))
        return;

    Bfsar& archive = ActiveSoundArchive();

    f32 labelWidth = CalcFormatCellLabelWidth(columnNum, 0);
    bool stackRows = IsFormatCellStacked(labelWidth);

    DrawPlatformRow(labelWidth, stackRows);

    {
        u32 version = archive.getVersion();

        DrawFormatRow(messages::common::cVersion, labelWidth, stackRows);
        if (DrawVersionUI(&version, archive.getFormat(), false))
        {
            SetArchiveVersion(archive, version);
            SetUnsavedChanges(true);
        }
    }

    if (columnNum > 1)
    {
        ImGui::TableNextColumn();

        labelWidth = CalcFormatCellLabelWidth(columnNum, 1);
        stackRows = IsFormatCellStacked(labelWidth);
    }

    {
        bool stringTable = archive.isIncludeStringTable();
        DrawFormatRow(messages::project::cStringTable, labelWidth, stackRows);
        DetailTooltip(messages::project::cStringTableDetail);

        const bool stringTableChanged = ImGui::Checkbox("##IncludeStringTable", &stringTable);
        DetailTooltip(messages::project::cStringTableDetail);

        if (stringTableChanged)
        {
            archive.setIncludeStringTable(stringTable);
            SetUnsavedChanges(true);
        }
    }

    {
        DrawFormatRow(messages::common::cOptions, labelWidth, stackRows);
        DetailTooltip(messages::project::cOptionsDetail);

        if (!stackRows)
            ImGui::SetNextItemWidth(CalcOptionsFieldWidth());

        const bool optionsChanged = ImGui::InputScalar("##Options", ImGuiDataType_U32, &archive.getSoundArchivePlayerInfo().options);
        DetailTooltip(messages::project::cOptionsDetail);

        if (optionsChanged)
            SetUnsavedChanges(true);
    }

    EndCard();
}

static LimitLayout CalcLimitLayout(f32 em)
{
    Bfsar& archive = ActiveSoundArchive();

    LimitLayout layout = {};

    layout.hasBufferTimes = archive.isStreamPrefetchAvailable();

    layout.playerInfo = &archive.getSoundArchivePlayerInfo();
    layout.labels[0] = messages::common::cSounds;
    layout.labels[1] = messages::common::cTracks;
    layout.labels[2] = messages::common::cChannels;
    layout.labels[3] = messages::project::cLimitBufferTimes;
    layout.labelNum = layout.hasBufferTimes ? 4 : 3;

    layout.fieldWidth = ImFloor(em * cProjectLimitFieldEm);
    layout.slotHeight = CalcSoundSlotHeight();
    layout.gap = CalcKnobGap();

    layout.slotWidth = layout.fieldWidth;
    for (s32 i = 0; i < layout.labelNum; i++)
        layout.slotWidth = sead::Mathf::max(layout.slotWidth, ImGui::CalcTextSize(layout.labels[i]).x);

    return layout;
}

static f32 CalcLimitLineWidth(const LimitLayout& layout, const LimitLine& line)
{
    f32 width = 1.0f;

    for (s32 i = 0; i < line.panelNum; i++)
        width += layout.calcPanelWidth(line.panels[i]) + CalcCardPadX() * 2.0f + 1.0f;

    return width;
}

static f32 CalcLimitFormWidth(const LimitLayout& layout, const LimitForm& form)
{
    f32 width = 0.0f;

    for (s32 i = 0; i < form.lineNum; i++)
        width = sead::Mathf::max(width, CalcLimitLineWidth(layout, form.lines[i]));

    return width;
}

static const LimitForm& SelectLimitForm(const LimitLayout& layout, f32 avail)
{
    for (const LimitForm& form : cLimitForms)
    {
        if (CalcLimitFormWidth(layout, form) <= avail)
            return form;
    }

    return cLimitForms[IM_ARRAYSIZE(cLimitForms) - 1];
}

static f32 CalcLimitsContentHeight(const LimitLayout& layout)
{
    return ImGui::GetFrameHeightWithSpacing() + layout.slotHeight;
}

static void DrawLimitField(const LimitLayout& layout, const char* id, u16& value, u16 min, u16 max)
{
    const u16 oldValue = value;

    ImGui::PushID(id);
    ImGui::SetNextItemWidth(layout.fieldWidth);

    if (DragField("##value", ImGuiDataType_U16, &value, cPropertyDragSpeed, &min, &max))
        value = sead::MathCalcCommon<u16>::clamp2(min, value, max);

    ImGui::PopID();

    if (value != oldValue)
        SetUnsavedChanges(true);
}

static void DrawLimitSlot(const LimitLayout& layout, const char* label, const char* detail, u16& value, u16 min, u16 max)
{
    DrawSoundSlot(label, layout.slotWidth, ImVec2(layout.fieldWidth, ImGui::GetFrameHeight()), layout.slotHeight, [&] { DrawLimitField(layout, label, value, min, max); });
    DetailTooltip(detail);
}

static void NextLimitSlot(const LimitLayout& layout)
{
    ImGui::SameLine(0.0f, layout.gap);
    if (ImGui::GetContentRegionAvail().x < layout.slotWidth)
        ImGui::NewLine();
}

static const char* GetLimitPanelTitle(LimitPanel panel)
{
    switch (panel)
    {
        case LimitPanel::Sequence:
            return messages::common::cSequence;

        case LimitPanel::Stream:
            return messages::common::cStream;

        default:
            return messages::common::cWave;
    }
}

static void DrawLimitPanel(const LimitLayout& layout, LimitPanel panel, f32 slotOffsetY)
{
    SoundArchivePlayerInfo& playerInfo = *layout.playerInfo;

    ImGui::PushID(static_cast<s32>(panel));

    RegionHeader(GetLimitPanelTitle(panel));

    if (slotOffsetY > 0.0f)
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + slotOffsetY);

    const f32 offset = ImFloor((ImGui::GetContentRegionAvail().x - layout.calcPanelWidth(panel)) * 0.5f);
    if (offset > 0.0f)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);

    switch (panel)
    {
        case LimitPanel::Sequence:
            DrawLimitSlot(layout, messages::common::cSounds, messages::player::cSoundLimitDetail, playerInfo.sequenceSoundMax, 0, cProjectLimitMax);
            NextLimitSlot(layout);
            DrawLimitSlot(layout, messages::common::cTracks, messages::project::cLimitTracksDetail, playerInfo.sequenceTrackMax, 0, cProjectLimitMax);
            break;

        case LimitPanel::Stream:
            DrawLimitSlot(layout, messages::common::cSounds, messages::player::cSoundLimitDetail, playerInfo.streamSoundMax, 0, cProjectLimitMax);
            NextLimitSlot(layout);
            DrawLimitSlot(layout, messages::common::cTracks, messages::project::cLimitTracksDetail, playerInfo.streamTrackMax, 0, cProjectLimitMax);
            NextLimitSlot(layout);
            DrawLimitSlot(layout, messages::common::cChannels, messages::project::cLimitChannelsDetail, playerInfo.streamChannelMax, 0, cProjectStreamChannelMax);

            if (layout.hasBufferTimes)
            {
                u16 streamBufferTimes = playerInfo.streamBufferTimes;

                NextLimitSlot(layout);
                DrawLimitSlot(layout, messages::project::cLimitBufferTimes, messages::project::cLimitBufferTimesDetail, streamBufferTimes, cProjectBufferTimesMin, cProjectBufferTimesMax);

                playerInfo.streamBufferTimes = static_cast<u8>(streamBufferTimes);
            }
            break;

        case LimitPanel::Wave:
            DrawLimitSlot(layout, messages::common::cSounds, messages::player::cSoundLimitDetail, playerInfo.waveSoundMax, 0, cProjectLimitMax);
            NextLimitSlot(layout);
            DrawLimitSlot(layout, messages::common::cTracks, messages::project::cLimitTracksDetail, playerInfo.waveTrackMax, 0, cProjectLimitMax);
            break;

        default:
            break;
    }

    ImGui::PopID();
}

static void DrawLimitsCard(const LimitLayout& layout, const LimitForm& form, f32 minContentHeight)
{
    const f32 slotOffsetY = ImFloor(sead::Mathf::max(0.0f, minContentHeight - CalcLimitsContentHeight(layout)) * 0.5f);

    for (s32 lineIndex = 0; lineIndex < form.lineNum; lineIndex++)
    {
        const LimitLine& line = form.lines[lineIndex];

        f32 weights[cLimitPanelNum];
        for (s32 i = 0; i < line.panelNum; i++)
            weights[i] = layout.calcPanelWidth(line.panels[i]);

        const bool open = lineIndex == 0 ? BeginCard(messages::project::cLimitsTitle, nullptr, line.panelNum, weights, minContentHeight) : NextCardLine(lineIndex, line.panelNum, weights);

        if (!open)
            return;

        for (s32 i = 0; i < line.panelNum; i++)
        {
            if (i > 0)
                ImGui::TableNextColumn();

            DrawLimitPanel(layout, line.panels[i], slotOffsetY);
        }
    }

    EndCard();
}

static void DrawTopCardsInLine(const LimitLayout& layout, f32 formatWidth, f32 gap)
{
    const f32 em = ImGui::GetFontSize();
    const f32 contentHeight = sead::Mathf::max(CalcFormatContentHeight(), CalcLimitsContentHeight(layout));

    const PageColumn column = BeginPageColumn((formatWidth + 0.5f) / em);

    ImGui::BeginGroup();
    DrawFormatCard(1, contentHeight);
    ImGui::EndGroup();

    EndPageColumn(column);

    ImGui::SameLine(0.0f, gap);

    ImGui::BeginGroup();
    DrawLimitsCard(layout, cLimitForms[0], contentHeight);
    ImGui::EndGroup();
}

void DrawProjectInfoUI()
{
    const f32 em = ImGui::GetFontSize();
    const f32 gap = ImFloor(em * cProjectCardGapEm);
    const LimitLayout layout = CalcLimitLayout(em);

    const f32 avail = ImGui::GetContentRegionAvail().x;
    const f32 formatWidth = CalcFormatCardWidth(1);
    const f32 limitsWidth = CalcLimitFormWidth(layout, cLimitForms[0]);

    if (formatWidth + gap + limitsWidth <= avail)
    {
        DrawTopCardsInLine(layout, formatWidth, gap);
    }
    else
    {
        DrawFormatCard(CalcFormatCardWidth(2) <= avail ? 2 : 1, 0.0f);
        CardGap(gap);
        DrawLimitsCard(layout, SelectLimitForm(layout, avail), 0.0f);
    }

    CardGap(gap);
    DrawFileStatisticsCard();
}
