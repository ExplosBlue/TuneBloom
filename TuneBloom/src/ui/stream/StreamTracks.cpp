#include <ui/stream/StreamTracks.h>

#include <messages/Messages.h>
#include <ui/sound/SoundPanels.h>
#include <ui/stream/StreamPanelFold.h>
#include <ui/stream/StreamRegions.h>
#include <ui/stream/StreamTrackImport.h>
#include <ui/widgets/TabRowTools.h>
#include <ui/app/UI.h>
#include <ui/app/Shortcuts.h>

#include <algorithm>

const char* const cTrackMixLabels[cTrackMixLabelCount]       = { messages::common::cVolume, messages::common::cPan, messages::param::cSurroundPan, messages::stream::cTrackBypass };
const char* const cTrackSendLabels[cTrackSendLabelCount]     = { messages::param::cSendMain, messages::param::cSendFxA, messages::param::cSendFxB, messages::param::cSendFxC };
const char* const cTrackFilterLabels[cTrackFilterLabelCount] = { messages::common::cLpf, messages::param::cBiquad, messages::stream::cTrackBiquadValue };
const char* const cTrackWaveLabel                            = messages::common::cWaveFile;

static Item* sHighlightTrack = nullptr;
static bool sTrackMixerView = false;
static bool sTrackToolsInTitleRow = false;

bool IsTrackMixerView()
{
    return sTrackMixerView;
}

void SetTrackMixerView(bool mixer)
{
    sTrackMixerView = mixer;
}

static void ToggleTrackMixerView()
{
    sTrackMixerView = !sTrackMixerView;
    ImGui::MarkIniSettingsDirty();
}

Item* GetHighlightedStreamTrack()
{
    return sHighlightTrack;
}

void HighlightStreamTrack(Item* track)
{
    sHighlightTrack = track;
}

static constexpr s32 cTrackToolNum = 3;

static void DrawTrackTools(const StreamSource& source, bool inTabBar, TrackEdit* edit)
{
    const sead::FormatFixedSafeString<64> limit(messages::stream::cTrackLimitFormat, cStrmTrackNum);
    const bool canAdd = source.isValid() && source.stream->getTrackList().size() < static_cast<s32>(cStrmTrackNum);
    const char* reason = source.isValid() ? limit.cstr() : messages::stream::cTrackNoSound;

    if (DrawTabRowCommand(ICON_LC_PLUS "###AddTrack", messages::stream::cTrackAddTip, canAdd ? nullptr : reason, inTabBar))
        edit->add = true;

    if (DrawTabRowCommand(ICON_LC_IMPORT "###ImportTracks", messages::import::cTrackImportMenuLabel, canAdd ? nullptr : reason, inTabBar))
        RequestStreamTrackImport(source);

    if (source.isValid() && !source.hasTrackMix)
    {
        DrawTabRowCommand(ICON_LC_SLIDERS_VERTICAL "###TrackView", messages::stream::cTrackMixerView, messages::stream::cTrackMixNotInFile, inTabBar);
    }
    else
    {
        if (DrawTabRowTool(sTrackMixerView ? ICON_LC_ROWS_3 "###TrackView" : ICON_LC_SLIDERS_VERTICAL "###TrackView", inTabBar))
            edit->toggleView = true;

        ImGui::SetItemTooltip("%s", sTrackMixerView ? messages::stream::cTrackTableView : messages::stream::cTrackMixerView);
    }

    if (!inTabBar)
        ImGui::NewLine();
}

static void ApplyTrackEdit(const StreamSource& source, const TrackEdit& edit)
{
    using Track = Sound::StreamSoundInfo::Track;

    Sound::StreamSoundInfo& stream = *source.stream;
    Track::List& list = stream.getTrackList();

    if (edit.toggleView)
        ToggleTrackMixerView();

    const bool move = edit.moveTrack && edit.moveTrack != edit.moveTarget;

    if (edit.add)
    {
        Track* track = new Track();
        track->setDefaultName();

        if (edit.insertAfter)
            edit.insertAfter->insertBack(track);
        else
            list.pushBack(track);

        sHighlightTrack = track;
    }

    if (edit.deleteTrack)
    {
        ForgetRemovedItems({ edit.deleteTrack });

        if (sHighlightTrack == edit.deleteTrack)
            sHighlightTrack = nullptr;

        delete edit.deleteTrack;
    }

    if (move)
    {
        const bool moveDown = edit.moveTrack->getId() < edit.moveTarget->getId();

        edit.moveTrack->erase();

        if (moveDown)
            edit.moveTarget->insertBack(edit.moveTrack);
        else
            edit.moveTarget->insertFront(edit.moveTrack);
    }

    if (edit.add || edit.deleteTrack || move)
    {
        stream.setMuteTracks(0);
        stream.setSoloTracks(0);
        sSoundPlayer.updateStreamTrackMute(&stream);
        Bfsar::updateList(list);
        SetUnsavedChanges(true);
    }
}

TrackContext MakeTrackContext(const StreamSource& source)
{
    TrackContext ctx = {};
    ctx.source = source;

    for (Item* trackItem : source.stream->getTrackList())
        ctx.tracks.push_back(static_cast<Sound::StreamSoundInfo::Track*>(trackItem));

    ctx.canAdd = ctx.tracks.size() < cStrmTrackNum;
    ctx.biquadWidth = CalcBiquadTypeWidth();

    return ctx;
}

static void HandleTrackDeleteKey(const TrackContext& ctx, TrackEdit* edit)
{
    if (edit->deleteTrack || !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::GetIO().WantTextInput)
        return;

    const auto highlighted = std::find(ctx.tracks.begin(), ctx.tracks.end(), GetHighlightedStreamTrack());

    if (highlighted != ctx.tracks.end() && shortcuts::Pressed(shortcuts::Action::DeleteSelection))
        edit->deleteTrack = *highlighted;
}

void DrawStreamTracksUI(const StreamSource& source)
{
    TrackContext ctx = MakeTrackContext(source);

    WaveFile* mainWave = source.stream->getMainWave();
    TrackEdit edit;

    if (!sTrackToolsInTitleRow)
        DrawTrackTools(source, false, &edit);

    if (sTrackMixerView && source.hasTrackMix)
        DrawTrackStrips(ctx, &edit);
    else
        DrawTrackTable(ctx, &edit);

    HandleTrackDeleteKey(ctx, &edit);
    ApplyTrackEdit(source, edit);
    KeepStreamRegions(source.stream->getMainWave(), mainWave);
}

bool DrawStreamTracksTabRow(const StreamSource& source)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImGuiDockNode* node = window->DockNode;

    const bool isVisible = node && node->VisibleWindow == window;
    const bool canFold = CanFoldStreamPanel();

    TrackEdit edit;
    bool toggleFold = false;

    sTrackToolsInTitleRow = isVisible && BeginTabRowTools(node, CalcTabRowToolsWidth(canFold ? cTrackToolNum + 1 : cTrackToolNum));

    if (sTrackToolsInTitleRow)
    {
        DrawTrackTools(source, true, &edit);

        if (canFold)
            toggleFold = DrawStreamPanelFoldTool();

        EndTabRowTools();
    }

    if (source.isValid())
        ApplyTrackEdit(source, edit);
    else if (edit.toggleView)
        ToggleTrackMixerView();

    return UpdateStreamPanelFold(toggleFold);
}

void DrawStreamTracksSection(const StreamSource& source, f32 height)
{
    TrackEdit edit;

    BeginTitleRowTools(messages::common::cTracks, CalcTabRowToolsWidth(cTrackToolNum));
    DrawTrackTools(source, false, &edit);
    ApplyTrackEdit(source, edit);

    sTrackToolsInTitleRow = true;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, ImGui::GetStyle().ChildBorderSize));
    const bool open = ImGui::BeginChild("TracksSection", ImVec2(0.0f, height), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();

    if (open)
        DrawStreamTracksUI(source);

    ImGui::EndChild();

    DrawStreamTrackImportPopup(source);
}
