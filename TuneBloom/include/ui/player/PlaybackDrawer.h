#pragma once

#include <imgui/imgui.h>

#include <basis/seadTypes.h>

enum class PlaybackDrawerView
{
    Parameters,
    Variables
};

enum PlaybackPanel
{
    PlaybackPanel_Sound,
    PlaybackPanel_Filter,
    PlaybackPanel_Sequence,
    PlaybackPanel_Tracks,
    PlaybackPanel_Num
};

struct PlaybackDrawerLayout
{
    ImVec2 padding;
    ImVec2 knobSize;
    f32 knobGap;
    f32 listWidth;
    f32 trackKnobSize;
    f32 trackPitch;
    f32 panelWidths[PlaybackPanel_Num];
    f32 parametersWidth;
    f32 parametersHeight;
    f32 seqVarLabelWidth;
    f32 seqVarValueWidth;
    f32 seqVarWidth;
    f32 seqVarTableHeight;
    f32 seqVarHeight;
};

bool IsPlaybackDrawerOpen();
void SetPlaybackDrawerOpen(bool open);
PlaybackDrawerView GetPlaybackDrawerView();
void SetPlaybackDrawerView(PlaybackDrawerView view);

PlaybackDrawerLayout CalcPlaybackDrawerLayout();
f32 CalcPlaybackDrawerHeight(const PlaybackDrawerLayout& layout, f32 availWidth);
void DrawPlaybackDrawer(const PlaybackDrawerLayout& layout, f32 height);

f32 CalcPlaybackOverrideButtonsWidth();
void DrawPlaybackOverrideButtons();
