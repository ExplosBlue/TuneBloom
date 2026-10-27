#pragma once

#include <ui/items/ItemList.h>

#include <vector>

class Sound;
class SoundSet;
struct StreamSource;

void DrawAllSoundsUI();
void DrawStreamSoundsUI();
void DrawWaveSoundsUI();
void DrawSequenceSoundsUI();

void DrawAllSoundSetsUI();
void DrawWaveSoundSetsUI();
void DrawSequenceSoundSetsUI();

struct SoundExportCheck
{
    bool canExport = true;
    bool onlySequence = true;
    sead::FixedSafeString<128> reason;
};

SoundExportCheck CheckSoundExport(const std::vector<Sound*>& sounds);
bool CanExportSoundSetMidi(const std::vector<SoundSet*>& soundSets);

void DrawSoundPropertiesUI();
bool ReloadStreamFile(Sound* sound);
bool PickStreamFilePath(sead::BufferedSafeString* outPath, Item* owner);

InstanciateItemCallback CreateSoundFunc(bool clear);
InstanciateItemCallback CreateStreamSoundFunc(bool clear);
InstanciateItemCallback CreateWaveSoundFunc(bool clear);
InstanciateItemCallback CreateSequenceSoundFunc(bool clear);

void DrawSoundSetPropertiesUI();
void InsertSoundIntoSet(SoundSet* targetSet, Sound* newSound, Item* afterItem);
void InsertSoundAtHook(Item *afterItem, Item *newItem);
void RemoveSoundWithCascade(Item *item);

void DrawStreamTracksUI(const StreamSource& source);
bool DrawStreamTracksTabRow(const StreamSource& source);
void DrawStreamTracksSection(const StreamSource& source, f32 height);
void HighlightStreamTrack(Item* track);

bool IsTrackMixerView();
void SetTrackMixerView(bool mixer);
