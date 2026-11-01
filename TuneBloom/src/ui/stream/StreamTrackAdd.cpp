#include <ui/stream/StreamTrackAdd.h>

#include <bfsar/files/StreamWaveOwner.h>
#include <bfsar/model/StreamSource.h>
#include <messages/Messages.h>
#include <ui/items/ItemBrowser.h>
#include <ui/stream/StreamTrackImport.h>
#include <ui/widgets/ContextMenu.h>
#include <ui/widgets/TabRowTools.h>
#include <libs/IconsLucide.h>
#include <imgui/imgui.h>
#include <snd/StreamSoundPlayer.h>

static constexpr const char* cAddMenuId     = "##TrackAddMenu";
static constexpr const char* cWaveBrowserId = "##TrackWaveAdd";

static const Sound::StreamSoundInfo* sWaveBrowserStream = nullptr;
static bool sWaveBrowserOpen = false;

static const Item::List* GetArchiveWaves(const StreamSource& source)
{
    return source.waveOwner ? source.waveOwner->getArchiveWaveList() : nullptr;
}

static const char* GetTrackAddBlockReason(const StreamSource& source, sead::BufferedSafeString* limitText)
{
    if (!source.isValid())
        return messages::stream::cTrackNoSound;

    if (source.stream->getTrackList().size() < static_cast<s32>(cStrmTrackNum))
        return nullptr;

    limitText->format(messages::stream::cTrackLimitFormat, cStrmTrackNum);

    return limitText->cstr();
}

static void DrawTrackAddItems(const StreamSource& source)
{
    if (ContextMenuItem(ICON_LC_IMPORT, messages::sound::cWaveSourceImport))
        RequestStreamTrackImport(source);

    const Item::List* waves = GetArchiveWaves(source);
    const bool hasWaves = waves && waves->front() != nullptr;

    if (!ContextMenuItem(ICON_LC_FILE_AUDIO, messages::common::cWaveFile, hasWaves, messages::group::cAddNoItems))
        return;

    sWaveBrowserStream = source.stream;
    sWaveBrowserOpen = true;
}

void DrawTrackAddButton(const StreamSource& source, bool inTabBar)
{
    sead::FixedSafeString<64> limitText;
    const char* disabledReason = GetTrackAddBlockReason(source, &limitText);
    const bool pressed = DrawTabRowCommand(ICON_LC_PLUS "###AddTrack", messages::import::cTrackImportMenuLabel, disabledReason, inTabBar);
    const ImVec2 buttonMin = ImGui::GetItemRectMin();
    const ImVec2 buttonMax = ImGui::GetItemRectMax();

    if (pressed && !GetArchiveWaves(source))
        RequestStreamTrackImport(source);
    else if (pressed)
        ImGui::OpenPopup(cAddMenuId);

    PlaceContextMenuBelow(cAddMenuId, buttonMin, buttonMax, true);

    if (!BeginContextMenu(cAddMenuId))
        return;

    if (source.isValid())
        DrawTrackAddItems(source);

    EndContextMenu();
}

void DrawTrackAddMenu(const StreamSource& source)
{
    sead::FixedSafeString<64> limitText;
    const char* disabledReason = GetTrackAddBlockReason(source, &limitText);

    if (disabledReason || !GetArchiveWaves(source))
    {
        if (ContextMenuItem(ICON_LC_PLUS, messages::import::cTrackImportMenuLabel, !disabledReason, disabledReason))
            RequestStreamTrackImport(source);

        return;
    }

    if (!BeginContextSubMenu(ICON_LC_PLUS, messages::import::cTrackImportMenuLabel))
        return;

    DrawTrackAddItems(source);
    EndContextSubMenu();
}

void DrawTrackWaveBrowser(const StreamSource& source)
{
    const Item::List* waves = GetArchiveWaves(source);

    if (!waves || !source.isValid() || sWaveBrowserStream != source.stream)
        return;

    if (sWaveBrowserOpen)
        OpenItemBrowser(cWaveBrowserId, *waves, nullptr);

    sWaveBrowserOpen = false;

    Item* wave = nullptr;

    if (!DrawItemBrowser(cWaveBrowserId, *waves, &wave, false, messages::import::cTrackImportMenuLabel) || !wave)
        return;

    sWaveBrowserStream = nullptr;
    RequestStreamTrackWaveImport(source, nullptr, static_cast<WaveFile*>(wave));
}
