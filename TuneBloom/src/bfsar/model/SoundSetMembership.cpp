#include <bfsar/model/SoundSetMembership.h>

#include <bfsar/archive/Bfsar.h>
#include <bfsar/model/Sound.h>
#include <bfsar/model/SoundSet.h>

#include <algorithm>
#include <functional>

namespace
{

struct SoundSetRun
{
    SoundSet* set;
    u32 first;
    u32 last;
};

struct SoundSetPlace
{
    SoundSet* prevSet;
    SoundSet* nextSet;
};

using SoundSetSortedList = std::vector<const SoundSet*>;

}

static constexpr u32 cNoSoundIndex = 0xFFFFFFFF;

bool SoundFitsSet(const Sound& sound, const SoundSet& set)
{
    switch (set.getSoundSetType())
    {
        case SoundSet::SoundSetType::Wave:
            return sound.getSoundType() == Sound::SoundType::Wave;

        case SoundSet::SoundSetType::Seq:
            return sound.getSoundType() == Sound::SoundType::Seq;

        default:
            return false;
    }
}

static SoundSetEditResult Refuse(SoundSetEditRefusal refusal, SoundSet* set, SoundSet* otherSet = nullptr, Sound* sound = nullptr)
{
    SoundSetEditResult result;
    result.refusal = refusal;
    result.set = set;
    result.otherSet = otherSet;
    result.sound = sound;

    return result;
}

static bool IsRefused(const SoundSetEditResult& result)
{
    return result.refusal != SoundSetEditRefusal::None;
}

static bool AddSortedSet(SoundSetSortedList* sets, const SoundSet* set)
{
    const auto it = std::lower_bound(sets->begin(), sets->end(), set, std::less<const SoundSet*>());

    if (it != sets->end() && *it == set)
        return false;

    sets->insert(it, set);
    return true;
}

static bool HasSortedSet(const SoundSetSortedList& sets, const SoundSet* set)
{
    return std::binary_search(sets.begin(), sets.end(), set, std::less<const SoundSet*>());
}

static std::vector<SoundSetRun> CollectSoundSetRuns(const std::vector<SoundSet*>& owners)
{
    std::vector<SoundSetRun> runs;

    for (u32 index = 0; index < owners.size(); index++)
    {
        SoundSet* owner = owners[index];

        if (!owner)
            continue;

        if (!runs.empty() && runs.back().set == owner && runs.back().last + 1 == index)
            runs.back().last = index;
        else
            runs.push_back({ owner, index, index });
    }

    return runs;
}

static SoundSetSortedList CollectFilledSets(const std::vector<SoundSet*>& owners)
{
    SoundSetSortedList sets;

    for (const SoundSetRun& run : CollectSoundSetRuns(owners))
        AddSortedSet(&sets, run.set);

    return sets;
}

static u32 FindSoundIndex(const SoundSetMembership& membership, const Sound* sound)
{
    const u32 index = sound ? sound->getId() : cNoSoundIndex;

    if (index >= membership.sounds.size() || membership.sounds[index] != sound)
        return cNoSoundIndex;

    return index;
}

static u32 FindSetEnd(const SoundSetMembership& membership, u32 index)
{
    const SoundSet* set = membership.owners[index];

    while (index + 1 < membership.owners.size() && membership.owners[index + 1] == set)
        index++;

    return index;
}

SoundSetEditResult CaptureSoundSetMembership(Bfsar& archive, SoundSetMembership* membership)
{
    membership->sounds.clear();

    for (Item* item : archive.getSoundList())
        membership->sounds.push_back(static_cast<Sound*>(item));

    const u32 soundCount = static_cast<u32>(membership->sounds.size());
    membership->owners.assign(soundCount, nullptr);

    for (Item* item : archive.getSoundSetList())
    {
        SoundSet* set = static_cast<SoundSet*>(item);

        if (set->getIsEmpty())
            continue;

        if (set->getStartId() > set->getEndId() || set->getEndId() >= soundCount)
            return Refuse(SoundSetEditRefusal::InvalidRange, set);

        for (u32 index = set->getStartId(); index <= set->getEndId(); index++)
        {
            if (membership->owners[index])
                return Refuse(SoundSetEditRefusal::SharedSounds, membership->owners[index], set);

            membership->owners[index] = set;
        }
    }

    return SoundSetEditResult();
}

SoundSet* FindSplitSoundSet(const SoundSetMembership& membership)
{
    SoundSetSortedList seen;

    for (const SoundSetRun& run : CollectSoundSetRuns(membership.owners))
    {
        if (!AddSortedSet(&seen, run.set))
            return run.set;
    }

    return nullptr;
}

void CommitSoundSetMembership(Bfsar& archive, const SoundSetMembership& membership)
{
    SEAD_ASSERT(membership.sounds.size() == membership.owners.size());
    SEAD_ASSERT(!FindSplitSoundSet(membership));

    Item::List& soundList = archive.getSoundList();

    for (Sound* sound : membership.sounds)
        soundList.pushBack(sound);

    SEAD_ASSERT(static_cast<size_t>(soundList.size()) == membership.sounds.size());
    Bfsar::updateList(soundList);

    SoundSetSortedList filled;

    for (const SoundSetRun& run : CollectSoundSetRuns(membership.owners))
    {
        run.set->setIsEmpty(false);
        run.set->setStartId(run.first);
        run.set->setEndId(run.last);

        AddSortedSet(&filled, run.set);
    }

    for (Item* item : archive.getSoundSetList())
    {
        SoundSet* set = static_cast<SoundSet*>(item);

        if (!set->getIsEmpty() && !HasSortedSet(filled, set))
            set->setIsEmpty(true);
    }
}

static bool ChooseSetBetween(const SoundSetPlace& place, const Sound& sound, SoundSet* currentSet, bool currentLeftEmpty, SoundSet** outSet)
{
    *outSet = nullptr;

    if (place.prevSet && place.prevSet == place.nextSet)
    {
        *outSet = place.prevSet;

        return currentSet == place.prevSet || SoundFitsSet(sound, *place.prevSet);
    }

    if (currentSet && (currentSet == place.prevSet || currentSet == place.nextSet || currentLeftEmpty))
        *outSet = currentSet;
    else if (place.prevSet && SoundFitsSet(sound, *place.prevSet))
        *outSet = place.prevSet;
    else if (place.nextSet && SoundFitsSet(sound, *place.nextSet))
        *outSet = place.nextSet;

    return true;
}

static u32 SplitMovedSounds(const SoundSetMembership& before, const std::vector<bool>& isMoved, u32 targetIndex, SoundSetMembership* kept, SoundSetMembership* moved)
{
    u32 insertAt = 0;

    for (u32 index = 0; index < before.sounds.size(); index++)
    {
        SoundSetMembership* part = isMoved[index] ? moved : kept;

        part->sounds.push_back(before.sounds[index]);
        part->owners.push_back(before.owners[index]);

        if (index == targetIndex)
            insertAt = static_cast<u32>(kept->sounds.size());
    }

    return insertAt;
}

static SoundSetEditResult JoinMovedSounds(const SoundSetMembership& kept, u32 insertAt, SoundSetMembership* moved)
{
    const SoundSetSortedList keptSets = CollectFilledSets(kept.owners);
    const SoundSetPlace place = { kept.owners[insertAt - 1], insertAt < kept.owners.size() ? kept.owners[insertAt] : nullptr };

    for (u32 index = 0; index < moved->sounds.size(); index++)
    {
        SoundSet* currentSet = moved->owners[index];
        const bool currentLeftEmpty = currentSet && !HasSortedSet(keptSets, currentSet);

        if (!ChooseSetBetween(place, *moved->sounds[index], currentSet, currentLeftEmpty, &moved->owners[index]))
            return Refuse(SoundSetEditRefusal::WrongType, moved->owners[index], nullptr, moved->sounds[index]);
    }

    return SoundSetEditResult();
}

SoundSetEditResult MoveSoundsAfter(Bfsar& archive, Sound* target, const std::vector<Sound*>& sounds)
{
    SoundSetMembership before;
    SoundSetEditResult result = CaptureSoundSetMembership(archive, &before);

    if (IsRefused(result))
        return result;

    const u32 targetIndex = FindSoundIndex(before, target);
    std::vector<bool> isMoved(before.sounds.size(), false);

    for (const Sound* sound : sounds)
    {
        const u32 index = FindSoundIndex(before, sound);

        if (index != cNoSoundIndex)
            isMoved[index] = true;
    }

    if (targetIndex == cNoSoundIndex || isMoved[targetIndex])
        return result;

    SoundSetMembership after;
    SoundSetMembership moved;
    const u32 insertAt = SplitMovedSounds(before, isMoved, targetIndex, &after, &moved);

    result = JoinMovedSounds(after, insertAt, &moved);

    if (IsRefused(result))
        return result;

    after.sounds.insert(after.sounds.begin() + insertAt, moved.sounds.begin(), moved.sounds.end());
    after.owners.insert(after.owners.begin() + insertAt, moved.owners.begin(), moved.owners.end());

    if (SoundSet* split = FindSplitSoundSet(after))
        return Refuse(SoundSetEditRefusal::SplitSet, split);

    if (after.sounds == before.sounds && after.owners == before.owners)
        return result;

    CommitSoundSetMembership(archive, after);
    result.changed = true;

    return result;
}

static SoundSetEditResult CommitInsertedSound(Bfsar& archive, SoundSetMembership* membership, u32 insertAt, Sound* newSound, SoundSet* owner)
{
    membership->sounds.insert(membership->sounds.begin() + insertAt, newSound);
    membership->owners.insert(membership->owners.begin() + insertAt, owner);

    if (SoundSet* split = FindSplitSoundSet(*membership))
        return Refuse(SoundSetEditRefusal::SplitSet, split);

    CommitSoundSetMembership(archive, *membership);

    SoundSetEditResult result;
    result.changed = true;

    return result;
}

SoundSetEditResult InsertSoundAfter(Bfsar& archive, Sound* afterSound, Sound* newSound)
{
    SoundSetMembership membership;
    const SoundSetEditResult result = CaptureSoundSetMembership(archive, &membership);

    if (IsRefused(result))
        return result;

    const u32 afterIndex = FindSoundIndex(membership, afterSound);
    u32 insertAt = static_cast<u32>(membership.sounds.size());
    SoundSet* owner = nullptr;

    if (afterIndex != cNoSoundIndex)
    {
        insertAt = afterIndex + 1;
        owner = membership.owners[afterIndex];

        if (owner && !SoundFitsSet(*newSound, *owner))
        {
            insertAt = FindSetEnd(membership, afterIndex) + 1;
            owner = nullptr;
        }
    }

    return CommitInsertedSound(archive, &membership, insertAt, newSound, owner);
}

static u32 FindEmptySetPlace(Bfsar& archive, const SoundSet* set)
{
    Item::List& setList = archive.getSoundSetList();

    for (Item::ListNode* node = setList.prev(set); node; node = setList.prev(node))
    {
        const SoundSet* earlierSet = static_cast<const SoundSet*>(node->val());

        if (!earlierSet->getIsEmpty())
            return earlierSet->getEndId() + 1;
    }

    return 0;
}

SoundSetEditResult InsertSoundIntoSet(Bfsar& archive, SoundSet* set, Sound* afterSound, Sound* newSound)
{
    SoundSetMembership membership;
    const SoundSetEditResult result = CaptureSoundSetMembership(archive, &membership);

    if (IsRefused(result))
        return result;

    const u32 afterIndex = FindSoundIndex(membership, afterSound);
    u32 insertAt = 0;

    if (afterIndex != cNoSoundIndex)
        insertAt = afterIndex + 1;
    else if (!set->getIsEmpty())
        insertAt = set->getEndId() + 1;
    else
        insertAt = FindEmptySetPlace(archive, set);

    return CommitInsertedSound(archive, &membership, insertAt, newSound, set);
}

SoundSetEditResult RemoveSound(Bfsar& archive, Sound* sound)
{
    SoundSetMembership membership;
    SoundSetEditResult result = CaptureSoundSetMembership(archive, &membership);

    if (IsRefused(result))
        return result;

    const u32 index = FindSoundIndex(membership, sound);

    if (index != cNoSoundIndex)
    {
        membership.sounds.erase(membership.sounds.begin() + index);
        membership.owners.erase(membership.owners.begin() + index);
    }

    delete sound;

    CommitSoundSetMembership(archive, membership);
    result.changed = true;

    return result;
}
