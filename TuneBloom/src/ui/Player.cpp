#include <ui/UI.h>
#include <ui/Messages.h>

#include <ui/PopupMgr.h>
#include <ui/TimeUtil.h>

#include <bfsar/SoundPlayer.h>

#include <snd/ChannelMgr.h>
#include <snd/MultiVoiceMgr.h>
#include <snd/Voice.h>
#include <snd/VoiceImpl.h>
#include <snd/SoundSystem.h>

#include <snd/snd_SequenceSoundFileReader.h>

#include <basis/seadWarning.h>
#include <filedevice/seadFileDeviceMgr.h>
#include <filedevice/seadPath.h>
#include <heap/seadExpHeap.h>

// Players

const Item* Player::validate(sead::BufferedSafeString& error) const
{
    if (!Item::validateName(error))
    {
        return this;
    }

    return nullptr;
}

static void PlayerCreatePropertiesCallback(bool clear, Item *item, bool *validate)
{
    static u32 sPlayableSoundMax = 1;
    static bool sEnablePlayerHeapSize = true;
    static u32 sPlayerHeapSize = 0;

    if (clear)
    {
        sPlayableSoundMax = 1;
        sEnablePlayerHeapSize = true;
        sPlayerHeapSize = 0;
        return;
    }

    if (!item && !validate)
    {
        const ImU32 cStepU32 = 1;

        ImGui::InputScalar("Playable Sound Max", ImGuiDataType_U32, &sPlayableSoundMax, &cStepU32);

        ImGui::Checkbox("Enable Player Heap Size", &sEnablePlayerHeapSize);

        if (!sEnablePlayerHeapSize)
            ImGui::BeginDisabled();

        ImGui::InputScalar("Player Heap Size", ImGuiDataType_U32, &sPlayerHeapSize, &cStepU32);

        if (!sEnablePlayerHeapSize)
            ImGui::EndDisabled();
    }
    else if (item && !validate)
    {
        Player *player = static_cast<Player *>(item);
        player->setPlayableSoundMax(sPlayableSoundMax);
        player->setEnablePlayerHeapSize(sEnablePlayerHeapSize);
        player->setPlayerHeapSize(sPlayerHeapSize);
    }
}

InstanciateItemCallback CreatePlayerFunc(bool clear)
{
    return CreateItemFunc(clear, []() -> Item * { return new Player(); }, &PlayerCreatePropertiesCallback);
}

void DrawPlayersUI()
{
    static SortState sSortState;

    DrawSortToolbar(sSortState);
    DrawTabFilterBar();

    DrawAllItemsUI("Player", sBfsar.getPlayerList(),
                   &CreatePlayerFunc, nullptr, nullptr, GetItemFilterCallback(),
                   false, nullptr, sSortState.mode, sSortState.ascending);
}

void DrawPlayerPropertiesUI()
{
    Player *player = static_cast<Player *>(sSelectedItem);

    const ImU32 cStepU32 = 1;

    {
        u32 playableSoundMax = player->getPlayableSoundMax();
        if (ImGui::InputScalar("Playable Sound Max", ImGuiDataType_U32, &playableSoundMax, &cStepU32))
        {
            player->setPlayableSoundMax(playableSoundMax);
            SetUnsavedChanges(true);
        }
    }

    bool enablePlayerHeapSize = player->isEnablePlayerHeapSize();
    if (ImGui::Checkbox("Enable Player Heap Size", &enablePlayerHeapSize))
    {
        player->setEnablePlayerHeapSize(enablePlayerHeapSize);
        SetUnsavedChanges(true);
    }

    if (!enablePlayerHeapSize)
        ImGui::BeginDisabled();

    {
        u32 playerHeapSize = player->getPlayerHeapSize();
        if (ImGui::InputScalar("Player Heap Size", ImGuiDataType_U32, &playerHeapSize, &cStepU32))
        {
            player->setPlayerHeapSize(playerHeapSize);
            SetUnsavedChanges(true);
        }
    }

    if (!enablePlayerHeapSize)
        ImGui::EndDisabled();
}

// Runtime Player

SoundPlayer sSoundPlayer;

static void DrawTimeText(f32 seconds)
{
    char clock[16];
    timeutil::FormatClock(clock, sizeof(clock), seconds);

    ImGui::TextUnformatted(clock);
}

constexpr float cSeekBarRounding      = 2.0f;
constexpr float cLoopMarkerThickness  = 2.0f;
constexpr ImU32 cLoopStartMarkerColor = IM_COL32(0, 255, 0, 255);
constexpr ImU32 cLoopEndMarkerColor   = IM_COL32(255, 0, 0, 255);

bool DrawSeekBar(const char* id, f32 width, f32 fraction, bool determinate,
                 bool seekable, bool allowDrag,
                 f32 loopStartFrac, f32 loopEndFrac, f32& outSeekFraction)
{
    ImGui::PushID(id);

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, cSeekBarRounding);
    ImVec4 fillColor = gAccentColor;
    fillColor.w = 1.0f;
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, fillColor);

    ImVec2 barStart = ImGui::GetCursorScreenPos();
    barStart.y -= 1;

    if (determinate)
    {
        ImGui::ProgressBar(fraction, ImVec2(width, 0.0f), "");
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::ProgressBar(1.0f, ImVec2(width, 0.0f), "");
        ImGui::PopStyleColor();
    }

    const f32 barHeight = ImGui::GetItemRectSize().y;

    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID canSeekKey = ImGui::GetID("##canSeek");
    bool canSeek = storage->GetBool(canSeekKey, false);

    if (ImGui::IsItemHovered() && seekable)
    {
        canSeek = true;
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    const bool wantSeek = ImGui::IsMouseClicked(ImGuiMouseButton_Left) || (allowDrag && ImGui::IsMouseDragging(ImGuiMouseButton_Left));
    bool seeked = false;

    if (canSeek && wantSeek)
    {
        const f32 barEnd = barStart.x + width;
        const f32 mouseX = sead::Mathf::clamp2(barStart.x, ImGui::GetMousePos().x, barEnd);
        outSeekFraction = (mouseX - barStart.x) / width;
        seeked = true;
    }
    else
    {
        canSeek = false;
    }

    storage->SetBool(canSeekKey, canSeek);

    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    ImDrawList* draw = ImGui::GetWindowDrawList();

    if (loopStartFrac >= 0.0f)
    {
        const f32 markerX = barStart.x + loopStartFrac * width;
        draw->AddLine(ImVec2(markerX, barStart.y), ImVec2(markerX, barStart.y + barHeight), cLoopStartMarkerColor, cLoopMarkerThickness);
    }

    if (loopEndFrac >= 0.0f)
    {
        const f32 markerX = barStart.x + loopEndFrac * width;
        draw->AddLine(ImVec2(markerX, barStart.y), ImVec2(markerX, barStart.y + barHeight), cLoopEndMarkerColor, cLoopMarkerThickness);
    }

    ImGui::PopID();
    return seeked;
}

static const WaveFile* ResolvePrimaryWaveFile(const Sound* sound)
{
    if (sound->getSoundType() == Sound::SoundType::Wave)
        return static_cast<const WaveFile*>(sound->getWaveSoundInfo().getWaveFileRef().getItem());

    if (sound->getSoundType() == Sound::SoundType::Strm && !sound->getStreamSoundInfo().getTrackList().isEmpty())
    {
        Sound::StreamSoundInfo::Track* track = static_cast<Sound::StreamSoundInfo::Track*>(sound->getStreamSoundInfo().getTrackList().front()->val());
        return static_cast<const WaveFile*>(track->getWaveFileRef().getItem());
    }

    return nullptr;
}

struct PlaybackProgress
{
    f32 fraction = 0.0f;
    f32 elapsedSec = 0.0f;
    f32 totalSec = 0.0f;

    bool determinate = true;
    bool seekable = false;
    bool allowDrag = false;
    bool showTotalTime = true;

    f32 loopStartFrac = -1.0f;
    f32 loopEndFrac = -1.0f;
};

static PlaybackProgress BuildPlaybackProgress()
{
    const bool isSeq = sSoundPlayer.isCurrentPlayerSequence();
    const bool active = sSoundPlayer.isActive();

    u32 sampleCount = sSoundPlayer.getSampleCount();
    u32 sampleRate = sSoundPlayer.getSampleRate();

    PlaybackProgress progress;
    progress.allowDrag = !isSeq;

    if (isSeq)
    {
        if (sSoundPlayer.seqHasFiniteTotal() && sSoundPlayer.getSeqTotalTicks() > 0)
        {
            u32 tot = sSoundPlayer.getSeqTotalTicks();
            u32 cur = active ? sSoundPlayer.getSeqCurrentTick() : 0;
            progress.fraction = sead::Mathf::clamp2(0.0f, static_cast<f32>(cur) / static_cast<f32>(tot), 1.0f);
            progress.totalSec = sSoundPlayer.getSeqTotalSeconds();
            progress.elapsedSec = progress.totalSec * progress.fraction;
        }
        else
        {
            progress.determinate = !active;
        }
    }
    else
    {
        s32 currentSample = sSoundPlayer.getPlaySamplePosition(true);
        const u32 elapsedSamples = currentSample > 0 ? static_cast<u32>(currentSample) : 0;
        progress.fraction = sampleCount != 0 ? sead::Mathf::clamp2(0.0f, static_cast<f32>(currentSample) / static_cast<f32>(sampleCount), 1.0f) : 0.0f;
        progress.elapsedSec = static_cast<f32>(timeutil::SamplesToSeconds(elapsedSamples, sampleRate));
    }

    if (!isSeq || !active)
    {
        const WaveFile *wave = nullptr;

        if (active && sSoundPlayer.getPlayingWaveFile())
        {
            wave = sSoundPlayer.getPlayingWaveFile();
        }
        else if (sSelectedItem && (sSelectedItem->getItemType() == Item::ItemType::WaveFile || sSelectedItem->getItemType() == Item::ItemType::Sound))
        {
            if (sSelectedItem->getItemType() == Item::ItemType::WaveFile)
                wave = static_cast<WaveFile *>(sSelectedItem);
            else
                wave = ResolvePrimaryWaveFile(static_cast<Sound *>(sSelectedItem));
        }
        else if (sSoundPlayer.getLastPlayedSound())
        {
            wave = ResolvePrimaryWaveFile(sSoundPlayer.getLastPlayedSound());
        }

        if (wave)
        {
            sampleCount = wave->getSampleCount();
            sampleRate = wave->getSampleRate();
            progress.totalSec = static_cast<f32>(timeutil::SamplesToSeconds(sampleCount, sampleRate));

            if (wave->getIsLoop())
                progress.loopStartFrac = wave->getOriginalLoopStartFrame() / static_cast<f32>(sampleCount);

            progress.loopEndFrac = wave->getOriginalLoopEndFrame() / static_cast<f32>(sampleCount);
        }
    }

    progress.seekable = active && progress.determinate && (!isSeq || sSoundPlayer.seqHasFiniteTotal());
    progress.showTotalTime = !(isSeq && !sSoundPlayer.seqHasFiniteTotal());

    return progress;
}

constexpr f32 cVolumeSliderWidth = 120.0f;

static const char* MasterVolumeIcon()
{
    if (gMasterVolume <= 0.0f)
        return ICON_LC_VOLUME_X;

    if (gMasterVolume <= 0.5f)
        return ICON_LC_VOLUME_1;

    return ICON_LC_VOLUME_2;
}

static f32 MasterVolumeWidth()
{
    return ImGui::CalcTextSize(MasterVolumeIcon()).x + cVolumeSliderWidth;
}

static void DrawMasterVolume()
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(MasterVolumeIcon());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(cVolumeSliderWidth);

    int volPct = static_cast<int>(gMasterVolume * 100.0f + 0.5f);

    if (ImGui::SliderInt("##vol", &volPct, 0, 100, "%d%%"))
    {
        gMasterVolume = sead::Mathf::clamp2(0.0f, volPct / 100.0f, 1.0f);
        snd::SoundSystem::setMasterVolume(gMasterVolume);
    }

    if (ImGui::IsItemDeactivatedAfterEdit())
        SaveAudioConfig();
}

static void DrawTransportBar(const PlaybackProgress& progress)
{
    const f32 spacing = ImGui::GetStyle().ItemSpacing.x;

    DrawTimeText(progress.elapsedSec);
    ImGui::SameLine();
    const f32 reserveRight = ImGui::CalcTextSize("00:00.000").x + MasterVolumeWidth() + spacing * 4.0f;
    f32 seekBarWidth = ImGui::GetContentRegionAvail().x - reserveRight;
    if (seekBarWidth < 60.0f)
        seekBarWidth = 60.0f;

    f32 seekFrac = 0.0f;

    if (DrawSeekBar("##transport", seekBarWidth, progress.fraction, progress.determinate, progress.seekable, progress.allowDrag, progress.loopStartFrac, progress.loopEndFrac, seekFrac))
        sSoundPlayer.seek(seekFrac);

    ImGui::SameLine();
    if (!progress.showTotalTime)
        ImGui::TextDisabled("%s", ICON_LC_REPEAT);
    else
        DrawTimeText(progress.totalSec);
}

static void DrawPlaybackProgress()
{
    const PlaybackProgress progress = BuildPlaybackProgress();

    DrawTransportBar(progress);
    ImGui::SameLine();
    DrawMasterVolume();
}

static void DrawDebugPanels()
{
    if (false)
    {
        if (ImGui::Begin("Wave"))
        {
            f32* buf = snd::SoundSystem::getWave();
            f32* fft = snd::SoundSystem::calcFFT();

            ImGui::PlotLines("##Wave", buf, snd::SoundSystem::cSamplePerFrame, 0, "Wave", -1, 1, ImVec2(264, 80));
            ImGui::PlotHistogram("##FFT", fft, snd::SoundSystem::cSamplePerFrame / 2, 0, "FFT", 0, 10, ImVec2(264, 80), 8);
        }
        ImGui::End();
    }

    if (false)
    {
        if (ImGui::Begin("Voices"))
        {
            snd::internal::driver::SoundThreadLock lock;
            ImGui::Text("Channel Count: %d", snd::internal::driver::ChannelMgr::instance()->getChannelCount());
            ImGui::Text("MultiVoice Count: %d", snd::internal::driver::MultiVoiceMgr::instance()->getVoiceCount());
            ImGui::Text("MultiVoice Active Count: %d", snd::internal::driver::MultiVoiceMgr::instance()->getActiveCount());
            ImGui::Text("Voice Count: %d", snd::internal::Voice::detail_getVoiceMgr()->getActiveVoiceCount());
        }
        ImGui::End();
    }
}

static void DrawTransportWindow()
{
    if (ImGui::Begin(ICON_LC_MUSIC " Player###PlayerWindow"))
    {
        ImGuiWindow* win = ImGui::GetCurrentWindow();
        if (win && win->DockNode)
            win->DockNode->LocalFlags |= ImGuiDockNodeFlags_NoResize;

        bool isPause = sSoundPlayer.isPause();

        const bool canTransport = sSoundPlayer.isCurrentPlayer() && (sSoundPlayer.isActive() || sSoundPlayer.getLastPlayedSound());

        ImGui::BeginDisabled(!canTransport);

        if (ImGui::Button(isPause ? ICON_LC_PLAY : ICON_LC_PAUSE))
        {
            if (!sSoundPlayer.isActive())
            {
                sSoundPlayer.playLastSound();
            }
            else
            {
                sSoundPlayer.pause(!isPause);
            }
        }

        ImGui::EndDisabled();

        if (!canTransport)
        {
            SetDisabledTooltip(messages::playback::cNothingToPlay);
        }
        else if (!sSoundPlayer.isActive() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone))
        {
            ImGui::SetTooltip("Last Sound '%s'", sSoundPlayer.getLastPlayedSound()->getFormattedName().cstr());
        }

        ImGui::SameLine();
        if (ImGui::ButtonEx(ICON_LC_SQUARE, ImVec2(0.0f, 0.0f), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle))
        {
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Middle))
            {
                sSoundPlayer.stopAllPlayers(true);
                sSoundPlayer.stopAllVoices();
            }
            else
            {
                sSoundPlayer.stopAllPlayers(false);
            }
        }
    
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone))
        {
            ImGui::SetTooltip("Middle click to kill all Voices");
        }

        ImGui::SameLine();
        DrawPlaybackProgress();
    }
    ImGui::End();
}

static void DrawPlayerParametersWindow()
{
    if (ImGui::Begin(ICON_LC_SETTINGS_2 " Player Parameters###PlayerParamWindow"))
    {
        sSoundPlayer.drawParameters();
    }
    ImGui::End();
}

static void DrawSequenceVariablesWindow()
{
    if (ImGui::Begin(ICON_LC_BINARY " Sequence Variables###SequenceVarWindow"))
    {
        sSoundPlayer.drawSeqVars();
    }
    ImGui::End();
}

void DrawPlayerUI()
{
    DrawDebugPanels();
    DrawTransportWindow();
    DrawPlayerParametersWindow();
    DrawSequenceVariablesWindow();
    sSoundPlayer.update();
}
