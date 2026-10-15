#include <ui/project/ProjectViews.h>

#include <ui/file/File.h>
#include <ui/items/ItemColumns.h>
#include <ui/Messages.h>
#include <ui/app/Shortcuts.h>
#include <ui/app/Workspace.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_internal.h>

#include <bfsar/files/BankFile.h>

struct ProjectRow
{
    UIType type;
    const char* icon;
    const char* name;
    s32 group;
    bool title;
    bool counted;
};

static const ProjectRow cProjectRows[] = {
    { UIType::ProjectInfo,       ICON_LC_INFO,           messages::common::cProjectInfo,    0, false, false },

    { UIType::AllSounds,         ICON_LC_LIST_MUSIC,     messages::common::cSounds,         1, true,  true  },
    { UIType::StreamSounds,      ICON_LC_DISC_3,         messages::common::cStream,         1, false, true  },
    { UIType::WaveSounds,        ICON_LC_AUDIO_LINES,    messages::common::cWave,           1, false, true  },
    { UIType::SequenceSounds,    ICON_LC_MUSIC_3,        messages::common::cSequence,       1, false, true  },

    { UIType::AllSoundSets,      ICON_LC_MUSIC_4,        messages::project::cRowSoundSets,      2, true,  true  },
    { UIType::WaveSoundSets,     ICON_LC_AUDIO_WAVEFORM, messages::common::cWave,           2, false, true  },
    { UIType::SequenceSoundSets, ICON_LC_MUSIC_2,        messages::common::cSequence,       2, false, true  },

    { UIType::Banks,             ICON_LC_PIANO,          messages::project::cRowBanks,          3, false, true  },
    { UIType::WaveArchives,      ICON_LC_ARCHIVE,        messages::project::cRowWaveArchives,   3, false, true  },
    { UIType::Groups,            ICON_LC_FOLDERS,        messages::project::cRowGroups,         3, false, true  },
    { UIType::Players,           ICON_LC_VOLUME_2,       messages::project::cRowPlayers,        3, false, true  },

    { UIType::WaveFiles,         ICON_LC_FILE_AUDIO,     messages::project::cRowWaveFiles,      4, false, true  },
    { UIType::SequenceFiles,     ICON_LC_FILE_MUSIC,     messages::project::cRowSequenceFiles,  4, false, true  },
    { UIType::BankFiles,         ICON_LC_KEYBOARD_MUSIC, messages::project::cRowBankFiles,      4, false, true  },

    { UIType::FileStatistics,    ICON_LC_CHART_AREA,     messages::project::cRowFileStatistics, 5, false, false },
};

static constexpr f32 cProjectBoxPadXEm     = 0.2f;
static constexpr f32 cProjectBoxPadYEm     = 0.15f;
static constexpr f32 cProjectRowGapEm      = 0.3f;
static constexpr f32 cProjectChildIndentEm = 0.7f;

static void CountProjectItems(s32* counts)
{
    auto at = [counts](UIType type) -> s32&
    {
        return counts[static_cast<size_t>(type)];
    };

    for (const Item* item : sBfsar.getSoundList())
    {
        switch (static_cast<const Sound*>(item)->getSoundType())
        {
            case Sound::SoundType::Strm:
                at(UIType::StreamSounds)++;
                break;

            case Sound::SoundType::Wave:
                at(UIType::WaveSounds)++;
                break;

            case Sound::SoundType::Seq:
                at(UIType::SequenceSounds)++;
                break;

            default:
                break;
        }
    }

    for (const Item* item : sBfsar.getSoundSetList())
    {
        if (static_cast<const SoundSet*>(item)->getSoundSetType() == SoundSet::SoundSetType::Seq)
            at(UIType::SequenceSoundSets)++;
        else
            at(UIType::WaveSoundSets)++;
    }

    at(UIType::AllSounds)     = static_cast<s32>(sBfsar.getSoundList().size());
    at(UIType::AllSoundSets)  = static_cast<s32>(sBfsar.getSoundSetList().size());
    at(UIType::Banks)         = static_cast<s32>(sBfsar.getBankList().size());
    at(UIType::WaveArchives)  = static_cast<s32>(sBfsar.getWaveArchiveList().size());
    at(UIType::Groups)        = static_cast<s32>(sBfsar.getGroupList().size());
    at(UIType::Players)       = static_cast<s32>(sBfsar.getPlayerList().size());
    at(UIType::WaveFiles)     = static_cast<s32>(sBfsar.getWaveFileList().size());
    at(UIType::SequenceFiles) = static_cast<s32>(sBfsar.getSequenceFileList().size());
    at(UIType::BankFiles)     = static_cast<s32>(sBfsar.getBankFileList().size());
}

bool DrawNavRow(const char* icon, const char* name, bool selected, const char* value, bool dim, bool title, f32 indent)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 iconWidth = ImGui::CalcTextSize(ICON_LC_INFO " ").x;

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const f32 width = ImGui::GetContentRegionAvail().x;

    const bool pressed = ImGui::Selectable("##Row", selected, ImGuiSelectableFlags_SpanAvailWidth);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec4 nameColor = ImGui::GetStyleColorVec4(dim ? ImGuiCol_TextDisabled : ImGuiCol_Text);

    f32 left = pos.x + style.FramePadding.x + indent;
    f32 right = pos.x + width - style.FramePadding.x;

    if (value)
    {
        const f32 textWidth = ImGui::CalcTextSize(value).x;
        const ImVec4 valueColor = dim ? nameColor : GetItemValueColor();

        drawList->AddText(ImVec2(right - textWidth, pos.y), ImGui::ColorConvertFloat4ToU32(valueColor), value);
        right -= textWidth + style.ItemSpacing.x;
    }

    drawList->AddText(ImVec2(left, pos.y), ImGui::ColorConvertFloat4ToU32(nameColor), icon);
    left += iconWidth;

    ImGui::PushStyleColor(ImGuiCol_Text, nameColor);
    ImGui::RenderTextEllipsis(drawList, ImVec2(left, pos.y), ImVec2(right, pos.y + ImGui::GetFontSize()), right, right, name, nullptr, nullptr);
    ImGui::PopStyleColor();

    if (title)
    {
        const f32 lineLeft = left + ImGui::CalcTextSize(name).x + style.ItemSpacing.x;
        const f32 lineY = ImFloor(pos.y + ImGui::GetFontSize() * 0.5f);

        if (lineLeft < right)
            drawList->AddLine(ImVec2(lineLeft, lineY), ImVec2(right, lineY), ImGui::GetColorU32(ImGuiCol_Separator));
    }

    return pressed;
}

f32 CalcNavRowGap()
{
    return ImFloor(ImGui::GetFontSize() * cProjectRowGapEm);
}

ImVec2 CalcNavBoxPadding(bool roomy)
{
    const f32 em = ImGui::GetFontSize();

    return ImVec2(ImFloor(em * cProjectBoxPadXEm), roomy ? ImFloor(CalcNavRowGap() * 0.5f) + 1.0f : ImFloor(em * cProjectBoxPadYEm));
}

bool BeginNavBox(const char* id, const ImVec2& padding, bool border, f32 width)
{
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, border ? padding : ImVec2(padding.x + 1.0f, 0.0f));

    if (!ImGui::BeginTable(id, 1, border ? ImGuiTableFlags_BordersOuter : ImGuiTableFlags_PadOuterX, ImVec2(width, 0.0f)))
    {
        ImGui::PopStyleVar();
        return false;
    }

    ImGui::TableNextColumn();
    return true;
}

void EndNavBox()
{
    ImGui::EndTable();
    ImGui::PopStyleVar();
}

static bool DrawProjectRow(const ProjectRow& row, s32 count, f32 indent)
{
    const sead::FormatFixedSafeString<16> text("%d", count);

    ImGui::PushID(static_cast<s32>(row.type));
    const bool pressed = DrawNavRow(row.icon, row.name, sSelectedUIType == row.type, row.counted ? text.cstr() : nullptr, row.counted && count == 0, row.title, indent);
    ImGui::PopID();

    return pressed;
}

void DrawProjectUI()
{
    if (ImGui::Begin(sead::FormatFixedSafeString<64>(ICON_LC_FOLDER " %s###ProjectWindow", messages::common::cProject).cstr()))
    {
        bool selected = false;

        s32 counts[static_cast<size_t>(UIType::Max) + 1] = {};
        CountProjectItems(counts);

        const f32 em = ImGui::GetFontSize();
        const ImGuiStyle& style = ImGui::GetStyle();

        const s32 rowNum = static_cast<s32>(IM_ARRAYSIZE(cProjectRows));
        const s32 groupNum = cProjectRows[rowNum - 1].group + 1;

        const f32 rowGap = CalcNavRowGap();
        const f32 roomyPadY = CalcNavBoxPadding(true).y;
        const f32 roomyHeight = rowNum * em + (rowNum - groupNum) * rowGap + groupNum * roomyPadY * 2.0f + (groupNum - 1) * rowGap;
        const bool roomy = ImGui::GetContentRegionAvail().y >= roomyHeight;

        const ImVec2 boxPad = CalcNavBoxPadding(roomy);
        const f32 childIndent = ImFloor(em * cProjectChildIndentEm);
        s32 group = -1;
        f32 indent = 0.0f;

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, roomy ? rowGap : style.ItemSpacing.y));

        auto endGroup = [&group]()
        {
            if (group < 0)
                return;

            EndNavBox();
            group = -1;
        };

        for (const ProjectRow& row : cProjectRows)
        {
            if (row.group != group)
            {
                endGroup();

                if (!BeginNavBox(sead::FormatFixedSafeString<16>("##Group%d", row.group).cstr(), boxPad, roomy || row.counted))
                    break;

                group = row.group;
                indent = 0.0f;
            }

            if (DrawProjectRow(row, counts[static_cast<size_t>(row.type)], indent))
            {
                sSelectedUIType = row.type;
                selected = true;
            }

            if (row.title)
                indent = childIndent;
        }

        endGroup();
        ImGui::PopStyleVar();

        if (ImGui::IsWindowFocused())
        {
            if (shortcuts::Pressed(shortcuts::Action::SelectPrevious))
            {
                if (sSelectedUIType > UIType::Min)
                {
                    sSelectedUIType = UIType(u32(sSelectedUIType) - 1);
                }
            }

            if (shortcuts::Pressed(shortcuts::Action::SelectNext))
            {
                if (sSelectedUIType < UIType::Max)
                {
                    sSelectedUIType = UIType(u32(sSelectedUIType) + 1);
                }
            }
        }

        if (selected)
        {
            ImGuiWindow* w = ImGui::FindWindowByName("###InfoWindow");
            if (w && w->DockIsActive && w->Hidden)
            {
                ImGui::SetWindowFocus("###InfoWindow");
            }
        }
    }
    ImGui::End();
}
