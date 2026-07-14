#include <ui/UI.h>

#include <ui/PopupMgr.h>

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
    if (seconds < 0.0f)
        seconds = 0.0f;
    
    u32 m = static_cast<u32>(seconds) / 60;
    u32 s = static_cast<u32>(seconds) % 60;
    u32 ms = static_cast<u32>(seconds * 1000.0f) % 1000;

    ImGui::Text("%02u:%02u.%03u", m, s, ms);
}

static void DrawPlaybackProgress()
{
    const bool isSeq = sSoundPlayer.isCurrentPlayerSequence();
    const bool active = sSoundPlayer.isActive();

    u32 sampleCount = sSoundPlayer.getSampleCount();
    u32 sampleRate = sSoundPlayer.getSampleRate();

    f32 fraction = 0.0f;
    f32 elapsedSec = 0.0f;
    f32 totalSec = 0.0f;

    bool determinate = true;

    if (isSeq)
    {
        if (sSoundPlayer.seqHasFiniteTotal() && sSoundPlayer.getSeqTotalTicks() > 0)
        {
            u32 tot = sSoundPlayer.getSeqTotalTicks();
            u32 cur = active ? sSoundPlayer.getSeqCurrentTick() : 0;
            fraction = sead::Mathf::clamp2(0.0f, static_cast<f32>(cur) / static_cast<f32>(tot), 1.0f);
            totalSec = sSoundPlayer.getSeqTotalSeconds();
            elapsedSec = totalSec * fraction;
        }
        else
        {
            determinate = !active;
        }
    }
    else
    {
        s32 currentSample = sSoundPlayer.getPlaySamplePosition(true);
        fraction = sampleCount != 0 ? sead::Mathf::clamp2(0.0f, static_cast<f32>(currentSample) / static_cast<f32>(sampleCount), 1.0f) : 0.0f;
        elapsedSec = sampleRate != 0 ? static_cast<f32>(currentSample) / static_cast<f32>(sampleRate) : 0.0f;
    }

    const char *volIcon = ICON_LC_VOLUME_2;

    if (gMasterVolume <= 0.5f)
        volIcon = ICON_LC_VOLUME_1;
    
    if (gMasterVolume <= 0.0f)
        volIcon = ICON_LC_VOLUME_X;

    const f32 volSliderW = 120.0f;
    const f32 spacing = ImGui::GetStyle().ItemSpacing.x;

    DrawTimeText(elapsedSec);
    ImGui::SameLine();

    f32 reserveRight = ImGui::CalcTextSize("00:00.000").x + ImGui::CalcTextSize(volIcon).x + volSliderW + spacing * 4.0f;
    f32 adjustSize = ImGui::GetContentRegionAvail().x - reserveRight;
    if (adjustSize < 60.0f)
        adjustSize = 60.0f;

    {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
        ImVec4 fillCol = gAccentColor;
        fillCol.w = 1.0f;
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, fillCol);

        f32 barStartX = ImGui::GetCursorPosX();
        ImVec2 barStartScreenPos = ImGui::GetCursorScreenPos();
        barStartScreenPos.y -= 1;

        if (determinate)
        {
            ImGui::ProgressBar(fraction, ImVec2(adjustSize, 0.0f), "");
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::ProgressBar(1.0f, ImVec2(adjustSize, 0.0f), "");
            ImGui::PopStyleColor();
        }

        f32 barYSize = ImGui::GetItemRectSize().y;

        bool hoverSeekable = active && determinate && (!isSeq || sSoundPlayer.seqHasFiniteTotal());
        static bool sCanSeek = false;

        if (ImGui::IsItemHovered() && hoverSeekable)
        {
            sCanSeek = true;
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }

        bool wantSeek = ImGui::IsMouseClicked(ImGuiMouseButton_Left) || (!isSeq && ImGui::IsMouseDragging(ImGuiMouseButton_Left));
        
        if (sCanSeek && wantSeek)
        {
            f32 barEndX = adjustSize + barStartX;
            f32 mouseX = ImGui::GetMousePos().x - ImGui::GetWindowPos().x;
            mouseX = sead::Mathf::clamp2(barStartX, mouseX, barEndX);
            sSoundPlayer.seek((mouseX - barStartX) / (barEndX - barStartX));
        }
        else
        {
            sCanSeek = false;
        }

        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

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
                {
                    wave = static_cast<WaveFile *>(sSelectedItem);
                }
                else
                {
                    Sound *sound = static_cast<Sound *>(sSelectedItem);
                    if (sound->getSoundType() == Sound::SoundType::Wave)
                    {
                        wave = static_cast<WaveFile *>(sound->getWaveSoundInfo().getWaveFileRef().getItem());
                    }
                    else if (sound->getSoundType() == Sound::SoundType::Strm && !sound->getStreamSoundInfo().getTrackList().isEmpty())
                    {
                        Sound::StreamSoundInfo::Track *track = static_cast<Sound::StreamSoundInfo::Track *>(sound->getStreamSoundInfo().getTrackList().front()->val());
                        wave = static_cast<WaveFile *>(track->getWaveFileRef().getItem());
                    }
                }
            }
            else if (sSoundPlayer.getLastPlayedSound())
            {
                const Sound *sound = sSoundPlayer.getLastPlayedSound();
                if (sound->getSoundType() == Sound::SoundType::Wave)
                {
                    wave = static_cast<const WaveFile *>(sound->getWaveSoundInfo().getWaveFileRef().getItem());
                }
                else if (sound->getSoundType() == Sound::SoundType::Strm && !sound->getStreamSoundInfo().getTrackList().isEmpty())
                {
                    Sound::StreamSoundInfo::Track *track = static_cast<Sound::StreamSoundInfo::Track *>(sound->getStreamSoundInfo().getTrackList().front()->val());
                    wave = static_cast<const WaveFile *>(track->getWaveFileRef().getItem());
                }
            }

            if (wave)
            {
                sampleCount = wave->getSampleCount();
                sampleRate = wave->getSampleRate();
                totalSec = sampleRate != 0 ? static_cast<f32>(sampleCount) / static_cast<f32>(sampleRate) : 0.0f;

                ImDrawList *draw = ImGui::GetWindowDrawList();
                f32 loopStartX = wave->getOriginalLoopStartFrame() / static_cast<f32>(sampleCount);
                f32 loopEndX = wave->getOriginalLoopEndFrame() / static_cast<f32>(sampleCount);

                if (wave->getIsLoop())
                {
                    draw->AddLine(
                        ImVec2(barStartScreenPos.x + loopStartX * adjustSize, barStartScreenPos.y),
                        ImVec2(barStartScreenPos.x + loopStartX * adjustSize, barStartScreenPos.y + barYSize), IM_COL32(0, 255, 0, 255), 2.0f);
                }

                draw->AddLine(
                    ImVec2(barStartScreenPos.x + loopEndX * adjustSize, barStartScreenPos.y),
                    ImVec2(barStartScreenPos.x + loopEndX * adjustSize, barStartScreenPos.y + barYSize), IM_COL32(255, 0, 0, 255), 2.0f);
            }
        }
    }

    ImGui::SameLine();
    
    if (isSeq && !sSoundPlayer.seqHasFiniteTotal())
        ImGui::TextDisabled("%s", ICON_LC_REPEAT);
    else
        DrawTimeText(totalSec);

    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(volIcon);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(volSliderW);

    int volPct = static_cast<int>(gMasterVolume * 100.0f + 0.5f);

    if (ImGui::SliderInt("##vol", &volPct, 0, 100, "%d%%"))
    {
        gMasterVolume = sead::Mathf::clamp2(0.0f, volPct / 100.0f, 1.0f);
        snd::SoundSystem::setMasterVolume(gMasterVolume);
    }

    if (ImGui::IsItemDeactivatedAfterEdit())
        SaveAudioConfig();
}

void DrawPlayerUI()
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

    if (ImGui::Begin(ICON_LC_MUSIC " Player###PlayerWindow"))
    {
        ImGuiWindow* win = ImGui::GetCurrentWindow();
        if (win && win->DockNode)
            win->DockNode->LocalFlags |= ImGuiDockNodeFlags_NoResize;

        bool isPause = sSoundPlayer.isPause();

        if (ImGui::Button(isPause ? ICON_LC_PLAY : ICON_LC_PAUSE) && sSoundPlayer.isCurrentPlayer())
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

        if (sSoundPlayer.isCurrentPlayer() && !sSoundPlayer.isActive() && sSoundPlayer.getLastPlayedSound())
        {
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone))
            {
                ImGui::SetTooltip("Last Sound '%s'", sSoundPlayer.getLastPlayedSound()->getFormattedName().cstr());
            }
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

    if (ImGui::Begin(ICON_LC_SETTINGS_2 " Player Parameters###PlayerParamWindow"))
    {
        sSoundPlayer.drawParameters();
    }
    ImGui::End();

    if (ImGui::Begin(ICON_LC_BINARY " Sequence Variables###SequenceVarWindow"))
    {
        sSoundPlayer.drawSeqVars();
    }
    ImGui::End();

    sSoundPlayer.update();
}
