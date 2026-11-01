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
bool InsertSoundHook(Item* afterItem, Item* newItem);
bool InsertSoundIntoSetHook(SoundSet* soundSet, Item* afterItem, Item* newItem);
bool RemoveSoundHook(Item* item);
void MoveSoundsHook(Item* target, const std::vector<Item*>& items);

void DrawStreamTracksUI(const StreamSource& source);
bool DrawStreamTracksTabRow(const StreamSource& source);
void DrawStreamTracksSection(const StreamSource& source, f32 reserveBelow);
void HighlightStreamTrack(Item* track);
