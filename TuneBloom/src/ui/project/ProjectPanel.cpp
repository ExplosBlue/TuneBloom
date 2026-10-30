#include <ui/project/ProjectViews.h>

#include <ui/widgets/Dialogs.h>
#include <ui/file/File.h>
#include <ui/items/ItemColumns.h>
#include <messages/Messages.h>
#include <ui/app/Shortcuts.h>
#include <ui/app/Workspace.h>
#include <ui/widgets/Widgets.h>

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

static const ProjectRow cSoundArchiveRows[] = {
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
};

struct ProjectRowTable
{
    const ProjectRow* rows;
    s32 count;
};

static constexpr f32 cProjectBoxPadXEm     = 0.2f;
static constexpr f32 cProjectBoxPadYEm     = 0.15f;
static constexpr f32 cProjectRowGapEm      = 0.3f;
static constexpr f32 cProjectChildIndentEm = 0.7f;

static s32 FindProjectRow(const ProjectRowTable& table, UIType type)
{
    for (s32 i = 0; i < table.count; i++)
    {
        if (table.rows[i].type == type)
            return i;
    }

    return -1;
}

static void StepProjectRow(const ProjectRowTable& table, s32 step)
{
    const s32 current = FindProjectRow(table, GetUITab());
    const s32 next = current < 0 ? 0 : current + step;

    if (next >= 0 && next < table.count)
        SetUITab(table.rows[next].type);
}

static s32& CountAt(s32* counts, UIType type)
{
    return counts[static_cast<size_t>(type)];
}

static void CountSoundArchiveItems(Bfsar& archive, s32* counts)
{
    for (const Item* item : archive.getSoundList())
    {
        switch (static_cast<const Sound*>(item)->getSoundType())
        {
            case Sound::SoundType::Strm:
                CountAt(counts, UIType::StreamSounds)++;
                break;

            case Sound::SoundType::Wave:
                CountAt(counts, UIType::WaveSounds)++;
                break;

            case Sound::SoundType::Seq:
                CountAt(counts, UIType::SequenceSounds)++;
                break;

            default:
                break;
        }
    }

    for (const Item* item : archive.getSoundSetList())
    {
        if (static_cast<const SoundSet*>(item)->getSoundSetType() == SoundSet::SoundSetType::Seq)
            CountAt(counts, UIType::SequenceSoundSets)++;
        else
            CountAt(counts, UIType::WaveSoundSets)++;
    }

    CountAt(counts, UIType::AllSounds)     = static_cast<s32>(archive.getSoundList().size());
    CountAt(counts, UIType::AllSoundSets)  = static_cast<s32>(archive.getSoundSetList().size());
    CountAt(counts, UIType::Banks)         = static_cast<s32>(archive.getBankList().size());
    CountAt(counts, UIType::WaveArchives)  = static_cast<s32>(archive.getWaveArchiveList().size());
    CountAt(counts, UIType::Groups)        = static_cast<s32>(archive.getGroupList().size());
    CountAt(counts, UIType::Players)       = static_cast<s32>(archive.getPlayerList().size());
    CountAt(counts, UIType::WaveFiles)     = static_cast<s32>(archive.getWaveFileList().size());
    CountAt(counts, UIType::SequenceFiles) = static_cast<s32>(archive.getSequenceFileList().size());
    CountAt(counts, UIType::BankFiles)     = static_cast<s32>(archive.getBankFileList().size());
}

static ProjectRowTable GetProjectRows(s32* counts)
{
    if (Bfsar* archive = FindActiveSoundArchive())
        CountSoundArchiveItems(*archive, counts);

    return { cSoundArchiveRows, IM_ARRAYSIZE(cSoundArchiveRows) };
}

static f32 CalcNavRowGap()
{
    return ImFloor(ImGui::GetFontSize() * cProjectRowGapEm);
}

static ImVec2 CalcNavBoxPadding(bool roomy)
{
    const f32 em = ImGui::GetFontSize();

    return ImVec2(ImFloor(em * cProjectBoxPadXEm), roomy ? ImFloor(CalcNavRowGap() * 0.5f) + 1.0f : ImFloor(em * cProjectBoxPadYEm));
}

static bool BeginNavBox(const char* id, const ImVec2& padding, bool border)
{
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, border ? padding : ImVec2(padding.x + 1.0f, 0.0f));

    if (!ImGui::BeginTable(id, 1, border ? ImGuiTableFlags_BordersOuter : ImGuiTableFlags_PadOuterX))
    {
        ImGui::PopStyleVar();
        return false;
    }

    ImGui::TableNextColumn();
    return true;
}

static void EndNavBox()
{
    ImGui::EndTable();
    ImGui::PopStyleVar();
}

static bool DrawProjectRow(const ProjectRow& row, s32 count, f32 indent)
{
    const sead::FixedSafeString<32> text = FormatCountText(count);

    ImGui::PushID(static_cast<s32>(row.type));
    const bool pressed = DrawNavRow(row.icon, row.name, GetUITab() == row.type, row.counted ? text.cstr() : nullptr, row.counted && count == 0, row.title, indent);
    ImGui::PopID();

    return pressed;
}

void DrawProjectList()
{
    bool selected = false;

    s32 counts[static_cast<size_t>(UIType::Max) + 1] = {};
    const ProjectRowTable table = GetProjectRows(counts);

    const f32 em = ImGui::GetFontSize();
    const ImGuiStyle& style = ImGui::GetStyle();

    const s32 rowNum = table.count;
    const s32 groupNum = table.rows[rowNum - 1].group + 1;

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

    for (s32 rowIndex = 0; rowIndex < table.count; rowIndex++)
    {
        const ProjectRow& row = table.rows[rowIndex];

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
            SetUITab(row.type);
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
            StepProjectRow(table, -1);

        if (shortcuts::Pressed(shortcuts::Action::SelectNext))
            StepProjectRow(table, 1);
    }

    if (selected)
        FocusInfoWindow();
}
