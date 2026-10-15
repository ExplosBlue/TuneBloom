#include <ui/player/PlaybackDrawer.h>

#include <ui/widgets/DockNode.h>
#include <ui/items/ItemColumns.h>
#include <ui/Messages.h>
#include <ui/player/OutputMeter.h>
#include <ui/app/Shortcuts.h>
#include <ui/stream/StreamPanelFold.h>
#include <ui/stream/StreamRegions.h>
#include <ui/settings/Theme.h>
#include <ui/widgets/TimeUtil.h>
#include <ui/app/UI.h>
#include <ui/settings/UiColors.h>

#include <imgui/imgui_custom.h>

#include <snd/SoundSystem.h>

#include <cmath>
#include <cstdio>
#include <cstring>

SoundPlayer sSoundPlayer;

constexpr f32   cSeekTrackHeight      = 6.0f;
constexpr f32   cSeekHandleRadius     = 5.0f;
constexpr f32   cSeekLoopInset        = 3.0f;
constexpr f32   cSeekGhostAlpha       = 0.35f;
constexpr f32   cSeekRegionGap        = 5.0f;
constexpr f32   cSeekRegionHeight     = 4.0f;
constexpr float cLoopMarkerThickness  = 2.0f;

constexpr uicolors::Themed<f32> cSeekLoopAlpha = { 0.08f, 0.12f };

constexpr f32 cTransportHeightEm   = 2.6f;
constexpr f32 cTransportButtonEm   = 1.6f;
constexpr f32 cTransportGroupGapEm = 1.0f;
constexpr f32 cDisplayMaxWidthEm   = 50.0f;
constexpr f32 cDisplayLargeWidthEm = 30.0f;
constexpr f32 cDisplayMinWidthEm   = 8.0f;
constexpr f32 cDisplayTimeEm       = 1.2f;
constexpr f32 cDisplayPadEm        = 0.5f;
constexpr f32 cDisplayRowGap       = 4.0f;
constexpr f32 cDisplayTextRaise    = 1.0f;
constexpr f32 cDisplayDarken       = 0.2f;
constexpr f32 cDisplaySeekMinEm    = 4.0f;
constexpr f32 cDisplayLoopHoverMix = 0.4f;
constexpr f32 cVolumeSliderWidth   = 120.0f;
constexpr f32 cVolumeSliderMinEm   = 3.0f;
constexpr f32 cVolumeSliderRatio   = 0.1f;
constexpr f32 cVolumeValueMinEm    = 40.0f;
constexpr f32 cVolumeTrackHeight   = 4.0f;
constexpr f32 cVolumeHandleWidth   = 8.0f;
constexpr f32 cVolumeHandleHeight  = 16.0f;
constexpr f32 cVolumeStepDb        = 0.5f;
constexpr f32 cVolumeWheelStepDb   = 1.0f;
constexpr f32 cVolumeMeterHeight   = 4.0f;
constexpr f32 cVolumeMeterGap      = 2.0f;
constexpr f32 cVolumeMeterOffset   = 3.0f;

static bool sShowRemainingTime = false;

bool DrawSeekBar(const char* id, f32 width, f32 fraction, bool determinate,
                 bool seekable, bool allowDrag,
                 f32 loopStartFrac, f32 loopEndFrac, f32& outSeekFraction)
{
    const f32 height = ImGui::GetFrameHeight();
    const ImVec2 barStart = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton(id, ImVec2(width, height));

    const bool hovered = seekable && ImGui::IsItemHovered();
    const bool held = seekable && ImGui::IsItemActive();

    if (hovered || held)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    const f32 trackStart = barStart.x + cSeekHandleRadius;
    const f32 trackWidth = sead::Mathf::max(width - cSeekHandleRadius * 2.0f, 1.0f);
    const f32 mouseFrac = sead::Mathf::clamp2(0.0f, (ImGui::GetMousePos().x - trackStart) / trackWidth, 1.0f);

    bool seeked = false;

    if (seekable && (ImGui::IsItemActivated() || (allowDrag && held && ImGui::IsMouseDragging(ImGuiMouseButton_Left))))
    {
        outSeekFraction = mouseFrac;
        seeked = true;
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();

    const f32 centerY = ImFloor(barStart.y + height * 0.5f);
    const ImVec2 trackMin(trackStart, centerY - cSeekTrackHeight * 0.5f);
    const ImVec2 trackMax(trackStart + trackWidth, centerY + cSeekTrackHeight * 0.5f);
    const f32 rounding = cSeekTrackHeight * 0.5f;

    const ImVec4& fillColor = uicolors::PlaybackFill();
    const ImU32 markerColor = uicolors::ToU32(uicolors::Marker());

    if (loopStartFrac >= 0.0f && loopEndFrac > loopStartFrac)
    {
        ImVec4 loopColor = uicolors::Marker();
        loopColor.w = uicolors::Pick(cSeekLoopAlpha);

        draw->AddRectFilled(ImVec2(trackStart + loopStartFrac * trackWidth, barStart.y + cSeekLoopInset),
                            ImVec2(trackStart + loopEndFrac * trackWidth, barStart.y + height - cSeekLoopInset),
                            ImGui::GetColorU32(loopColor));
    }

    draw->AddRectFilled(trackMin, trackMax, GetTrackColorU32(ImGui::GetHoverColorU32(ImGui::GetID(id) + 1, hovered || held, false, ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgHovered)), rounding);

    if (!determinate)
        draw->AddRectFilled(trackMin, trackMax, ImGui::GetColorU32(ImGuiCol_TextDisabled), rounding);
    else if (fraction > 0.0f)
        draw->AddRectFilled(trackMin, ImVec2(trackStart + fraction * trackWidth, trackMax.y), ImGui::GetColorU32(fillColor), rounding);

    if (loopStartFrac >= 0.0f)
    {
        const f32 markerX = ImFloor(trackStart + loopStartFrac * trackWidth);
        draw->AddRectFilled(ImVec2(markerX, barStart.y + cSeekLoopInset), ImVec2(markerX + cLoopMarkerThickness, barStart.y + height - cSeekLoopInset), markerColor);
    }

    if (loopEndFrac >= 0.0f)
    {
        const f32 markerX = ImFloor(trackStart + loopEndFrac * trackWidth);
        draw->AddRectFilled(ImVec2(markerX - cLoopMarkerThickness, barStart.y + cSeekLoopInset), ImVec2(markerX, barStart.y + height - cSeekLoopInset), markerColor);
    }

    if (hovered && !held)
    {
        const f32 ghostX = ImFloor(trackStart + mouseFrac * trackWidth);
        draw->AddLine(ImVec2(ghostX, barStart.y + 1.0f), ImVec2(ghostX, barStart.y + height - 1.0f), ImGui::GetColorU32(ImGuiCol_Text, cSeekGhostAlpha));
    }

    if (determinate)
    {
        const ImVec2 center(trackStart + fraction * trackWidth, centerY);
        const ImGuiCol handleColor = seekable ? ImGuiCol_Text : ImGuiCol_TextDisabled;

        draw->AddCircleFilled(center, held ? cSeekHandleRadius + 1.0f : cSeekHandleRadius, ImGui::GetColorU32(handleColor));
    }

    return seeked;
}

static const WaveFile* ResolvePrimaryWaveFile(const Sound* sound)
{
    if (sound->getSoundType() == Sound::SoundType::Wave)
        return static_cast<const WaveFile*>(sound->getWaveSoundInfo().getWaveFileRef().getItem());

    if (sound->getSoundType() == Sound::SoundType::Strm)
        return sound->getStreamSoundInfo().getMainWave();

    return nullptr;
}

const Item* ResolveDisplayItem()
{
    if (sSoundPlayer.isActive())
    {
        if (sSoundPlayer.getPlayingSound())
            return sSoundPlayer.getPlayingSound();

        return sSoundPlayer.getPlayingWaveFile();
    }

    if (sSelectedItem && (sSelectedItem->getItemType() == Item::ItemType::WaveFile || sSelectedItem->getItemType() == Item::ItemType::Sound))
        return sSelectedItem;

    return sSoundPlayer.getLastPlayedSound();
}

static const char* GetDisplayTypeName(const Item* item)
{
    if (item->getItemType() == Item::ItemType::WaveFile)
        return messages::common::cWaveFile;

    switch (static_cast<const Sound*>(item)->getSoundType())
    {
    case Sound::SoundType::Seq:
        return messages::common::cSequence;
    case Sound::SoundType::Strm:
        return messages::common::cStream;
    case Sound::SoundType::Wave:
        return messages::common::cWave;
    default:
        return "";
    }
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

    u32 sampleRate = 0;
    bool isLoop = false;

    const WaveFile* wave = nullptr;
};

const SeqTimeline* ResolveSeqTimeline()
{
    if (sSoundPlayer.isActive())
        return sSoundPlayer.isCurrentPlayerSequence() ? &sSoundPlayer.getPlayingSeqTimeline() : nullptr;

    const Item* item = ResolveDisplayItem();

    if (!item || item->getItemType() != Item::ItemType::Sound)
        return nullptr;

    const Sound* sound = static_cast<const Sound*>(item);

    if (sound->getSoundType() != Sound::SoundType::Seq)
        return nullptr;

    return &sSoundPlayer.getSeqTimeline(*sound);
}

static void SetPlayingRegionLoop(PlaybackProgress* progress, const WaveFile& wave)
{
    const std::vector<StreamRegion>& regions = wave.getStreamFileLayout().regions;
    const u32 sampleCount = wave.getSampleCount();

    s32 playingNo = -1;
    s32 nextNo = -1;
    GetPlayingStreamRegions(&wave, &playingNo, &nextNo);

    if (playingNo < 0 || playingNo >= static_cast<s32>(regions.size()) || sampleCount == 0)
        return;

    progress->isLoop = true;
    progress->loopStartFrac = regions[playingNo].start / static_cast<f32>(sampleCount);
    progress->loopEndFrac = regions[playingNo].end / static_cast<f32>(sampleCount);
}

static PlaybackProgress BuildPlaybackProgress()
{
    const SeqTimeline* timeline = ResolveSeqTimeline();
    const bool isSeq = timeline != nullptr;
    const bool active = sSoundPlayer.isActive();

    u32 sampleCount = sSoundPlayer.getSampleCount();
    u32 sampleRate = sSoundPlayer.getSampleRate();

    PlaybackProgress progress;
    progress.allowDrag = !isSeq;

    if (isSeq)
    {
        if (timeline->hasLength())
        {
            const f32 tempoRatio = sSoundPlayer.getSeqTempoRatio();
            const f32 timeScale = tempoRatio > 0.0f ? 1.0f / tempoRatio : 1.0f;

            const f32 totalSec = timeline->calcSeconds(timeline->endTick);
            const f32 elapsedSec = active ? timeline->calcSeconds(sSoundPlayer.getSeqCurrentTick()) : 0.0f;

            progress.fraction = totalSec > 0.0f ? sead::Mathf::clamp2(0.0f, elapsedSec / totalSec, 1.0f) : 0.0f;
            progress.totalSec = totalSec * timeScale;
            progress.elapsedSec = elapsedSec * timeScale;

            if (timeline->kind == SeqTimeline::Kind::Loop)
            {
                progress.isLoop = true;
                progress.loopStartFrac = totalSec > 0.0f ? timeline->calcSeconds(timeline->loopStartTick) / totalSec : 0.0f;
                progress.loopEndFrac = 1.0f;
            }
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

    if (!isSeq)
    {
        const WaveFile* wave = nullptr;

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
            progress.sampleRate = sampleRate;
            progress.isLoop = wave->getIsLoop();
            progress.wave = wave;

            if (wave->getIsLoop())
                progress.loopStartFrac = wave->getOriginalLoopStartFrame() / static_cast<f32>(sampleCount);

            progress.loopEndFrac = wave->getOriginalLoopEndFrame() / static_cast<f32>(sampleCount);

            SetPlayingRegionLoop(&progress, *wave);
        }
    }

    progress.seekable = active && progress.determinate && (!isSeq || timeline->hasLength());
    progress.showTotalTime = !(isSeq && timeline->kind == SeqTimeline::Kind::NoEnd);

    return progress;
}

static ImU32 GetDisplayValueColor()
{
    return ImGui::GetColorU32(GetItemValueColor());
}

static bool DrawTransportButton(const char* id, TransportGlyph glyph, f32 size, bool lit, IconButtonTint tint = IconButtonTint::Accent, ImGuiButtonFlags flags = ImGuiButtonFlags_MouseButtonLeft)
{
    return DrawGlyphButton(id, glyph, ImVec2(size, size), size, lit, tint, flags);
}

bool CanTogglePlayback()
{
    return sSoundPlayer.isCurrentPlayer() && (sSoundPlayer.isActive() || sSoundPlayer.getLastPlayedSound());
}

void TogglePlayback()
{
    if (!CanTogglePlayback())
        return;

    if (!sSoundPlayer.isActive())
        sSoundPlayer.playLastSound();
    else
        sSoundPlayer.pause(!sSoundPlayer.isPause());
}

static void SetRemainingTimeShown(bool shown)
{
    sShowRemainingTime = shown;
    ImGui::MarkIniSettingsDirty();
}

void SetPlaybackLoopEnabled(bool enable)
{
    sSoundPlayer.setLoopEnabled(enable);
    ImGui::MarkIniSettingsDirty();
}

static void HandleTransportShortcuts()
{
    if (ImGui::GetIO().WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
        return;

    if (shortcuts::Pressed(shortcuts::Action::PlayPause))
        TogglePlayback();
}

static void DrawTransportButtons(f32 size)
{
    const bool isPause = sSoundPlayer.isPause();
    const bool isPlaying = sSoundPlayer.isActive() && !isPause;
    const bool canTransport = CanTogglePlayback();

    ImGui::BeginDisabled(!canTransport);

    if (DrawTransportButton("##play", isPause ? TransportGlyph::Play : TransportGlyph::Pause, size, isPlaying, IconButtonTint::Play))
        TogglePlayback();

    ImGui::EndDisabled();

    if (!canTransport)
        ImGui::SetItemTooltip("%s", messages::playback::cNothingToPlay);
    else if (!sSoundPlayer.isActive() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip(messages::playback::cLastSoundFormat, sSoundPlayer.getLastPlayedSound()->getFormattedName().cstr());

    ImGui::SameLine();

    if (DrawTransportButton("##stop", TransportGlyph::Stop, size, false, IconButtonTint::Accent, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle))
    {
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Middle))
            sSoundPlayer.fadeOutAllVoices();
        else
            sSoundPlayer.fadeOutPlayers();
    }

    ImGui::SetItemTooltip("%s", messages::playback::cStopAllVoices);
}

static void DrawDisplayTime(const PlaybackProgress& progress, const ImVec2& pos, f32 width, f32 height, ImFont* font, f32 fontSize)
{
    const bool showRemaining = sShowRemainingTime && progress.showTotalTime && progress.totalSec > 0.0f;

    ImGui::SetCursorScreenPos(pos);

    if (ImGui::InvisibleButton("##time", ImVec2(width, height)))
        SetRemainingTimeShown(!sShowRemainingTime);

    if (ImGui::IsItemHovered())
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        ImGui::SetItemTooltip("%s", sShowRemainingTime ? messages::playback::cShowElapsedTime : messages::playback::cShowRemainingTime);
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const f32 textY = ImFloor(pos.y + (height - fontSize) * 0.5f);

    if (!progress.determinate)
    {
        const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, messages::playback::cUnknownTime);
        draw->AddText(font, fontSize, ImVec2(ImFloor(pos.x + width - textSize.x), textY), GetDisplayValueColor(), messages::playback::cUnknownTime);
        return;
    }

    char clock[16];
    char text[20];
    timeutil::FormatClock(clock, sizeof(clock), showRemaining ? progress.totalSec - progress.elapsedSec : progress.elapsedSec);
    snprintf(text, sizeof(text), "%s%s", showRemaining ? "-" : "", clock);

    const char* fractionText = strchr(text, '.');
    const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);
    const ImVec2 mainSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text, fractionText);
    const f32 textX = ImFloor(pos.x + width - textSize.x);

    draw->AddText(font, fontSize, ImVec2(textX, textY), ImGui::GetColorU32(ImGuiCol_Text), text, fractionText);
    draw->AddText(font, fontSize, ImVec2(textX + mainSize.x, textY), GetDisplayValueColor(), fractionText);
}

static void DrawDisplayLoop(const PlaybackProgress& progress, const ImVec2& pos)
{
    const bool noEnd = !progress.showTotalTime;
    const bool canToggle = progress.isLoop && !noEnd;
    const bool loopEnabled = sSoundPlayer.isLoopEnabled();
    const bool lit = noEnd || (progress.isLoop && loopEnabled);

    ImGui::SetCursorScreenPos(pos);

    if (ImGui::InvisibleButton("##loop", ImGui::CalcTextSize(ICON_LC_REPEAT)) && canToggle)
        SetPlaybackLoopEnabled(!loopEnabled);

    const bool hovered = canToggle && ImGui::IsItemHovered();

    const ImVec4 accent = GetThemeAccent();

    const ImU32 idleColor = ImGui::GetColorU32(lit ? accent : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    const ImU32 hoverColor = ImGui::GetColorU32(lit ? ImLerp(accent, ImGui::GetStyleColorVec4(ImGuiCol_Text), cDisplayLoopHoverMix) : ImGui::GetStyleColorVec4(ImGuiCol_Text));

    ImGui::GetWindowDrawList()->AddText(pos, ImGui::GetHoverColorU32(ImGui::GetItemID(), hovered, idleColor, hoverColor), ICON_LC_REPEAT);

    if (hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        return;

    if (noEnd)
    {
        ImGui::SetTooltip("%s", messages::playback::cSequenceNoEnd);
    }
    else if (progress.isLoop)
    {
        char start[16];
        char end[16];
        timeutil::FormatClock(start, sizeof(start), progress.loopStartFrac * progress.totalSec);
        timeutil::FormatClock(end, sizeof(end), progress.loopEndFrac * progress.totalSec);

        if (loopEnabled)
            ImGui::SetTooltip(messages::playback::cLoopFormat, start, end);
        else
            ImGui::SetTooltip(messages::playback::cLoopOffFormat, end);
    }
    else
    {
        ImGui::SetTooltip("%s", messages::playback::cNoLoop);
    }
}

static void FormatDisplayRegion(sead::BufferedSafeString* out, const StreamFileLayout& layout, s32 regionNo)
{
    const sead::SafeString& name = layout.regions[regionNo].name;

    if (name.isEmpty())
        out->format(messages::stream::cRegionDisplayFormat, regionNo);
    else
        out->format("%d  %s", regionNo, name.cstr());
}

static f32 DrawDisplayRegion(const ImVec2& pos, f32 pad)
{
    const Sound* sound = sSoundPlayer.getPlayingSound();
    const s32 regionNo = sound ? sSoundPlayer.getPlayingStreamRegion() : -1;
    const StreamFileLayout* layout = regionNo != -1 ? sound->getStreamSoundInfo().getFileLayout() : nullptr;

    if (!layout || regionNo >= static_cast<s32>(layout->regions.size()))
        return pos.x;

    sead::FixedSafeString<80> region;
    FormatDisplayRegion(&region, *layout, regionNo);

    const sead::FormatFixedSafeString<96> text(ICON_LC_FLAG " %s", region.cstr());

    const f32 left = pos.x - ImGui::CalcTextSize(text.cstr()).x - pad;

    ImGui::GetWindowDrawList()->AddText(ImVec2(left, pos.y), ImGui::GetColorU32(ImGuiCol_CheckMark), text.cstr());

    return left;
}

static void DrawSeekRegions(const WaveFile* wave, const ImVec2& barMin, const ImVec2& barMax)
{
    if (!wave || wave->getStreamFileLayout().regions.empty())
        return;

    const f32 top = ImFloor((barMin.y + barMax.y) * 0.5f) + cSeekTrackHeight * 0.5f + cSeekRegionGap;

    DrawStreamRegionMarks(ImGui::GetWindowDrawList(), ImVec2(barMin.x + cSeekHandleRadius, top), ImVec2(barMax.x - cSeekHandleRadius, top + cSeekRegionHeight), wave);
}

static void SetSeekTooltip(const PlaybackProgress& progress, f32 fraction)
{
    char clock[16];
    timeutil::FormatClock(clock, sizeof(clock), fraction * progress.totalSec);

    const s32 regionNo = progress.wave ? FindStreamRegionAt(progress.wave, static_cast<u32>(fraction * progress.wave->getSampleCount())) : -1;

    if (regionNo == -1)
    {
        ImGui::SetTooltip("%s", clock);
        return;
    }

    sead::FixedSafeString<80> region;
    FormatDisplayRegion(&region, progress.wave->getStreamFileLayout(), regionNo);

    ImGui::SetTooltip("%s\n%s", clock, region.cstr());
}

static void DrawTransportDisplay(const PlaybackProgress& progress, f32 width, f32 height)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 em = ImGui::GetFontSize();
    const f32 pad = ImFloor(em * cDisplayPadEm);
    const f32 rowTop = ImFloor((height - em * 2.0f - cDisplayRowGap) * 0.5f) - cDisplayTextRaise;
    const bool large = width >= em * cDisplayLargeWidthEm;

    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + width, min.y + height);
    const f32 row1 = min.y + rowTop;
    const f32 row2 = row1 + em + cDisplayRowGap;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec4 background = IsDarkTheme() ? ImLerp(style.Colors[ImGuiCol_WindowBg], uicolors::cInkOnLight, cDisplayDarken) : style.Colors[ImGuiCol_ChildBg];

    draw->AddRectFilled(min, max, ImGui::GetColorU32(background), style.FrameRounding);
    draw->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_Border), style.FrameRounding);

    ImGui::PushID("##display");
    ImGui::PushClipRect(min, max, true);

    f32 left = min.x + pad;
    f32 nameLeft = left;
    const f32 right = max.x - pad;

    // Time
    if (large)
    {
        ImFont* font = ImGui::GetIO().Fonts->Fonts[0];
        const f32 fontSize = ImFloor(em * cDisplayTimeEm);
        const f32 timeWidth = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, "-00:00.000").x;

        DrawDisplayTime(progress, ImVec2(left, min.y), timeWidth, height - cDisplayTextRaise * 4.0f, font, fontSize);
        left += timeWidth + pad;

        draw->AddLine(ImVec2(left, min.y + pad), ImVec2(left, max.y - pad), ImGui::GetColorU32(ImGuiCol_Border));
        left += pad + 1.0f;
        nameLeft = left;
    }
    else
    {
        const f32 timeWidth = ImGui::CalcTextSize("-00:00.000").x;

        DrawDisplayTime(progress, ImVec2(left, row1), timeWidth, em, ImGui::GetFont(), em);
        nameLeft += timeWidth + pad;
    }

    // Loop
    f32 infoLeft = right - ImGui::CalcTextSize(ICON_LC_REPEAT).x;
    DrawDisplayLoop(progress, ImVec2(infoLeft, row1));

    const Item* item = ResolveDisplayItem();

    // Type and sample rate
    if (large && item)
    {
        sead::FixedSafeString<64> info(GetDisplayTypeName(item));

        if (progress.sampleRate != 0)
        {
            info.append("  ");
            info.appendWithFormat(messages::unit::cHertzFormat, progress.sampleRate);
        }

        infoLeft -= ImGui::CalcTextSize(info.cstr()).x + pad;
        draw->AddText(ImVec2(infoLeft, row1), GetDisplayValueColor(), info.cstr());
    }

    infoLeft = DrawDisplayRegion(ImVec2(infoLeft, row1), pad);

    // Name
    const f32 nameRight = infoLeft - pad;

    if (nameRight > nameLeft)
    {
        const sead::FixedSafeString<256> name = item ? item->getFormattedName() : sead::FixedSafeString<256>(sSoundPlayer.isActive() ? "" : messages::playback::cNoSound);
        const bool bright = item && sSoundPlayer.isActive();

        ImGui::PushStyleColor(ImGuiCol_Text, bright ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : GetItemValueColor());
        ImGui::RenderTextEllipsis(draw, ImVec2(nameLeft, row1), ImVec2(nameRight, row1 + em), nameRight, nameRight, name.cstr(), nullptr, nullptr);
        ImGui::PopStyleColor();
    }

    // Total time
    char total[16];
    if (progress.showTotalTime)
        timeutil::FormatClock(total, sizeof(total), progress.totalSec);
    else
        snprintf(total, sizeof(total), "%s", messages::playback::cUnknownTime);

    const f32 totalWidth = ImGui::CalcTextSize("00:00.000").x;
    f32 seekWidth = right - left - totalWidth - pad;
    const bool showTotal = seekWidth >= em * cDisplaySeekMinEm;

    if (showTotal)
        draw->AddText(ImVec2(right - totalWidth, row2), GetDisplayValueColor(), total);
    else
        seekWidth = right - left;

    // Seek bar
    if (seekWidth > cSeekHandleRadius * 4.0f)
    {
        f32 seekFrac = 0.0f;

        ImGui::SetCursorScreenPos(ImVec2(left, row2));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, 0.0f));

        if (DrawSeekBar("##transport", seekWidth, progress.fraction, progress.determinate, progress.seekable, progress.allowDrag, progress.loopStartFrac, progress.loopEndFrac, seekFrac))
            sSoundPlayer.seek(seekFrac);

        ImGui::PopStyleVar();

        DrawSeekRegions(progress.wave, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

        if (progress.seekable && progress.totalSec > 0.0f && ImGui::IsItemHovered())
        {
            const f32 trackWidth = seekWidth - cSeekHandleRadius * 2.0f;

            SetSeekTooltip(progress, sead::Mathf::clamp2(0.0f, (ImGui::GetMousePos().x - left - cSeekHandleRadius) / trackWidth, 1.0f));
        }
    }

    ImGui::PopClipRect();
    ImGui::PopID();

    ImGui::SetCursorScreenPos(min);
    ImGui::Dummy(ImVec2(width, height));
}

f32 GetMasterVolumeDb()
{
    if (gMasterVolume <= 0.0f)
        return OutputMeterPositionToDb(0.0f);

    return 20.0f * std::log10(gMasterVolume);
}

void SetMasterVolumeDb(f32 db)
{
    db = sead::Mathf::min(std::floor(db / cVolumeStepDb + 0.5f) * cVolumeStepDb, 0.0f);

    gMasterVolume = db > OutputMeterPositionToDb(0.0f) ? std::pow(10.0f, db / 20.0f) : 0.0f;
    snd::SoundSystem::setMasterVolume(gMasterVolume);
}

f32 GetMasterVolumePosition()
{
    return OutputMeterDbToPosition(GetMasterVolumeDb());
}

void SetMasterVolumePosition(f32 position)
{
    SetMasterVolumeDb(OutputMeterPositionToDb(sead::Mathf::clamp2(0.0f, position, 1.0f)));
}

bool IsMasterVolumeDefault()
{
    return gMasterVolume == 1.0f;
}

bool ResetMasterVolumeOnRightClick()
{
    if (IsMasterVolumeDefault() || !ImGui::IsItemClicked(ImGuiMouseButton_Right))
        return false;

    SetMasterVolumeDb(0.0f);
    SaveAudioConfig();
    return true;
}

void FormatMasterVolume(char* buffer, size_t size)
{
    const f32 db = GetMasterVolumeDb();

    if (gMasterVolume <= 0.0f)
        snprintf(buffer, size, "%s", messages::playback::cVolumeSilent);
    else
        snprintf(buffer, size, messages::playback::cVolumeDbFormat, db > -cVolumeStepDb * 0.1f ? 0.0f : db);
}

static const char* MasterVolumeIcon()
{
    if (gMasterVolume <= 0.0f)
        return ICON_LC_VOLUME_X;

    if (GetMasterVolumePosition() <= 0.5f)
        return ICON_LC_VOLUME_1;

    return ICON_LC_VOLUME_2;
}

static f32 MasterVolumeWidth(f32 faderWidth, bool showValue)
{
    const f32 spacing = ImGui::GetStyle().ItemSpacing.x;
    f32 width = ImGui::CalcTextSize(ICON_LC_VOLUME_2).x + spacing + faderWidth;

    if (showValue)
        width += spacing + ImGui::CalcTextSize(messages::playback::cVolumeWidest).x;

    return width;
}

static void DrawMasterVolume(f32 faderWidth, f32 height, bool showValue)
{
    const f32 spacing = ImGui::GetStyle().ItemSpacing.x;
    const f32 em = ImGui::GetFontSize();
    const f32 metersHeight = cVolumeMeterHeight * 2.0f + cVolumeMeterGap;

    const ImVec2 min = ImGui::GetCursorScreenPos();
    const f32 top = ImFloor(min.y + (height - em - cVolumeMeterOffset - metersHeight) * 0.5f);

    ImGui::PushID("##master");

    ImGui::SetCursorScreenPos(ImVec2(min.x, top));
    ImGui::TextUnformatted(MasterVolumeIcon());

    ImGui::SetItemTooltip("%s", messages::playback::cMasterVolume);

    // Fader
    const ImVec2 faderMin(min.x + ImGui::CalcTextSize(ICON_LC_VOLUME_2).x + spacing, top);
    const f32 trackStart = faderMin.x + cVolumeHandleWidth * 0.5f;
    const f32 trackWidth = faderWidth - cVolumeHandleWidth;

    ImGui::SetCursorScreenPos(faderMin);
    ImGui::InvisibleButton("##vol", ImVec2(faderWidth, em));

    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    const f32 wheel = ImGui::GetIO().MouseWheel;

    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
        SetMasterVolumeDb(0.0f);
        ImGui::ClearActiveID();
        SaveAudioConfig();
    }
    else if (held)
    {
        SetMasterVolumePosition((ImGui::GetMousePos().x - trackStart) / trackWidth);
    }
    else if (hovered && wheel != 0.0f)
    {
        SetMasterVolumeDb(GetMasterVolumeDb() + wheel * cVolumeWheelStepDb);
        SaveAudioConfig();
    }

    ResetMasterVolumeOnRightClick();

    if (ImGui::IsItemDeactivated())
        SaveAudioConfig();

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const f32 centerY = ImFloor(top + em * 0.5f);
    const f32 handleX = ImFloor(trackStart + GetMasterVolumePosition() * trackWidth);

    const ImVec4 fillColor = GetThemeAccent();

    ImVec4 handleColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    if (held)
        handleColor = ImLerp(fillColor, handleColor, 0.5f);
    else if (!hovered)
        handleColor = GetItemValueColor();

    draw->AddRectFilled(ImVec2(trackStart, centerY - cVolumeTrackHeight * 0.5f), ImVec2(trackStart + trackWidth, centerY + cVolumeTrackHeight * 0.5f), GetTrackColorU32(ImGui::GetColorU32(hovered || held ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg)), cVolumeTrackHeight * 0.5f);
    draw->AddRectFilled(ImVec2(trackStart, centerY - cVolumeTrackHeight * 0.5f), ImVec2(handleX, centerY + cVolumeTrackHeight * 0.5f), ImGui::GetColorU32(fillColor), cVolumeTrackHeight * 0.5f);
    draw->AddRectFilled(ImVec2(handleX - cVolumeHandleWidth * 0.5f, centerY - cVolumeHandleHeight * 0.5f), ImVec2(handleX + cVolumeHandleWidth * 0.5f, centerY + cVolumeHandleHeight * 0.5f), ImGui::GetColorU32(handleColor), 2.0f);

    // Output meter
    DrawOutputMeterRows(ImVec2(trackStart, top + em + cVolumeMeterOffset), trackWidth, cVolumeMeterHeight, cVolumeMeterGap);

    // Value
    if (showValue)
    {
        char value[16];
        FormatMasterVolume(value, sizeof(value));

        draw->AddText(ImVec2(faderMin.x + faderWidth + spacing, top), GetDisplayValueColor(), value);
    }

    ImGui::PopID();

    ImGui::SetCursorScreenPos(min);
    ImGui::Dummy(ImVec2(MasterVolumeWidth(faderWidth, showValue), height));
}

static void* PlaybackSettingsReadOpen(ImGuiContext*, ImGuiSettingsHandler*, const char*)
{
    return &sShowRemainingTime;
}

static void PlaybackSettingsReadLine(ImGuiContext*, ImGuiSettingsHandler*, void*, const char* line)
{
    int open = 0;

    if (sscanf(line, "DrawerOpen=%d", &open) == 1)
        SetPlaybackDrawerOpen(open != 0);
    else if (sscanf(line, "DrawerView=%d", &open) == 1)
        SetPlaybackDrawerView(open != 0 ? PlaybackDrawerView::Variables : PlaybackDrawerView::Parameters);
    else if (sscanf(line, "RemainingTime=%d", &open) == 1)
        sShowRemainingTime = open != 0;
    else if (sscanf(line, "Loop=%d", &open) == 1)
        sSoundPlayer.setLoopEnabled(open != 0);
    else if (sscanf(line, "TrackMixer=%d", &open) == 1)
        SetTrackMixerView(open != 0);
    else if (sscanf(line, "TrackFolded=%d", &open) == 1)
        SetTrackFolded(open != 0);
    else if (sscanf(line, "TrackHeight=%d", &open) == 1)
        SetTrackUnfoldedHeight(static_cast<f32>(open));
}

static void PlaybackSettingsWriteAll(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buf)
{
    buf->appendf("[%s][Player]\n", handler->TypeName);
    buf->appendf("DrawerOpen=%d\n", IsPlaybackDrawerOpen() ? 1 : 0);
    buf->appendf("DrawerView=%d\n", GetPlaybackDrawerView() == PlaybackDrawerView::Variables ? 1 : 0);
    buf->appendf("RemainingTime=%d\n", sShowRemainingTime ? 1 : 0);
    buf->appendf("Loop=%d\n", sSoundPlayer.isLoopEnabled() ? 1 : 0);
    buf->appendf("TrackMixer=%d\n", IsTrackMixerView() ? 1 : 0);
    buf->appendf("TrackFolded=%d\n", IsTrackFolded() ? 1 : 0);
    buf->appendf("TrackHeight=%d\n\n", static_cast<s32>(GetTrackUnfoldedHeight()));
}

void RegisterPlaybackSettings()
{
    ImGuiSettingsHandler handler;
    handler.TypeName = "Playback";
    handler.TypeHash = ImHashStr("Playback");
    handler.ReadOpenFn = PlaybackSettingsReadOpen;
    handler.ReadLineFn = PlaybackSettingsReadLine;
    handler.WriteAllFn = PlaybackSettingsWriteAll;

    ImGui::AddSettingsHandler(&handler);
}

static ImGuiDockNode* GetPlayerBarNode()
{
    ImGuiDockNode* node = ImGui::GetCurrentWindow()->DockNode;

    if (!node || !node->ParentNode || node->ParentNode->SplitAxis != ImGuiAxis_Y)
        return nullptr;

    return node;
}

static void UpdatePlayerBarHeight(f32 transportHeight, f32 drawerHeight)
{
    ImGuiDockNode* node = GetPlayerBarNode();

    if (!node)
        return;

    const ImGuiStyle& style = ImGui::GetStyle();
    f32 height = ImGui::GetCurrentWindow()->DC.CursorStartPos.y - node->Pos.y + transportHeight + style.WindowPadding.y;

    if (IsPlaybackDrawerOpen())
        height += drawerHeight + style.ItemSpacing.y;

    SetDockNodeHeight(node, height);
}

static void DrawTransportRow(f32 height)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 em = ImGui::GetFontSize();
    const PlaybackProgress progress = BuildPlaybackProgress();

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const f32 availWidth = ImGui::GetContentRegionAvail().x;
    const f32 gap = ImFloor(em * cTransportGroupGapEm);

    const f32 buttonSize = ImFloor(em * cTransportButtonEm);
    const f32 buttonsWidth = buttonSize * 2.0f + style.ItemSpacing.x;

    const bool showVolumeValue = availWidth >= em * cVolumeValueMinEm;
    const f32 faderWidth = sead::Mathf::clamp2(em * cVolumeSliderMinEm, availWidth * cVolumeSliderRatio, cVolumeSliderWidth);
    const f32 masterWidth = MasterVolumeWidth(faderWidth, showVolumeValue);
    const f32 overridesWidth = CalcPlaybackOverrideButtonsWidth();

    const f32 fixedWidth = buttonsWidth + overridesWidth + masterWidth + gap * 3.0f;
    const f32 displayWidth = sead::Mathf::clamp2(em * cDisplayMinWidthEm, availWidth - fixedWidth, em * cDisplayMaxWidthEm);

    const f32 buttonsX = origin.x + ImFloor(sead::Mathf::max((availWidth - fixedWidth - displayWidth) * 0.5f, 0.0f));
    const f32 displayX = buttonsX + buttonsWidth + gap;
    const f32 overridesX = displayX + displayWidth + gap;
    const f32 masterX = overridesX + overridesWidth + gap;

    ImGui::SetCursorScreenPos(ImVec2(buttonsX, origin.y + ImFloor((height - buttonSize) * 0.5f)));
    DrawTransportButtons(buttonSize);

    ImGui::SetCursorScreenPos(ImVec2(displayX, origin.y));
    DrawTransportDisplay(progress, displayWidth, height);

    ImGui::SetCursorScreenPos(ImVec2(overridesX, origin.y + ImFloor((height - ImGui::GetFrameHeight()) * 0.5f)));
    DrawPlaybackOverrideButtons();

    ImGui::SetCursorScreenPos(ImVec2(masterX, origin.y));
    DrawMasterVolume(faderWidth, height, showVolumeValue);

    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(availWidth, height));
}

static void DrawTransportWindow()
{
    if (ImGui::Begin(sead::FormatFixedSafeString<64>(ICON_LC_MUSIC " %s###PlayerWindow", messages::common::cPlayer).cstr(), nullptr,ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        ImGuiWindow* win = ImGui::GetCurrentWindow();
        if (win && win->DockNode)
            win->DockNode->LocalFlags |= ImGuiDockNodeFlags_NoResize;

        const f32 height = ImFloor(ImGui::GetFontSize() * cTransportHeightEm);
        const f32 drawerWidth = ImGui::GetContentRegionAvail().x;
        const PlaybackDrawerLayout drawerLayout = CalcPlaybackDrawerLayout();

        DrawPlaybackDrawer(drawerLayout, CalcPlaybackDrawerHeight(drawerLayout, drawerWidth));
        DrawTransportRow(height);
        UpdatePlayerBarHeight(height, CalcPlaybackDrawerHeight(drawerLayout, drawerWidth));
    }
    ImGui::End();
}

void DrawPlayerUI()
{
    HandleTransportShortcuts();
    DrawTransportWindow();
    sSoundPlayer.update();
}
