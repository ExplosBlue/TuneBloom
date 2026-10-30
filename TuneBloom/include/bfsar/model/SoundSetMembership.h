#pragma once

#include <vector>

class Bfsar;
class Sound;
class SoundSet;

struct SoundSetMembership
{
    std::vector<Sound*> sounds;
    std::vector<SoundSet*> owners;
};

enum class SoundSetEditRefusal
{
    None,
    SharedSounds,
    InvalidRange,
    WrongType,
    SplitSet
};

struct SoundSetEditResult
{
    SoundSetEditRefusal refusal = SoundSetEditRefusal::None;
    SoundSet* set = nullptr;
    SoundSet* otherSet = nullptr;
    Sound* sound = nullptr;
    bool changed = false;
};

bool SoundFitsSet(const Sound& sound, const SoundSet& set);

SoundSetEditResult CaptureSoundSetMembership(Bfsar& archive, SoundSetMembership* membership);
void CommitSoundSetMembership(Bfsar& archive, const SoundSetMembership& membership);
SoundSet* FindSplitSoundSet(const SoundSetMembership& membership);

SoundSetEditResult MoveSoundsAfter(Bfsar& archive, Sound* target, const std::vector<Sound*>& sounds);
SoundSetEditResult InsertSoundAfter(Bfsar& archive, Sound* afterSound, Sound* newSound);
SoundSetEditResult InsertSoundIntoSet(Bfsar& archive, SoundSet* set, Sound* afterSound, Sound* newSound);
SoundSetEditResult RemoveSound(Bfsar& archive, Sound* sound);
