#include <ui/stream/LooseStreamInfo.h>

#include <ui/app/Shortcuts.h>
#include <ui/file/LooseFileInfo.h>
#include <ui/items/Selection.h>
#include <messages/Messages.h>
#include <ui/sound/SoundViews.h>
#include <ui/stream/OpusBitrate.h>
#include <ui/stream/StreamRegions.h>
#include <ui/stream/StreamTracks.h>
#include <ui/widgets/Knob.h>
#include <ui/widgets/PanelLayout.h>
#include <ui/widgets/Properties.h>

#include <imgui/imgui_internal.h>

#include <bfsar/files/LooseStreamFile.h>

static constexpr f32 cRegionsSectionEm = 11.0f;

static bool sPopupWasOpen = false;

LooseFileFormat GetLooseStreamFormat(const LooseStreamFile& file)
{
    if (!file.isOpus())
        return GetLooseFileFormat(file.getTarget(), InnerFileKind::Stream);

    LooseFileFormat format;
    format.name = messages::stream::cTypeOpus;
    format.extension = cOpusExtension;
    format.endian = sead::Endian::eLittle;

    return format;
}

static void DrawStreamFileBox(const LooseStreamFile& file)
{
    if (!BeginPropertyBox("StreamFile"))
        return;

    RegionHeader(messages::common::cFile);
    DrawLooseFileRows(GetLooseStreamFormat(file));

    EndPropertyBox();
}

static void DrawStreamContentBox(LooseStreamFile& file)
{
    const Sound::StreamSoundInfo& stream = file.getStream();

    if (!BeginPropertyBox("StreamContent"))
        return;

    RegionHeader(messages::common::cStream);

    PropertyRow(messages::common::cTracks);
    ImGui::Text("%u", static_cast<u32>(stream.getTrackList().size()));

    PropertyRow(messages::common::cChannels);
    ImGui::Text("%u", static_cast<u32>(stream.getAllocateChannelCount()));

    if (file.isOpus())
        DrawOpusBitrateRow(file.getStreamSource());

    EndPropertyBox();
}

void DrawLooseStreamInfoUI(LooseStreamFile& file)
{
    DrawStreamFileBox(file);
    DrawStreamContentBox(file);
}

static Sound::StreamSoundInfo::Track* FindTrackOfWave(const StreamSource& source, const Item* wave)
{
    for (Item* item : source.stream->getTrackList())
    {
        Sound::StreamSoundInfo::Track* track = static_cast<Sound::StreamSoundInfo::Track*>(item);

        if (wave && track->getWaveFileRef().getItem() == wave)
            return track;
    }

    return nullptr;
}

static Item* GetTrackWave(Item* track)
{
    return static_cast<Sound::StreamSoundInfo::Track*>(track)->getWaveFileRef().getItem();
}

static void SyncTrackHighlight(const StreamSource& source)
{
    Item* track = GetHighlightedStreamTrack();
    const Item* selected = sSelectedItemIsSubWindow ? nullptr : sSelectedItem;
    const bool isOwnTrack = track && track->list() == &source.stream->getTrackList();

    if (isOwnTrack && GetTrackWave(track) == selected)
        return;

    HighlightStreamTrack(FindTrackOfWave(source, selected));
}

static void SelectHighlightedTrackWave(const Item* previousTrack)
{
    Item* track = GetHighlightedStreamTrack();

    if (!track || track == previousTrack)
        return;

    if (Item* wave = GetTrackWave(track))
        SelectItem(wave);
    else
        ClearSelection();
}

static void HandleClearSelection()
{
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput;
    const bool onItem = ImGui::IsAnyItemHovered() || GImGui->HoveredIdDisabled;
    const bool emptyClick = !sPopupWasOpen && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && !onItem;

    if (emptyClick || (focused && shortcuts::Pressed(shortcuts::Action::ClearSelection)))
        ClearSelection();

    sPopupWasOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
}

void DrawLooseStreamContentUI(LooseStreamFile& file)
{
    const StreamSource source = file.getStreamSource();
    const bool hasRegions = source.hasRegionSupport();
    const f32 regionsHeight = ImFloor(ImGui::GetFontSize() * cRegionsSectionEm);

    SyncTrackHighlight(source);

    const Item* previousTrack = GetHighlightedStreamTrack();

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImFloor(ImGui::GetFontSize() * cSoundPanelPadYEm) + ImGui::GetStyle().FrameBorderSize);
    DrawStreamTracksSection(source, hasRegions ? -regionsHeight : 0.0f);

    if (hasRegions)
        DrawStreamRegionsSection();

    SelectHighlightedTrackWave(previousTrack);
    HandleClearSelection();
}
