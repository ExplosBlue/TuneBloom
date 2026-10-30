#include <bfsar/archive/Bfsar.h>

#include <core/Debug.h>

#include <bfsar/files/BfstmFile.h>
#include <bfsar/archive/SaveReport.h>

#include <ui/Messages.h>
#include <ui/app/PopupMgr.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>

bool Bfsar::validName(const sead::SafeString& name)
{
    if (name.isEmpty())
    {
        return false;
    }

    if (!name.startsWith("_") && !::isalpha(name[0]))
    {
        return false;
    }

    auto isValidNameChar = [](int c) -> int
    {
        return ::isalnum(c) || c == '_';
    };

    std::string n = name.cstr();

    if (!std::all_of(n.begin(), n.end(), isValidNameChar))
    {
        return false;
    }

    return true;
}

struct BfsarNameCounts
{
    std::unordered_map<std::string, u32> counts;
};

std::array<const Item::List*, 6> Bfsar::getNamedLists() const
{
    return { &mSoundList, &mSoundSetList, &mBankList, &mWaveArchiveList, &mGroupList, &mPlayerList };
}

static u32 CountOtherNames(const BfsarNameCounts& names, const std::array<const Item::List*, 6>& lists, const sead::SafeString& name, const Item* ignoreItem)
{
    const auto it = names.counts.find(name.cstr());
    const u32 count = it != names.counts.end() ? it->second : 0;
    const bool countsIgnoreItem = ignoreItem && name == ignoreItem->getName() && std::find(lists.begin(), lists.end(), ignoreItem->list()) != lists.end();

    return countsIgnoreItem ? count - 1 : count;
}

bool Bfsar::validateName(const sead::SafeString& name, const Item* ignoreItem) const
{
    if (name.isEmpty())
        return false;

    const std::array<const Item::List*, 6> lists = getNamedLists();

    if (mValidateNameCounts)
        return CountOtherNames(*mValidateNameCounts, lists, name, ignoreItem) == 0;

    for (const Item::List* list : lists)
    {
        for (auto it = list->robustBegin(); it != list->robustEnd(); ++it)
        {
            const Item* item = (*it).val();
            SEAD_ASSERT(item);

            if (item != ignoreItem && name == item->getName())
                return false;
        }
    }

    return true;
}

bool Bfsar::validateName(const sead::SafeString& name) const
{
    return validateName(name, nullptr);
}

bool Bfsar::validateName(const Item& item) const
{
    return validateName(item.getName(), &item);
}

struct BfsarValidateContext
{
    bool showErrors = true;
    bool toStderr = false;
    std::unordered_map<std::string, const Sound*> streamSounds;

    bool refuse(const sead::SafeString& error, const Item* item, const Item* owner = nullptr) const
    {
        if (showErrors)
            ReportArchiveError(toStderr, {error, const_cast<Item*>(item), const_cast<Item*>(owner)});

        return false;
    }
};

static bool IsPartOfItem(const Item* item)
{
    return item->getItemType() == Item::ItemType::StreamTrack ||
           item->getItemType() == Item::ItemType::GroupItemInfo ||
           item->getItemType() == Item::ItemType::BankFileInstrument;
}

static const ItemReference* FindOtherArchiveReference(const Item& target)
{
    const Bfsar* archive = target.getArchive();

    if (!archive)
        return nullptr;

    for (const ItemReference* reference : target.getReferences())
    {
        const Bfsar* ownerArchive = reference->getOwner()->getArchive();

        if (ownerArchive && ownerArchive != archive)
            return reference;
    }

    return nullptr;
}

static bool ValidateItem(const BfsarValidateContext& ctx, const Item* item)
{
    sead::FixedSafeString<1024> error;

    if (const ItemReference* reference = FindOtherArchiveReference(*item))
    {
        error.format(messages::validation::cOtherArchiveItemFormat, item->getFormattedName().cstr());
        return ctx.refuse(error, reference->getOwner());
    }

    const Item* problemItem = item->validate(error);

    if (!problemItem)
        return true;

    if (error.isEmpty())
        error = messages::common::cUnknown;

    return ctx.refuse(error, problemItem, IsPartOfItem(problemItem) ? item : nullptr);
}

static bool ValidateItems(const BfsarValidateContext& ctx, const Item::List& list)
{
    for (auto it = list.robustBegin(); it != list.robustEnd(); ++it)
    {
        const Item* item = (*it).val();
        SEAD_ASSERT(item);

        if (!ValidateItem(ctx, item))
            return false;
    }

    return true;
}

static bool StreamTrackMixMatches(const Sound::StreamSoundInfo::Track& track, const Sound::StreamSoundInfo::Track& masterTrack)
{
    return track.getVolume() == masterTrack.getVolume() && track.getPan() == masterTrack.getPan() &&
           track.getSPan() == masterTrack.getSPan() && track.getFlags() == masterTrack.getFlags();
}

bool Bfsar::validateSharedStream_(BfsarValidateContext& ctx, const Sound* sound) const
{
    auto pair = ctx.streamSounds.insert({sound->getStreamSoundInfo().getPath().cstr(), sound});

    if (pair.second)
        return true;

    const Sound* masterSound = pair.first->second;

    const Sound::StreamSoundInfo::Track::List& tracks = sound->getStreamSoundInfo().getTrackList();
    const Sound::StreamSoundInfo::Track::List& masterTracks = masterSound->getStreamSoundInfo().getTrackList();

    sead::FixedSafeString<1024> error;

    if (tracks.size() != masterTracks.size())
    {
        error.format(messages::validation::cSharedStreamTrackCountFormat, masterSound->getFormattedName().cstr());
        return ctx.refuse(error, sound);
    }

    const bool tracksInArchive = !BfstmFile::IsTrackInfoAvailable(getVersionForBfstm(), mFormat);

    for (u32 trackNo = 0; trackNo < tracks.size(); trackNo++)
    {
        const Sound::StreamSoundInfo::Track& track = *static_cast<const Sound::StreamSoundInfo::Track*>(tracks.nth(trackNo)->val());
        const Sound::StreamSoundInfo::Track& masterTrack = *static_cast<const Sound::StreamSoundInfo::Track*>(masterTracks.nth(trackNo)->val());

        if (track.getWaveFileRef().getItem() != masterTrack.getWaveFileRef().getItem())
        {
            error.format(messages::validation::cSharedStreamWaveFileFormat, masterSound->getFormattedName().cstr());
            return ctx.refuse(error, &track, sound);
        }

        if (tracksInArchive || StreamTrackMixMatches(track, masterTrack))
            continue;

        error.format(messages::validation::cSharedStreamTracksIdenticalFormat, masterSound->getFormattedName().cstr());
        return ctx.refuse(error, &track, sound);
    }

    return true;
}

bool Bfsar::validateSounds_(BfsarValidateContext& ctx) const
{
    for (auto it = mSoundList.robustBegin(); it != mSoundList.robustEnd(); ++it)
    {
        const Item* item = (*it).val();
        SEAD_ASSERT(item);

        const Sound* sound = static_cast<const Sound*>(item);
        sound->mOwnerSet = nullptr;

        if (!ValidateItem(ctx, item))
            return false;

        if (sound->getSoundType() == Sound::SoundType::Strm && !validateSharedStream_(ctx, sound))
            return false;
    }

    return true;
}

bool Bfsar::validateWaveSoundOwners_(const BfsarValidateContext& ctx) const
{
    for (auto it = mSoundList.robustBegin(); it != mSoundList.robustEnd(); ++it)
    {
        const Sound* sound = static_cast<const Sound*>((*it).val());
        SEAD_ASSERT(sound);

        if (sound->getSoundType() != Sound::SoundType::Wave)
            continue;

        const SoundSet* soundSet = sound->mOwnerSet;

        if (!soundSet)
            return ctx.refuse(messages::validation::cWaveSoundNeedsSoundSet, sound);

        if (soundSet->getSoundSetType() != SoundSet::SoundSetType::Wave)
            return ctx.refuse(messages::validation::cWaveSoundNeedsWaveSoundSet, soundSet);
    }

    return true;
}

bool Bfsar::validate(bool showErrors)
{
    LOG_FUNC();

    BfsarValidateContext ctx;
    ctx.showErrors = showErrors;
    ctx.toStderr = mCliMode;

    BfsarNameCounts names;

    for (const Item::List* list : getNamedLists())
    {
        for (auto it = list->robustBegin(); it != list->robustEnd(); ++it)
            names.counts[(*it).val()->getName().cstr()]++;
    }

    mValidateNameCounts = &names;

    const bool isValid = validateSounds_(ctx) &&
                         ValidateItems(ctx, mSoundSetList) &&
                         validateWaveSoundOwners_(ctx) &&
                         ValidateItems(ctx, mBankList) &&
                         ValidateItems(ctx, mWaveArchiveList) &&
                         ValidateItems(ctx, mGroupList) &&
                         ValidateItems(ctx, mPlayerList) &&
                         ValidateItems(ctx, mWaveFileList) &&
                         ValidateItems(ctx, mSequenceFileList) &&
                         ValidateItems(ctx, mBankFileList);

    mValidateNameCounts = nullptr;

    return isValid;
}
