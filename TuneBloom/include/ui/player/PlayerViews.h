#pragma once

#include <bfsar/player/SoundPlayer.h>

f32 GetMasterVolumeDb();
void SetMasterVolumeDb(f32 db);
f32 GetMasterVolumePosition();
void SetMasterVolumePosition(f32 position);
bool IsMasterVolumeDefault();
bool ResetMasterVolumeOnRightClick();
void FormatMasterVolume(char* buffer, size_t size);

bool CanTogglePlayback();
void TogglePlayback();
void PlayActiveDocumentFile();
void SetPlaybackLoopEnabled(bool enable);

const Item* ResolveDisplayItem();
const SeqTimeline* ResolveSeqTimeline();

void DrawPlayerUI();
f32 CalcTransportRowHeight();
f32 CalcCompactPlayerBarMaxWidth();
void DrawCompactPlayerBar();
void RegisterPlaybackSettings();

bool DrawSeekBar(const char* id, f32 width, f32 fraction, bool determinate, bool seekable,
                 f32 loopStartFrac, f32 loopEndFrac, f32& outSeekFraction);

void DrawPlayersUI();
void DrawPlayerPropertiesUI();
