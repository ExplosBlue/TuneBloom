#include <ui/stream/StreamTracks.h>

#include <ui/Messages.h>
#include <ui/sound/SoundPanels.h>
#include <ui/stream/StreamPanelFold.h>
#include <ui/stream/StreamRegions.h>
#include <ui/stream/StreamTrackImport.h>
#include <ui/widgets/TabRowTools.h>
#include <ui/app/UI.h>

const char* const cTrackMixLabels[cTrackMixLabelCount]       = { messages::common::cVolume, messages::common::cPan, messages::param::cSurroundPan, messages::stream::cTrackBypass };
const char* const cTrackSendLabels[cTrackSendLabelCount]     = { messages::param::cSendMain, messages::param::cSendFxA, messages::param::cSendFxB, messages::param::cSendFxC };
const char* const cTrackFilterLabels[cTrackFilterLabelCount] = { messages::common::cLpf, messages::param::cBiquad, messages::stream::cTrackBiquadValue };
const char* const cTrackWaveLabel                            = messages::common::cWaveFile;

static Item* sHighlightTrack = nullptr;
static bool sTrackMixerView = false;
static bool sTrackToolsInTabBar = false;

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

static void DrawTrackTools(Sound* sound, bool inTabBar, TrackEdit* edit)
{
    const sead::FormatFixedSafeString<64> limit(messages::stream::cTrackLimitFormat, cStrmTrackNum);
    const bool canAdd = sound && sound->getStreamSoundInfo().getTrackList().size() < static_cast<s32>(cStrmTrackNum);
    const char* reason = sound ? limit.cstr() : messages::stream::cTrackNoSound;

    if (DrawTabRowCommand(ICON_LC_PLUS "###AddTrack", messages::stream::cTrackAddTip, canAdd ? nullptr : reason, inTabBar))
        edit->add = true;

    if (DrawTabRowCommand(ICON_LC_IMPORT "###ImportTracks", messages::import::cTrackImportMenuLabel, canAdd ? nullptr : reason, inTabBar))
        RequestStreamTrackImport(sound);

    if (DrawTabRowTool(sTrackMixerView ? ICON_LC_ROWS_3 "###TrackView" : ICON_LC_SLIDERS_VERTICAL "###TrackView", inTabBar))
        edit->toggleView = true;

    ImGui::SetItemTooltip("%s", sTrackMixerView ? messages::stream::cTrackTableView : messages::stream::cTrackMixerView);

    if (!inTabBar)
        ImGui::NewLine();
}

static void ApplyTrackEdit(Sound* sound, const TrackEdit& edit)
{
    using Track = Sound::StreamSoundInfo::Track;

    Track::List& list = sound->getStreamSoundInfo().getTrackList();

    if (edit.toggleView)
        ToggleTrackMixerView();

    const bool move = edit.moveTrack && edit.moveTrack != edit.moveTarget;

    if (edit.add)
    {
        Track* track = new Track();
        track->setEnableName(true);
        track->getName() = "Track";

        if (edit.insertAfter)
            edit.insertAfter->insertBack(track);
        else
            list.pushBack(track);

        sHighlightTrack = track;
    }

    if (edit.deleteTrack)
    {
        ForgetRemovedItems({ edit.deleteTrack });
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
        sound->getStreamSoundInfo().setMuteTracks(0);
        sound->getStreamSoundInfo().setSoloTracks(0);
        sSoundPlayer.updateStreamTrackMute(sound);
        sBfsar.updateList(list);
        SetUnsavedChanges(true);
    }
}

void DrawStreamTracksUI(Sound* sound)
{
    using Track = Sound::StreamSoundInfo::Track;

    Track::List& list = sound->getStreamSoundInfo().getTrackList();

    TrackContext ctx = {};
    ctx.sound = sound;

    for (Item* trackItem : list)
        ctx.tracks.push_back(static_cast<Track*>(trackItem));

    ctx.enableSend = sBfsar.isStreamSendAvailable();
    ctx.enableFilter = sBfsar.isFilterSupportedVersion();
    ctx.canAdd = ctx.tracks.size() < cStrmTrackNum;
    ctx.biquadWidth = CalcBiquadTypeWidth();

    WaveFile* mainWave = sound->getStreamSoundInfo().getMainWave();
    TrackEdit edit;

    if (!sTrackToolsInTabBar)
        DrawTrackTools(sound, false, &edit);

    if (sTrackMixerView)
        DrawTrackStrips(ctx, &edit);
    else
        DrawTrackTable(ctx, &edit);

    ApplyTrackEdit(sound, edit);
    KeepStreamRegions(sound, mainWave);
}

bool DrawStreamTracksTabRow(Sound* sound)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImGuiDockNode* node = window->DockNode;

    const bool isVisible = node && node->VisibleWindow == window;
    const bool canFold = CanFoldStreamPanel();

    TrackEdit edit;
    bool toggleFold = false;

    sTrackToolsInTabBar = isVisible && BeginTabRowTools(node, CalcTabRowToolsWidth(canFold ? cTrackToolNum + 1 : cTrackToolNum));

    if (sTrackToolsInTabBar)
    {
        DrawTrackTools(sound, true, &edit);

        if (canFold)
            toggleFold = DrawStreamPanelFoldTool();

        EndTabRowTools();
    }

    if (sound)
        ApplyTrackEdit(sound, edit);
    else if (edit.toggleView)
        ToggleTrackMixerView();

    return UpdateStreamPanelFold(toggleFold);
}
