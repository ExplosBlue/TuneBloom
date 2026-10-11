#define IMGUI_DEFINE_MATH_OPERATORS
#include <ui/export/Export.h>

#include <ui/bank/BankFileEditor.h>
#include <ui/widgets/Dialogs.h>
#include <ui/export/ExportInternal.h>
#include <ui/file/FileDialogs.h>
#include <ui/items/ItemColumns.h>
#include <ui/Messages.h>
#include <ui/app/PopupMgr.h>
#include <ui/widgets/Properties.h>
#include <ui/project/References.h>
#include <ui/widgets/Widgets.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_internal.h>

#include <bfsar/files/BankFile.h>
#include <bfsar/model/Sound.h>
#include <bfsar/model/SoundSet.h>
#include <midi/ExportCommon.h>
#include <midi/InstBankExporter.h>
#include <midi/SeqMidiExporter.h>

#include <algorithm>
#include <string>
#include <vector>

static constexpr f32 cMidiDialogWidthEm  = 34.0f;
static constexpr f32 cMidiTrackWidthEm   = 4.0f;
static constexpr f32 cMidiProgramWidthEm = 3.6f;
static constexpr f32 cMidiNotesWidthEm   = 3.6f;
static constexpr f32 cMidiBankGapEm      = 1.5f;
static constexpr s32 cMidiListRowMin     = 1;
static constexpr s32 cMidiListRowMax     = 8;

namespace
{

struct MidiListRow
{
    std::string name;
    std::string detail;
    u32 noteCount = 0;
    bool enabled = true;
};

struct MidiTrackRow : MidiListRow
{
    u8 trackNo = 0;
};

struct MidiInstrumentRow : MidiListRow
{
    const BankFile* bankFile = nullptr;
    u16 programNo = 0;
};

struct MidiListColumns
{
    const char* id;
    const char* titles[4];
    f32 nameWidthEm;
    f32 detailWidthEm;
};

struct MidiSoundEntry
{
    const Sound* sound = nullptr;
    const BankFile* bankFiles[cSeqSoundBankNum] = {};
    SeqMidiContents contents;
};

struct MidiExportRequest
{
    std::vector<const Sound*> sounds;
    std::vector<MidiSoundEntry> entries;
    std::vector<MidiTrackRow> tracks;
    std::vector<MidiInstrumentRow> instruments;
    u32 noteNum = 0;
    u32 selectedNoteNum = 0;
    bool toFolder = false;
    bool confirmed = false;
    bool withSf2 = true;
    bool withDls = false;

    void clear()
    {
        sounds.clear();
        entries.clear();
        tracks.clear();
        instruments.clear();
        noteNum = 0;
        selectedNoteNum = 0;
        toFolder = false;
        confirmed = false;
    }
};

}

static MidiExportRequest sMidiExport;

static constexpr MidiListColumns cMidiTrackColumns = {
    "##Tracks",
    { "", messages::common::cTrack, messages::common::cInstruments, messages::common::cNotes },
    cMidiTrackWidthEm,
    0.0f
};
static constexpr MidiListColumns cMidiInstrumentColumns = {
    "##Instruments",
    { "", messages::common::cInstrument, messages::common::cProgram, messages::common::cNotes },
    0.0f,
    cMidiProgramWidthEm
};

static MidiTrackRow* FindTrackRow(u32 trackNo)
{
    for (MidiTrackRow& row : sMidiExport.tracks)
    {
        if (row.trackNo == trackNo)
            return &row;
    }

    return nullptr;
}

static MidiInstrumentRow* FindInstrumentRow(const BankFile* bankFile, u16 programNo)
{
    for (MidiInstrumentRow& row : sMidiExport.instruments)
    {
        if (row.bankFile == bankFile && row.programNo == programNo)
            return &row;
    }

    return nullptr;
}

static MidiInstrumentRow* FindInstrumentRow(const MidiSoundEntry& entry, const SeqMidiPart& part)
{
    const u32 bankIndex = part.program.first;

    return FindInstrumentRow(bankIndex < cSeqSoundBankNum ? entry.bankFiles[bankIndex] : nullptr, part.program.second);
}

static std::string MakeInstrumentName(const BankFile* bankFile, u16 programNo)
{
    const BankFile::Instrument* instrument = bankFile ? bankFile->getInstrument(programNo) : nullptr;

    sead::FixedSafeString<256> buffer;
    const char* label = instrument ? GetInstrumentLabel(*instrument, buffer) : nullptr;

    if (label)
        return label;

    return sead::FormatFixedSafeString<32>(messages::exporting::cMidiProgramNameFormat, programNo).cstr();
}

static void AddEntryRows(const MidiSoundEntry& entry)
{
    for (u32 trackNo = 0; trackNo < cSeqMidiTrackNum; trackNo++)
    {
        if (!(entry.contents.trackFlags & (1 << trackNo)) || FindTrackRow(trackNo))
            continue;

        MidiTrackRow row;
        row.trackNo = static_cast<u8>(trackNo);
        row.name = sead::FormatFixedSafeString<32>(messages::stream::cTrackNameFormat, trackNo).cstr();
        sMidiExport.tracks.push_back(row);
    }

    for (const SeqMidiPart& part : entry.contents.parts)
    {
        if (!FindInstrumentRow(entry, part))
        {
            MidiInstrumentRow row;
            row.bankFile = part.program.first < cSeqSoundBankNum ? entry.bankFiles[part.program.first] : nullptr;
            row.programNo = part.program.second;
            row.name = MakeInstrumentName(row.bankFile, row.programNo);
            row.detail = sead::FormatFixedSafeString<16>("%u", row.programNo).cstr();
            sMidiExport.instruments.push_back(row);
        }

        FindTrackRow(part.trackNo)->noteCount += part.noteCount;
        FindInstrumentRow(entry, part)->noteCount += part.noteCount;
        sMidiExport.noteNum += part.noteCount;
    }
}

static void SetTrackInstrumentTexts()
{
    for (MidiTrackRow& track : sMidiExport.tracks)
    {
        std::vector<const MidiInstrumentRow*> used;

        for (const MidiSoundEntry& entry : sMidiExport.entries)
        {
            for (const SeqMidiPart& part : entry.contents.parts)
            {
                const MidiInstrumentRow* instrument = FindInstrumentRow(entry, part);

                if (part.trackNo != track.trackNo || std::find(used.begin(), used.end(), instrument) != used.end())
                    continue;

                if (!used.empty())
                    track.detail += messages::exporting::cHeadJoin;

                track.detail += instrument->name;
                used.push_back(instrument);
            }
        }
    }
}

static void UpdateSelectedNotes()
{
    sMidiExport.selectedNoteNum = 0;

    for (const MidiSoundEntry& entry : sMidiExport.entries)
    {
        for (const SeqMidiPart& part : entry.contents.parts)
        {
            if (FindTrackRow(part.trackNo)->enabled && FindInstrumentRow(entry, part)->enabled)
                sMidiExport.selectedNoteNum += part.noteCount;
        }
    }
}

static void ScanMidiExport()
{
    for (const Sound* sound : sMidiExport.sounds)
    {
        MidiSoundEntry entry;
        entry.sound = sound;
        entry.contents = collectSeqMidiContents(*sound);

        for (u32 i = 0; i < cSeqSoundBankNum; i++)
            entry.bankFiles[i] = GetSeqSoundBankFile(*sound, i);

        AddEntryRows(entry);
        sMidiExport.entries.push_back(entry);
    }

    std::sort(sMidiExport.tracks.begin(), sMidiExport.tracks.end(), [](const MidiTrackRow& a, const MidiTrackRow& b) { return a.trackNo < b.trackNo; });
    std::stable_sort(sMidiExport.instruments.begin(), sMidiExport.instruments.end(), [](const MidiInstrumentRow& a, const MidiInstrumentRow& b) { return a.programNo < b.programNo; });

    SetTrackInstrumentTexts();
    UpdateSelectedNotes();
}

void RequestExportMidi(const std::vector<Sound*>& sounds)
{
    sMidiExport.clear();
    sMidiExport.sounds.assign(sounds.begin(), sounds.end());
    sMidiExport.toFolder = sounds.size() > 1;

    ScanMidiExport();
}

void RequestExportMidi(const std::vector<SoundSet*>& soundSets)
{
    sMidiExport.clear();
    sMidiExport.toFolder = true;

    for (const SoundSet* soundSet : soundSets)
    {
        for (const Sound* sound : GetSeqSoundsOfSet(*soundSet))
        {
            if (std::find(sMidiExport.sounds.begin(), sMidiExport.sounds.end(), sound) == sMidiExport.sounds.end())
                sMidiExport.sounds.push_back(sound);
        }
    }

    ScanMidiExport();
}

void CancelMidiExport()
{
    sMidiExport.clear();
}

static SeqMidiFilter MakeSoundFilter(const MidiSoundEntry& entry)
{
    SeqMidiFilter filter;
    filter.trackFlags = 0;

    for (const MidiTrackRow& track : sMidiExport.tracks)
    {
        if (track.enabled)
            filter.trackFlags |= 1 << track.trackNo;
    }

    for (const SeqMidiPart& part : entry.contents.parts)
    {
        if (FindInstrumentRow(entry, part)->enabled)
            continue;

        if (std::find(filter.excludedPrograms.begin(), filter.excludedPrograms.end(), part.program) == filter.excludedPrograms.end())
            filter.excludedPrograms.push_back(part.program);
    }

    return filter;
}

static bool HasSelectedNotes(const MidiSoundEntry& entry, const SeqMidiFilter& filter)
{
    if (entry.contents.parts.empty())
        return true;

    for (const SeqMidiPart& part : entry.contents.parts)
    {
        if (filter.isEnabled(part.trackNo, part.program))
            return true;
    }

    return false;
}

static bool ExportMidiSound(const MidiSoundEntry& entry, const SeqMidiFilter& filter, const sead::SafeString& midiPath)
{
    const bool exported = exportSeqToMidi(midiPath, *entry.sound, &filter);

    if (sMidiExport.withSf2)
    {
        sead::FixedSafeString<512> sf2Path(midiPath);
        ReplacePathExtension(&sf2Path, ".sf2");
        exportSeqSoundToSf2(sf2Path, *entry.sound, &filter);
    }

    if (sMidiExport.withDls)
    {
        sead::FixedSafeString<512> dlsPath(midiPath);
        ReplacePathExtension(&dlsPath, ".dls");
        exportSeqSoundToDls(dlsPath, *entry.sound, &filter);
    }

    return exported;
}

static void ExportMidiSoundsToFolder()
{
    sead::FixedSafeString<512> dirPath;
    if (!SelectFolderDialog(&dirPath, messages::exporting::cSelectMidiFolder))
        return;

    BeginExportNameBatch();

    s32 failureCount = 0;

    for (const MidiSoundEntry& entry : sMidiExport.entries)
    {
        const SeqMidiFilter filter = MakeSoundFilter(entry);

        if (!HasSelectedNotes(entry, filter))
            continue;

        sead::FixedSafeString<512> filePath;
        BuildExportPathFromDir(&filePath, dirPath.cstr(), entry.sound, "midi");

        if (!ExportMidiSound(entry, filter, filePath))
            failureCount++;
    }

    if (failureCount > 0)
        PopupMgr::instance()->addPopup({ sead::FormatFixedSafeString<64>(failureCount == 1 ? messages::exporting::cMidisExportFailedOneFormat : messages::exporting::cMidisExportFailedFormat, failureCount), nullptr });

    ShowExportedFiles(dirPath);
}

static void ExportMidiSoundToFile(const MidiSoundEntry& entry)
{
    sead::FixedSafeString<512> defaultPath;
    BuildDefaultExportPath(&defaultPath, entry.sound, "midi");

    sead::FixedSafeString<512> path;
    if (!SaveFileDialogWithExtension(&path, messages::filedialog::cFilterMidi, "midi", defaultPath.cstr()))
        return;

    if (ExportMidiSound(entry, MakeSoundFilter(entry), path))
        ShowExportedFiles(path);
    else
        PopupMgr::instance()->addPopup({ messages::exporting::cMidiExportFailed, nullptr });
}

void ExportPendingMidi()
{
    if (!sMidiExport.confirmed)
        return;

    if (sMidiExport.toFolder)
        ExportMidiSoundsToFolder();
    else
        ExportMidiSoundToFile(sMidiExport.entries.front());

    sMidiExport.clear();
}

static sead::FixedSafeString<32> FormatMidiCount(s32 count, const char* oneText, const char* format)
{
    sead::FixedSafeString<32> text;

    if (count == 1)
        text.copy(oneText);
    else
        text.format(format, count);

    return text;
}

static void DrawMidiExportHead()
{
    const s32 count = static_cast<s32>(sMidiExport.sounds.size());
    const bool single = count == 1;

    sead::FixedSafeString<256> title;
    if (single && sMidiExport.sounds[0]->isNameValid())
        title.format("%s", sMidiExport.sounds[0]->getName().cstr());
    else if (single)
        title.copy(messages::item::cTypeSequenceSound);
    else
        title.format(messages::exporting::cSoundsFormat, count);

    sead::FixedSafeString<128> detail;
    detail.append(FormatMidiCount(static_cast<s32>(sMidiExport.tracks.size()), messages::exporting::cMidiTrackNumOne, messages::exporting::cMidiTrackNumFormat));
    detail.append(messages::exporting::cHeadJoin);
    detail.append(FormatMidiCount(static_cast<s32>(sMidiExport.instruments.size()), messages::exporting::cMidiInstrumentNumOne, messages::exporting::cMidiInstrumentNumFormat));

    DrawPageHeader(single ? GetItemIcon(sMidiExport.sounds[0]) : ICON_LC_FILE_MUSIC, title.cstr(), detail.cstr());
}

static bool DrawMidiRowCheck(bool* enabled)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();

    bool rowHovered = false;

    ImGui::SetNextItemAllowOverlap();

    bool changed = DrawTableRowSelectable("##Row", false, &rowHovered);

    if (changed)
        *enabled = !*enabled;

    ImGui::SameLine(0.0f, 0.0f);

    if (ImGui::Checkbox("##On", enabled))
        changed = true;

    return changed;
}

template <typename Row>
static void DrawMidiListBox(const char* title, const MidiListColumns& columns, std::vector<Row>* rows, bool* changed)
{
    const bool allEnabled = std::all_of(rows->begin(), rows->end(), [](const Row& row) { return row.enabled; });

    bool pressed = false;

    if (!BeginGroupBox(title, allEnabled ? messages::common::cNone : messages::common::cAll, &pressed))
        return;

    if (pressed)
    {
        for (Row& row : *rows)
            row.enabled = !allEnabled;

        *changed = true;
    }

    const f32 em = ImGui::GetFontSize();
    const s32 rowNum = ImClamp(static_cast<s32>(rows->size()), cMidiListRowMin, cMidiListRowMax);

    if (BeginDialogList(columns.id, IM_ARRAYSIZE(columns.titles), rowNum))
    {
        ImGui::TableSetupColumn("##On", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
        ImGui::TableSetupColumn(columns.titles[1], columns.nameWidthEm > 0.0f ? ImGuiTableColumnFlags_WidthFixed : ImGuiTableColumnFlags_WidthStretch, ImFloor(em * columns.nameWidthEm));
        ImGui::TableSetupColumn(columns.titles[2], columns.detailWidthEm > 0.0f ? ImGuiTableColumnFlags_WidthFixed : ImGuiTableColumnFlags_WidthStretch, ImFloor(em * columns.detailWidthEm));
        ImGui::TableSetupColumn(columns.titles[3], ImGuiTableColumnFlags_WidthFixed, ImFloor(em * cMidiNotesWidthEm));

        DrawDialogListTitles(columns.titles, IM_ARRAYSIZE(columns.titles), true);

        for (Row& row : *rows)
        {
            ImGui::PushID(&row);

            if (DrawMidiRowCheck(&row.enabled))
                *changed = true;

            const ImVec4 valueColor = row.enabled ? GetItemValueColor() : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);

            ImGui::TableSetColumnIndex(1);
            DrawDialogListText(row.name.c_str(), ImGui::GetColorU32(row.enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled), false, true);

            ImGui::TableSetColumnIndex(2);
            DrawDialogListText(row.detail.c_str(), ImGui::GetColorU32(valueColor));

            ImGui::TableSetColumnIndex(3);
            DrawDialogListText(sead::FormatFixedSafeString<16>("%u", row.noteCount).cstr(), ImGui::GetColorU32(valueColor), true);

            ImGui::PopID();
        }

        if (rows->empty())
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(columns.nameWidthEm > 0.0f ? 2 : 1);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", messages::exporting::cMidiListEmpty);
        }

        EndDialogList();
    }

    EndGroupBox();
}

static void DrawMidiExportLists()
{
    bool changed = false;

    DrawMidiListBox(messages::common::cTracks, cMidiTrackColumns, &sMidiExport.tracks, &changed);
    DrawMidiListBox(messages::common::cInstruments, cMidiInstrumentColumns, &sMidiExport.instruments, &changed);

    if (changed)
        UpdateSelectedNotes();
}

static void DrawMidiBankCheck(const char* label, const char* fileType, bool* enabled)
{
    ImGui::Checkbox(label, enabled);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::TextDisabled("%s", fileType);
}

static void DrawMidiBankRow()
{
    PropertyRow(messages::exporting::cMidiBankLabel);

    DrawMidiBankCheck(messages::exporting::cFormatSoundFont, messages::exporting::cMidiSf2Type, &sMidiExport.withSf2);
    ImGui::SameLine(0.0f, ImFloor(ImGui::GetFontSize() * cMidiBankGapEm));
    DrawMidiBankCheck(messages::exporting::cFormatDls, messages::exporting::cMidiDlsType, &sMidiExport.withDls);
}

static void DrawMidiExportFooter()
{
    const bool canExport = sMidiExport.selectedNoteNum > 0 || sMidiExport.noteNum == 0;

    DrawShowExportedFilesCheck();
    ImGui::SameLine(0.0f, ImGui::GetFontSize());
    ImGui::TextDisabled(messages::exporting::cMidiNotesFormat, sMidiExport.selectedNoteNum, sMidiExport.noteNum);
    ImGui::SameLine();

    BeginDialogFooter({messages::common::cExport, messages::common::cCancel}, false);

    ImGui::BeginDisabled(!canExport);

    if (DialogButton(messages::common::cExport))
    {
        sMidiExport.confirmed = true;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndDisabled();

    if (!canExport)
        SetDisabledTooltip(messages::exporting::cMidiNothingSelected);

    if (DialogButton(messages::common::cCancel))
    {
        sMidiExport.clear();
        ImGui::CloseCurrentPopup();
    }
}

void DrawMidiExportDialog()
{
    if (sMidiExport.sounds.empty() || sMidiExport.confirmed)
        return;

    const sead::FormatFixedSafeString<64> popupName("%s###ExportMidi", messages::menu::cExportMidi);

    ImGui::OpenPopup(popupName.cstr());

    CenterNextDialog();
    ImGui::SetNextWindowContentSize(ImVec2(ImFloor(ImGui::GetFontSize() * cMidiDialogWidthEm), 0.0f));

    if (!BeginDialog(popupName.cstr(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    DrawMidiExportHead();
    DrawMidiExportLists();
    DrawMidiBankRow();
    DialogRowGap();
    DrawMidiExportFooter();

    ImGui::EndPopup();
}
