#pragma once

#include <basis/seadTypes.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

enum class UIType
{
    Min = 0,

    ProjectInfo = Min,

    AllSounds,
    StreamSounds,
    WaveSounds,
    SequenceSounds,

    AllSoundSets,
    WaveSoundSets,
    SequenceSoundSets,

    Banks,
    WaveArchives,
    Groups,
    Players,

    WaveFiles,
    SequenceFiles,
    BankFiles,

    FileStatistics,

    Max = FileStatistics
};

extern UIType sSelectedUIType;

void SetUITab(UIType type);
void FocusInfoWindow();
void FocusPropertiesWindow();

void DrawUI();

class Sound;
Sound* GetSelectedStreamSound();
bool IsStreamPanelShown();

bool IsSystemWindowShown();
void SetSystemWindowShown(bool shown);

void RequestResetLayout();

void DrawEmptyDetailPane(const char* text);
void DrawSplitDivider(f32 listWidth, f32 gap);

inline constexpr f32 cSplitListRatio = 0.5f;

template <typename ListFn, typename DetailFn>
void DrawSplitView(const char *id, f32 bottomReserve, ListFn list, DetailFn detail)
{
    const ImGuiWindowFlags paneFlags = ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::PushID(id);
    ImGui::BeginChild("Split", ImVec2(0.0f, -bottomReserve), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const f32 gap = ImGui::GetStyle().WindowPadding.x * 2.0f;
    const f32 listWidth = ImFloor((ImGui::GetContentRegionAvail().x - gap) * cSplitListRatio);

    DrawSplitDivider(listWidth, gap);

    ImGui::BeginChild("List", ImVec2(listWidth, 0.0f), ImGuiChildFlags_None, paneFlags);
    list();
    ImGui::EndChild();

    ImGui::SameLine(0.0f, gap);

    ImGui::BeginChild("Detail", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, paneFlags);
    detail();
    ImGui::EndChild();

    ImGui::EndChild();
    ImGui::PopID();
}
