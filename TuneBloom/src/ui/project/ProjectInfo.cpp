#include <ui/project/ProjectViews.h>

#include <ui/file/File.h>
#include <ui/widgets/Knob.h>
#include <ui/Messages.h>
#include <ui/widgets/PanelLayout.h>
#include <ui/widgets/Properties.h>
#include <ui/file/UnsavedChanges.h>
#include <ui/widgets/Widgets.h>

#include <imgui/imgui_internal.h>

#include <cstdio>

static constexpr f32 cProjectLimitFieldEm     = 3.5f;
static constexpr f32 cProjectColumnEm         = 30.0f;
static constexpr f32 cProjectTitleGapEm       = 0.3f;
static constexpr f32 cProjectRowFieldEm       = 6.0f;
static constexpr u16 cProjectLimitMax         = 999;
static constexpr u16 cProjectStreamChannelMax = 32;
static constexpr u16 cProjectBufferTimesMin   = 1;
static constexpr u16 cProjectBufferTimesMax   = 4;

namespace
{

enum class LimitPanel
{
    Sequence,
    Stream,
    Wave
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

    f32 calcPanelWidth(s32 slotNum) const
    {
        return slotWidth * slotNum + gap * (slotNum - 1);
    }
};

}

static void DrawProjectRow(const char* label, bool stackRows)
{
    if (!stackRows)
    {
        PropertyRow(label);
        return;
    }

    ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(-FLT_MIN);
}

static void DrawProjectPlatformUI(f32 em)
{
    static char sLabels[IM_ARRAYSIZE(cFormatTable)][64];
    static u32 sVersionForEntry[IM_ARRAYSIZE(cFormatTable)] = {};
    const char *labels[IM_ARRAYSIZE(cFormatTable)];

    static const bool sInit = [&]
    {
        for (int i = 0; i < IM_ARRAYSIZE(cFormatTable); i++)
        {
            snprintf(sLabels[i], sizeof(sLabels[i]), "%s (%s)", cFormatTable[i].fmtName, cFormatTable[i].systemName);
            sVersionForEntry[i] = cFormatTable[i].defaultVersion;
        }
        return true;
    }();

    for (int i = 0; i < IM_ARRAYSIZE(cFormatTable); i++)
        labels[i] = sLabels[i];

    const ArchiveFormatInfo *current = getFormatInfo(sBfsar.getFormat(), sBfsar.getPlatform());

    s32 selected = current ? (s32)(current - cFormatTable) : 0;
    sVersionForEntry[selected] = sBfsar.getVersion();

    f32 platformWidth = 0.0f;
    for (int i = 0; i < IM_ARRAYSIZE(cFormatTable); i++)
        platformWidth = sead::Mathf::max(platformWidth, ImGui::CalcTextSize(labels[i]).x);

    const f32 comboWidth = platformWidth + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2.0f + em;
    const f32 titleWidth = ImGui::CalcTextSize(messages::project::cPlatformTitle).x + ImGui::GetStyle().ItemSpacing.x * 2.0f + em;

    if (titleWidth + comboWidth <= ImGui::GetContentRegionAvail().x)
    {
        const ImVec2 headerPos = ImGui::GetCursorScreenPos();
        const f32 comboX = headerPos.x + ImGui::GetContentRegionAvail().x - comboWidth;

        RegionHeader(messages::project::cPlatformTitle, comboWidth + ImGui::GetStyle().ItemSpacing.x);

        ImGui::SameLine();
        ImGui::SetCursorScreenPos(ImVec2(comboX, headerPos.y));
        ImGui::SetNextItemWidth(comboWidth);
    }
    else
    {
        RegionHeader(messages::project::cPlatformTitle);
        ImGui::SetNextItemWidth(-FLT_MIN);
    }

    const bool platformChanged = ComboScroll("##Platform", &selected, labels, IM_ARRAYSIZE(cFormatTable));

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImFloor(em * cProjectTitleGapEm));

    if (platformChanged)
    {
        const ArchiveFormatInfo &info = cFormatTable[selected];

        sBfsar.setFormat(info.format);
        sBfsar.setPlatform(info.platform);
        sBfsar.setEndian(info.endian);
        sBfsar.setVersion(sVersionForEntry[selected]);

        SetUnsavedChanges(true);
    }
}

static void DrawProjectFormatBox(f32 em, bool stackRows)
{
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ImFloor(em * cSoundPanelPadXEm), ImFloor(em * cSoundPanelPadYEm)));

    const bool boxOpen = ImGui::BeginTable("ProjectFormat", 1, ImGuiTableFlags_BordersOuter);

    if (boxOpen)
        ImGui::TableNextColumn();

    DrawProjectPlatformUI(em);

    {
        u32 version = sBfsar.getVersion();

        DrawProjectRow(messages::common::cVersion, stackRows);
        if (DrawVersionUI(&version, sBfsar.getFormat(), false))
        {
            sBfsar.setVersion(version);
            SetUnsavedChanges(true);
        }
    }

    {
        bool stringTable = sBfsar.isIncludeStringTable();
        DrawProjectRow(messages::project::cStringTable, stackRows);
        DetailTooltip(messages::project::cStringTableDetail);

        const bool stringTableChanged = ImGui::Checkbox("##IncludeStringTable", &stringTable);
        DetailTooltip(messages::project::cStringTableDetail);

        if (stringTableChanged)
        {
            sBfsar.setIncludeStringTable(stringTable);
            SetUnsavedChanges(true);
        }
    }

    {
        DrawProjectRow(messages::common::cOptions, stackRows);
        DetailTooltip(messages::project::cOptionsDetail);

        const bool optionsChanged = ImGui::InputScalar("##Options", ImGuiDataType_U32, &sBfsar.getSoundArchivePlayerInfo().options);
        DetailTooltip(messages::project::cOptionsDetail);

        if (optionsChanged)
            SetUnsavedChanges(true);
    }

    if (boxOpen)
        ImGui::EndTable();

    ImGui::PopStyleVar();
}

static LimitLayout CalcLimitLayout(f32 em)
{
    LimitLayout layout = {};

    layout.hasBufferTimes = sBfsar.isStreamPrefetchAvailable();

    layout.playerInfo = &sBfsar.getSoundArchivePlayerInfo();
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

static void DrawLimitPanel(const LimitLayout& layout, LimitPanel panel, f32 offset)
{
    SoundArchivePlayerInfo& playerInfo = *layout.playerInfo;

    ImGui::PushID(static_cast<s32>(panel));

    switch (panel)
    {
        case LimitPanel::Sequence:
            RegionHeader(messages::project::cLimitSequenceTitle);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);

            DrawLimitSlot(layout, messages::common::cSounds, messages::player::cSoundLimitDetail, playerInfo.sequenceSoundMax, 0, cProjectLimitMax);
            NextLimitSlot(layout);
            DrawLimitSlot(layout, messages::common::cTracks, messages::project::cLimitTracksDetail, playerInfo.sequenceTrackMax, 0, cProjectLimitMax);
            break;

        case LimitPanel::Stream:
            RegionHeader(messages::project::cLimitStreamTitle);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);

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
            RegionHeader(messages::project::cLimitWaveTitle);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);

            DrawLimitSlot(layout, messages::common::cSounds, messages::player::cSoundLimitDetail, playerInfo.waveSoundMax, 0, cProjectLimitMax);
            NextLimitSlot(layout);
            DrawLimitSlot(layout, messages::common::cTracks, messages::project::cLimitTracksDetail, playerInfo.waveTrackMax, 0, cProjectLimitMax);
            break;
    }

    ImGui::PopID();
}

static void DrawProjectLimitsUI(f32 em, bool fullColumn)
{
    const LimitLayout layout = CalcLimitLayout(em);

    const f32 pairWidth = layout.calcPanelWidth(2);
    const f32 streamWidth = layout.calcPanelWidth(layout.labelNum);
    const f32 cellExtra = ImFloor(em * cSoundPanelPadXEm) * 2.0f + 1.0f;
    const f32 avail = ImGui::GetContentRegionAvail().x;

    if (!fullColumn)
    {
        const LimitPanel panels[] = { LimitPanel::Sequence, LimitPanel::Wave, LimitPanel::Stream };
        const f32 lineWidth = avail - cellExtra;

        for (LimitPanel panel : panels)
        {
            ImGui::PushID(static_cast<s32>(panel));
            DrawSoundPanels("ProjectLimitPanels", &lineWidth, 1, [&](s32, f32) { DrawLimitPanel(layout, panel, 0.0f); });
            ImGui::PopID();
        }
    }
    else if (pairWidth * 2.0f + streamWidth + cellExtra * 3.0f <= avail)
    {
        const LimitPanel panels[] = { LimitPanel::Sequence, LimitPanel::Stream, LimitPanel::Wave };
        const f32 widths[] = { pairWidth, streamWidth, pairWidth };

        DrawSoundPanels("ProjectLimitPanels", widths, IM_ARRAYSIZE(widths), [&](s32 index, f32 offset) { DrawLimitPanel(layout, panels[index], offset); });
    }
    else
    {
        const LimitPanel panels[] = { LimitPanel::Sequence, LimitPanel::Wave };
        const f32 widths[] = { pairWidth, pairWidth };
        const f32 lineWidth = sead::Mathf::max(streamWidth, avail - cellExtra);

        DrawSoundPanels("ProjectLimitPanels", widths, IM_ARRAYSIZE(widths), [&](s32 index, f32 offset) { DrawLimitPanel(layout, panels[index], offset); });
        DrawSoundPanels("ProjectLimitPanels2", &lineWidth, 1, [&](s32, f32) { DrawLimitPanel(layout, LimitPanel::Stream, ImFloor((lineWidth - streamWidth) * 0.5f)); });
    }
}

void DrawProjectInfoUI()
{
    const f32 em = ImGui::GetFontSize();

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const f32 workMaxX = window->WorkRect.Max.x;
    const f32 contentMaxX = window->ContentRegionRect.Max.x;
    const f32 columnMaxX = ImGui::GetCursorScreenPos().x + ImFloor(em * cProjectColumnEm);

    window->WorkRect.Max.x = sead::Mathf::min(workMaxX, columnMaxX);
    window->ContentRegionRect.Max.x = sead::Mathf::min(contentMaxX, columnMaxX);

    const f32 boxExtra = ImFloor(em * cSoundPanelPadXEm) * 2.0f + 2.0f;
    const f32 boxWidth = ImGui::GetContentRegionAvail().x - boxExtra;
    const bool fullColumn = ImGui::GetContentRegionAvail().x >= ImFloor(em * cProjectColumnEm) - 0.5f;
    const bool stackRows = boxWidth < CalcPropertyLabelWidth() + ImFloor(em * cProjectRowFieldEm);

    DrawProjectFormatBox(em, stackRows);
    DrawProjectLimitsUI(em, fullColumn);

    window->WorkRect.Max.x = workMaxX;
    window->ContentRegionRect.Max.x = contentMaxX;
}
