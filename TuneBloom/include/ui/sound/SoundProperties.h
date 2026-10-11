#pragma once

#include <ui/app/UI.h>

inline constexpr f32 cSoundSideGapEm = 1.0f;

f32 TrailingButtonsWidth(s32 count);
bool TrailingButton(const char* label);
bool ItemSelectorRow(const char* label, const Item::List& list, Item** item, bool allowNone = false, f32 trailingWidth = 0.0f);
bool DrawSoundPanelHeader(const char* title, bool* enabled);

template <typename DrawFn>
void DrawSoundSideField(const char* label, const char* detail, f32 fieldWidth, f32 knobHeight, DrawFn draw)
{
    const f32 em = ImGui::GetFontSize();

    ImGui::SameLine(0.0f, ImFloor(em * cSoundSideGapEm));

    const bool fits = ImGui::GetContentRegionAvail().x >= ImGui::CalcTextSize(label).x + ImGui::GetStyle().ItemSpacing.x + fieldWidth;
    if (!fits)
        ImGui::NewLine();

    ImGui::BeginGroup();

    const ImVec2 origin = ImGui::GetCursorPos();
    const f32 y = fits ? origin.y + ImFloor((knobHeight - em - ImGui::GetFrameHeight()) * 0.5f) : origin.y;

    ImGui::SetCursorPos(ImVec2(origin.x, y + ImGui::GetStyle().FramePadding.y));
    ImGui::TextUnformatted(label);
    DetailTooltip(detail);

    ImGui::SetCursorPos(ImVec2(origin.x + ImGui::CalcTextSize(label).x + ImGui::GetStyle().ItemSpacing.x, y));
    draw();

    ImGui::EndGroup();
}

void DrawSoundMixPanels(Sound* sound);

void DrawSequenceSoundUI(Sound::SequenceSoundInfo& seqSoundInfo);
void DrawSequenceStartLabelPanel(Sound::SequenceSoundInfo& seqSoundInfo);

void DrawStreamSoundUI(Sound* sound, Sound::StreamSoundInfo& strmSoundInfo);
void DrawStreamExtensionUI(Sound::StreamSoundInfo& strmSoundInfo);

void DrawWaveSoundUI(Sound::WaveSoundInfo& waveSoundInfo);
void DrawWaveEnvelopeSection(Sound::WaveSoundInfo& waveSoundInfo);
void DrawWaveExtraOptionsUI(Sound::WaveSoundInfo& waveSoundInfo);
