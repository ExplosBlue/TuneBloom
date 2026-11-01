#include <ui/stream/StreamTrackImportInternal.h>

#include <ui/items/ItemColumns.h>
#include <ui/items/ItemList.h>
#include <ui/player/PlayerViews.h>
#include <ui/settings/UiColors.h>
#include <ui/wave/WaveFormatUI.h>
#include <ui/widgets/Dialogs.h>
#include <ui/widgets/Properties.h>
#include <ui/widgets/Widgets.h>
#include <messages/Messages.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_internal.h>

#include <algorithm>

namespace trackimport
{

enum FileColumn : s32
{
    cFileColumn_Play,
    cFileColumn_Track,
    cFileColumn_File,
    cFileColumn_Channels,
    cFileColumn_Rate,
    cFileColumn_Length,
    cFileColumn_Edit,
    cFileColumn_Remove,
    cFileColumn_Num,
};

static f32 CalcColumnWidth(const char* title, const char* widestValue)
{
    return ImCeil(ImMax(ImGui::CalcTextSize(title, nullptr, true).x, ImGui::CalcTextSize(widestValue).x));
}

static void DrawValueText(const char* text)
{
    ImGui::TextColored(GetItemValueColor(), "%s", text);
}

static void DrawValueCell(s32 column, const char* text)
{
    ImGui::TableSetColumnIndex(column);
    ImGui::AlignTextToFramePadding();
    DrawValueText(text);
}

static void DrawFormatCells(u32 channels, u32 rate, u32 frames)
{
    const double seconds = rate ? static_cast<double>(frames) / rate : 0.0;

    if (channels <= 2)
        DrawValueCell(cFileColumn_Channels, channels == 1 ? messages::common::cMono : messages::common::cStereo);
    else
        DrawValueCell(cFileColumn_Channels, sead::FormatFixedSafeString<32>(messages::stream::cTrackChannelsFormat, channels).cstr());

    DrawValueCell(cFileColumn_Rate, sead::FormatFixedSafeString<32>(messages::unit::cHertzFormat, rate).cstr());
    DrawValueCell(cFileColumn_Length, sead::FormatFixedSafeString<32>(messages::common::cSecondsFormat, seconds).cstr());
}

static bool DrawRowStart(u32 id, bool playing, bool* outPlay)
{
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(cFileColumn_Play);
    BeginItemActionCell();
    *outPlay = DrawRowPlayButton(id, nullptr, playing);
    EndItemActionCell();

    ImGui::TableSetColumnIndex(cFileColumn_Track);
    ImGui::AlignTextToFramePadding();

    bool rowHovered = false;

    ImGui::SetNextItemAllowOverlap();
    DrawTableRowSelectable("##Row", false, &rowHovered);

    return rowHovered;
}

static bool DrawRowAction(s32 column, const char* id, const char* icon, bool visible, const char* tooltip)
{
    ImGui::TableSetColumnIndex(column);
    BeginItemActionCell();

    const bool pressed = DrawRowHoverButton(id, icon, visible, tooltip, GetItemActionCellSize(), GetRowButtonSize());

    EndItemActionCell();

    return pressed;
}

void StreamTrackImport::drawMainRow(FileActions* actions)
{
    ImGui::PushID("main");

    DrawRowStart(cMaxFiles, isMainPreviewing(), &actions->previewMain);

    ImGui::SameLine(0.0f, 0.0f);
    DrawValueText("0");

    ImGui::TableSetColumnIndex(cFileColumn_File);
    DrawDialogListText(mainFile.name.cstr(), ImGui::GetColorU32(ImGuiCol_Text));

    DrawFormatCells(static_cast<u32>(mainWave->getChannels().size()), mainWave->getSampleRate(), mainWave->getOriginalLoopEndFrame());

    if (DrawRowAction(cFileColumn_Edit, "##edit", ICON_LC_PENCIL, true, messages::import::cTrackImportEditTip))
        actions->edit = cEditMain;

    ImGui::PopID();
}

FileTracks StreamTrackImport::findFileTracks(const std::vector<TrackPlan>& plans, u32 index) const
{
    FileTracks tracks;

    for (u32 p = 0; p < plans.size(); p++)
    {
        for (u32 s = 0; s < plans[p].count; s++)
        {
            if (plans[p].sources[s].file != index)
                continue;

            if (tracks.first < 0)
                tracks.first = static_cast<s32>(p);

            tracks.last = static_cast<s32>(p);

            if (files[index].info.numChannels == 1 && plans[p].count > 1)
                tracks.side = s == 0 ? messages::common::cLeft : messages::common::cRight;
        }
    }

    return tracks;
}

void StreamTrackImport::drawFileRow(u32 index, const FileTracks& tracks, u32 firstTrackIndex, FileActions* actions)
{
    const WaveFile::RiffWaveInfo& info = files[index].info;

    ImGui::PushID(static_cast<int>(index));

    bool play = false;
    const bool rowHovered = DrawRowStart(index, isPreviewing(index), &play);

    if (play)
        actions->preview = static_cast<s32>(index);

    drawFileDragDrop(index, actions);

    sead::FixedSafeString<32> trackText;

    if (tracks.first != tracks.last)
        trackText.format(messages::import::cTrackImportTrackRangeFormat, firstTrackIndex + tracks.first, firstTrackIndex + tracks.last);
    else if (tracks.side)
        trackText.format(messages::import::cTrackImportTrackSideFormat, firstTrackIndex + tracks.first, tracks.side);
    else
        trackText.format("%u", firstTrackIndex + tracks.first);

    ImGui::SameLine(0.0f, 0.0f);
    DrawValueText(trackText.cstr());

    ImGui::TableSetColumnIndex(cFileColumn_File);
    DrawDialogListText(files[index].name.cstr(), ImGui::GetColorU32(ImGuiCol_Text));

    DrawFormatCells(info.numChannels, info.sampleRate, info.sampleCount);

    if (DrawRowAction(cFileColumn_Edit, "##edit", ICON_LC_PENCIL, true, messages::import::cTrackImportEditTip))
        actions->edit = static_cast<s32>(index);

    if (DrawRowAction(cFileColumn_Remove, "##remove", ICON_LC_X, rowHovered, messages::import::cTrackImportRemoveTip))
        actions->remove = static_cast<s32>(index);

    ImGui::PopID();
}

void StreamTrackImport::drawFileDragDrop(u32 index, FileActions* actions) const
{
    if (fileCount > 1 && ImGui::BeginDragDropSource())
    {
        ImGui::SetDragDropPayload(cFileDragType, &index, sizeof(index));
        ImGui::TextUnformatted(files[index].name.cstr());
        ImGui::EndDragDropSource();
    }

    if (!ImGui::BeginDragDropTarget())
        return;

    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(cFileDragType))
    {
        actions->moveFrom = static_cast<s32>(*static_cast<const u32*>(payload->Data));
        actions->moveTo = static_cast<s32>(index);
    }

    ImGui::EndDragDropTarget();
}

void StreamTrackImport::applyFileActions(const FileActions& actions)
{
    if (actions.previewMain)
        toggleMainPreview();

    if (actions.preview >= 0)
        togglePreview(static_cast<u32>(actions.preview));

    if (actions.edit != cEditNone)
        openEditor(actions.edit);

    if (actions.moveFrom >= 0 || actions.remove >= 0)
    {
        stopPreview();

        if (panelOwner >= 0)
            panelOwner = cEditNone;
    }

    if (actions.moveFrom >= 0 && actions.moveFrom != actions.moveTo)
    {
        SourceFile* const first = files + std::min(actions.moveFrom, actions.moveTo);
        SourceFile* const last = files + std::max(actions.moveFrom, actions.moveTo) + 1;

        if (actions.moveFrom < actions.moveTo)
            std::rotate(first, first + 1, last);
        else
            std::rotate(first, last - 1, last);
    }

    if (actions.remove >= 0)
    {
        RemoveEditedFile(&files[actions.remove]);

        for (u32 i = static_cast<u32>(actions.remove); i + 1 < fileCount; i++)
            files[i] = files[i + 1];

        fileCount--;
    }
}

void StreamTrackImport::drawFileTable(const std::vector<TrackPlan>& plans, u32 firstTrackIndex)
{
    const char* const titles[cFileColumn_Num] = { "##Play", "#", messages::common::cFile, messages::common::cChannels, messages::common::cSampleRate, messages::common::cLength, "##Edit", "##Remove" };
    const s32 rowNum = ImClamp(static_cast<s32>(fileCount) + (mainWave ? 1 : 0), 1, cListRowMax);
    const f32 actionWidth = GetItemActionColumnWidth();

    FileActions actions;

    if (!BeginDialogList("##Files", cFileColumn_Num, rowNum))
        return;

    ImGui::TableSetupColumn(titles[cFileColumn_Play], ImGuiTableColumnFlags_WidthFixed, actionWidth);
    ImGui::TableSetupColumn(titles[cFileColumn_Track], ImGuiTableColumnFlags_WidthFixed, CalcColumnWidth(titles[cFileColumn_Track], sead::FormatFixedSafeString<32>(messages::import::cTrackImportTrackSideFormat, 88u, messages::common::cRight).cstr()));
    ImGui::TableSetupColumn(titles[cFileColumn_File], ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn(titles[cFileColumn_Channels], ImGuiTableColumnFlags_WidthFixed, CalcColumnWidth(titles[cFileColumn_Channels], messages::common::cStereo));
    ImGui::TableSetupColumn(titles[cFileColumn_Rate], ImGuiTableColumnFlags_WidthFixed, CalcColumnWidth(titles[cFileColumn_Rate], "000000 Hz"));
    ImGui::TableSetupColumn(titles[cFileColumn_Length], ImGuiTableColumnFlags_WidthFixed, CalcColumnWidth(titles[cFileColumn_Length], "000.00 s"));
    ImGui::TableSetupColumn(titles[cFileColumn_Edit], ImGuiTableColumnFlags_WidthFixed, actionWidth);
    ImGui::TableSetupColumn(titles[cFileColumn_Remove], ImGuiTableColumnFlags_WidthFixed, actionWidth);

    DrawDialogListTitles(titles, cFileColumn_Num, false);

    if (mainWave)
        drawMainRow(&actions);

    for (u32 index = 0; index < fileCount; index++)
        drawFileRow(index, findFileTracks(plans, index), firstTrackIndex, &actions);

    EndDialogList();

    applyFileActions(actions);
}

bool StreamTrackImport::hasLayoutChoice() const
{
    for (u32 i = 0; i < fileCount; i++)
    {
        if (files[i].info.numChannels > 1)
            return true;

        if (i + 1 < fileCount && files[i].info.numChannels == 1 && files[i + 1].info.numChannels == 1)
            return true;
    }

    return false;
}

void StreamTrackImport::drawLayoutRow()
{
    const char* layoutNames[] = { messages::import::cTrackImportLayoutPerFile, messages::import::cTrackImportLayoutPairMono, messages::import::cTrackImportLayoutPerChannel };
    const char* layoutDetails[] = { messages::import::cTrackImportLayoutPerFileDetail, messages::import::cTrackImportLayoutPairMonoDetail, nullptr };

    DialogRow(messages::import::cTrackImportLayout);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ComboScroll("##layout", reinterpret_cast<s32*>(&layout), layoutNames, layoutDetails, IM_ARRAYSIZE(layoutNames));
}

bool StreamTrackImport::drawBody(bool inBox)
{
    SEAD_ASSERT(findLeadWave());

    const std::vector<TrackPlan> plans = buildPlan();
    const u32 firstTrackIndex = findFirstNewTrack() + (mainWave ? 1 : 0);

    sead::FixedSafeString<128> error;
    findLimitError(plans, &error);

    const f32 startY = ImGui::GetCursorPosY();

    ImGui::PushID(this);
    ImGui::Dummy(ImVec2(ImFloor(ImGui::GetFontSize() * cPanelWidthEm), 0.0f));

    const sead::FormatFixedSafeString<64> addLabel("%s %s", ICON_LC_PLUS, messages::import::cTrackImportAddFiles);
    bool addPressed = false;
    const bool isOpen = !inBox || BeginGroupBox(messages::common::cTracks, addLabel.cstr(), &addPressed);

    if (isOpen)
    {
        if (inBox)
            DialogRowGap();

        drawFileTable(plans, firstTrackIndex);

        if (!inBox)
        {
            DialogRowGap();
            addPressed = TextButton(addLabel.cstr());
        }

        if (hasLayoutChoice())
            drawLayoutRow();

        if (inBox)
            EndGroupBox();
    }

    if (!error.isEmpty())
        ImGui::TextColored(uicolors::Marker(), "%s", error.cstr());

    ImGui::PopID();

    bodyHeight = ImGui::GetCursorPosY() - startY;

    if (addPressed)
    {
        std::vector<std::string> paths;

        if (pickPaths(&paths))
            appendPaths(paths, targetSource.sound);
    }

    return error.isEmpty() && (fileCount != 0 || mainWave);
}

void StreamTrackImport::drawHead(const StreamSource& source) const
{
    const WaveFile* leadWave = findLeadWave();
    const u32 rate = leadWave->getSampleRate();
    const char* encodingName = isOpusTarget() ? cBwavCodecNames[static_cast<s32>(aal::BwavCodec::Opus)] : WaveFile::sEncodingTypes[static_cast<s32>(leadWave->getEncoding())];

    const double seconds = rate ? static_cast<double>(leadWave->getOriginalLoopEndFrame()) / rate : 0.0;
    const sead::FormatFixedSafeString<32> length(messages::common::cSecondsFormat, seconds);

    const char* format = leadWave->getIsLoop() ? messages::import::cTrackImportStreamFactsLoopFormat : messages::import::cTrackImportStreamFactsFormat;

    DrawPageHeader(ICON_LC_DISC_3, source.getDisplayName().cstr(), sead::FormatFixedSafeString<128>(format, encodingName, rate, length.cstr()).cstr());
}

void StreamTrackImport::draw(const StreamSource& source)
{
    if (targetSource.stream && source.stream != targetSource.stream)
        return;

    if (drawnFrame == ImGui::GetFrameCount())
        return;

    drawnFrame = ImGui::GetFrameCount();

    ImGui::PushOverrideID(ImHashStr(cTrackDialogId));
    drawTrackDialog(source);
    ImGui::PopID();
}

void StreamTrackImport::drawTrackDialog(const StreamSource& source)
{
    trackDialogOpen = ImGui::IsPopupOpen(cTrackDialogId);

    if (!trackDialogOpen)
        drawEditor();

    if (openRequested)
    {
        ImGui::OpenPopup(cTrackDialogId);
        openRequested = false;
    }

    CenterNextDialog();
    ImGui::SetNextWindowContentSize(ImVec2(ImFloor(ImGui::GetFontSize() * cPanelWidthEm), 0.0f));

    if (!BeginDialog(sead::FormatFixedSafeString<64>(ICON_LC_IMPORT " %s%s", messages::import::cTrackImportTitle, cTrackDialogId).cstr(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    trackDialogOpen = true;

    if (source.stream != targetSource.stream || !findLeadWave())
    {
        reset();
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    drawHead(source);

    const bool canImport = drawBody(false);

    drawEditor();

    BeginDialogFooter({messages::common::cImport, messages::common::cCancel});

    ImGui::BeginDisabled(!canImport);
    const bool importClicked = DialogButton(messages::common::cImport);
    ImGui::EndDisabled();

    const bool cancelClicked = DialogButton(messages::common::cCancel);

    if (importClicked)
        commit();

    if (importClicked || cancelClicked)
    {
        reset();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

}
