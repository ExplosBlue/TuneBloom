#include <ui/stream/StreamTracks.h>

#include <messages/Messages.h>
#include <ui/sound/SoundPanels.h>
#include <ui/stream/StreamPanelFold.h>
#include <ui/stream/StreamRegions.h>
#include <ui/stream/StreamTrackAdd.h>
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
static bool sTrackToolsInTitleRow = false;

Item* GetHighlightedStreamTrack()
{
    return sHighlightTrack;
}

void HighlightStreamTrack(Item* track)
{
    sHighlightTrack = track;
}

static constexpr s32 cTrackToolNum = 1;

static void DrawTrackTools(const StreamSource& source, bool inTabBar)
{
    DrawTrackAddButton(source, inTabBar);

    if (!inTabBar)
        ImGui::NewLine();
}

static void ApplyTrackEdit(const StreamSource& source, const TrackEdit& edit)
{
    using Track = Sound::StreamSoundInfo::Track;

    Sound::StreamSoundInfo& stream = *source.stream;
    Track::List& list = stream.getTrackList();

    const bool move = edit.moveTrack && edit.moveTrack != edit.moveTarget;

    if (edit.deleteTrack)
    {
        ForgetRemovedItems({ edit.deleteTrack });

        if (sHighlightTrack == edit.deleteTrack)
            sHighlightTrack = nullptr;

        sSoundPlayer.forgetStreamTrack(edit.deleteTrack);
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

    if (edit.deleteTrack || move)
    {
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

    ctx.biquadWidth = CalcBiquadTypeWidth();

    return ctx;
}

static void HandleTrackDeleteKey(const TrackContext& ctx, TrackEdit* edit)
{
    if (edit->deleteTrack || !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows | ImGuiFocusedFlags_NoPopupHierarchy) || ImGui::GetIO().WantTextInput)
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
        DrawTrackTools(source, false);

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

    bool toggleFold = false;

    sTrackToolsInTitleRow = isVisible && BeginTabRowTools(node, CalcTabRowToolsWidth(canFold ? cTrackToolNum + 1 : cTrackToolNum));

    if (sTrackToolsInTitleRow)
    {
        DrawTrackTools(source, true);

        if (canFold)
            toggleFold = DrawStreamPanelFoldTool();

        EndTabRowTools();
    }

    return UpdateStreamPanelFold(toggleFold);
}

void DrawStreamTracksSection(const StreamSource& source, f32 reserveBelow)
{
    BeginTitleRowTools(messages::common::cTracks, CalcTabRowToolsWidth(cTrackToolNum));
    DrawTrackTools(source, false);

    sTrackToolsInTitleRow = true;

    f32 height = 0.0f;

    if (reserveBelow > 0.0f)
    {
        const f32 fitHeight = CalcTrackTableFitHeight(MakeTrackContext(source)) + ImGui::GetStyle().ChildBorderSize * 2.0f;

        height = sead::Mathf::max(ImGui::GetFrameHeight(), sead::Mathf::min(fitHeight, ImGui::GetContentRegionAvail().y - reserveBelow));
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, ImGui::GetStyle().ChildBorderSize));
    const bool open = ImGui::BeginChild("TracksSection", ImVec2(0.0f, height), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();

    if (open)
        DrawStreamTracksUI(source);

    ImGui::EndChild();

    DrawStreamTrackImportPopup(source);
}
