#include <bfsar/archive/Bfsar.h>
#include <bfsar/files/BankFile.h>
#include <bfsar/files/SequenceFile.h>
#include <bfsar/files/WaveFile.h>
#include <bfsar/model/Bank.h>
#include <bfsar/model/Group.h>
#include <bfsar/model/Item.h>
#include <bfsar/model/Player.h>
#include <bfsar/model/Sound.h>
#include <bfsar/model/SoundSet.h>
#include <bfsar/model/WaveArchive.h>
#include <messages/Messages.h>

#include <snd/StreamSoundPlayer.h>

const Item* Item::validate(sead::BufferedSafeString& error) const
{
    error = messages::cNotImplemented;
    return this;
}

bool Item::validateName(sead::BufferedSafeString& error) const
{
    if (!Bfsar::validName(getName()))
    {
        error = messages::item::cNameInvalidDetail;
        return false;
    }

    const Bfsar* archive = getArchive();

    if (archive && !archive->validateName(getName(), this))
    {
        error = messages::item::cNameDuplicated;
        return false;
    }

    return true;
}

bool Item::validateWaveArchive(WaveArchiveType type, const ItemReference& ref, sead::BufferedSafeString& error)
{
    switch (type)
    {
        case WaveArchiveType::AutomaticShared:
        case WaveArchiveType::AutomaticIndividual:
            return true;

        case WaveArchiveType::Explicit:
            if (ref.isAttached())
                return true;

            break;

        default:
            break;
    }

    error = messages::validation::cInvalidWaveArchive;
    return false;
}

const Item* Bank::validate(sead::BufferedSafeString& error) const
{
    if (!Item::validateName(error))
    {
        return this;
    }

    if (!Item::validateWaveArchive(getWaveArchiveType(), getWaveArchiveRef(), error))
        return this;

    return nullptr;
}

const Item* Group::validate(sead::BufferedSafeString& error) const
{
    if (!Item::validateName(error))
    {
        return this;
    }

    switch (getOutputType())
    {
        case OutputType::Embed:
        case OutputType::Link:
            break;

        case OutputType::External:
            if (mExternalFileState == ExternalFileState::Missing)
            {
                error = messages::group::cErrorExternalFileMissing;
                return this;
            }

            break;

        default:
            error = messages::group::cErrorInvalidOutputType;
            return this;
    }

    sead::FixedSafeString<256> itemError;

    for (const Item *item : mItemInfoList)
    {
        const ItemInfo &itemInfo = *static_cast<const ItemInfo *>(item);

        if (!itemInfo.getIsDisabled())
        {
            if (itemInfo.validate(itemError))
            {
                error.format("'%s': %s", itemInfo.getFormattedName().cstr(), itemError.cstr());
                return &itemInfo;
            }
        }

        itemError.clear();
    }

    return nullptr;
}

const Item* Group::ItemInfo::validate(sead::BufferedSafeString& error) const
{
    if (mItemRefType != ItemType::Sound && mItemRefType != ItemType::SoundSet && mItemRefType != ItemType::Bank && mItemRefType != ItemType::WaveArchive)
    {
        error = messages::group::cErrorInvalidItemType;
        return this;
    }

    if (!mItemRef.isAttached())
    {
        error = messages::group::cErrorItemNotAttached;
        return this;
    }

    switch (mItemRefType)
    {
        case ItemType::Sound:
        {
            const Sound* sound = static_cast<const Sound*>(mItemRef.getItem());
            if (sound->getSoundType() == Sound::SoundType::Seq)
            {
                if (validateSequence_(*sound, error))
                {
                    return nullptr;
                }
                else
                {
                    return this;
                }
            }

            const bool isStream = sound->getSoundType() == Sound::SoundType::Strm;

            const Bfsar* archive = sound->getArchive();

            if (archive && !archive->isV3Bfsar() && !(isStream && sound->getStreamSoundInfo().hasPrefetch()))
            {
                error = isStream && archive->isStreamPrefetchBuildable() ? messages::group::cErrorStreamNeedsPrefetch : messages::group::cErrorSoundNotSequence;
                return this;
            }

            return nullptr;
        }

        case ItemType::SoundSet:
        {
            const SoundSet* soundSet = static_cast<const SoundSet*>(mItemRef.getItem());
            const Bfsar* archive = soundSet->getArchive();

            if (archive && soundSet->getSoundSetType() == SoundSet::SoundSetType::Seq)
            {
                const Item::List& soundList = archive->getSoundList();

                for (const Item::ListNode* itemNode = archive->getItem(soundSet->getStartId(), soundList); itemNode && itemNode->val()->getId() <= soundSet->getEndId(); itemNode = soundList.next(itemNode))
                {
                    SEAD_ASSERT(itemNode->val()->getItemType() == Item::ItemType::Sound);
                    const Sound* sound = static_cast<const Sound*>(itemNode->val());

                    if (sound->getSoundType() == Sound::SoundType::Seq)
                    {
                        if (!validateSequence_(*sound, error))
                        {
                            error = messages::group::cErrorSequenceSetLoadDetail;
                            return this;
                        }
                    }
                }
            }
            else if (soundSet->getSoundSetType() == SoundSet::SoundSetType::Wave)
            {
                if (validateWaveSoundSet_(*soundSet, error, getLoadFlag()))
                {
                    return nullptr;
                }
                else
                {
                    return this;
                }
            }

            break;
        }

        case ItemType::Bank:
        {
            const Bank* bank = static_cast<const Bank*>(mItemRef.getItem());
            if (validateBank_(*bank, error, getLoadFlag()))
            {
                return nullptr;
            }
            else
            {
                return this;
            }

            break;
        }

        case ItemType::WaveArchive:
        {
            if (getLoadFlag() != LoadFlag::LoadAll)
            {
                error = messages::group::cErrorWaveArchiveLoadAll;
                return this;
            }

            break;
        }

        default:
            break;
    }

    return nullptr;
}

bool Group::ItemInfo::validateSequence_(const Sound& sound, sead::BufferedSafeString& error) const
{
    SEAD_ASSERT(sound.getSoundType() == Sound::SoundType::Seq);

    if (mLoadItem == (u32)SequenceItems::Sequence)
    {
        return true;
    }

    for (u32 i = 0; i < 4; i++)
    {
        const Bank* bank = static_cast<const Bank*>(sound.getSequenceSoundInfo().getBankRef(i).getItem());
        if (!bank)
        {
            continue;
        }

        if (!validateBank_(*bank, error, getLoadFlag()))
        {
            error = messages::group::cErrorSequenceLoadDetail;
            return false;
        }
    }

    return true;
}

bool Group::ItemInfo::validateWaveSoundSet_(const SoundSet& soundSet, sead::BufferedSafeString& error, u32 loadFlags) const
{
    if (soundSet.getWaveArchiveType() == WaveArchiveType::AutomaticShared || soundSet.getWaveArchiveType() == WaveArchiveType::AutomaticIndividual)
    {
        if ((loadFlags & (LoadFlag::LoadBank | LoadFlag::LoadWarc)) != (LoadFlag::LoadBank | LoadFlag::LoadWarc))
        {
            error = messages::group::cErrorAutomaticLoadAllDetail;
            return false;
        }
    }

    return true;
}

bool Group::ItemInfo::validateBank_(const Bank& bank, sead::BufferedSafeString& error, u32 loadFlags) const
{
    if (bank.getWaveArchiveType() == WaveArchiveType::AutomaticShared || bank.getWaveArchiveType() == WaveArchiveType::AutomaticIndividual)
    {
        if ((loadFlags & (LoadFlag::LoadBank | LoadFlag::LoadWarc)) != (LoadFlag::LoadBank | LoadFlag::LoadWarc))
        {
            error = messages::group::cErrorAutomaticLoadAllDetail;
            return false;
        }
    }

    return true;
}

const Item* Player::validate(sead::BufferedSafeString& error) const
{
    if (!Item::validateName(error))
        return this;

    return nullptr;
}

const Item* WaveArchive::validate(sead::BufferedSafeString& error) const
{
    if (!Item::validateName(error))
    {
        return this;
    }

    return nullptr;
}

static bool ValidateSequenceSound(const Sound::SequenceSoundInfo& seqInfo, sead::BufferedSafeString& error)
{
    if (!seqInfo.getSequenceFileRef().isAttached())
    {
        error = messages::validation::cInvalidSequenceFile;
        return false;
    }

    const SequenceFile& seqFile = *static_cast<const SequenceFile*>(seqInfo.getSequenceFileRef().getItem());
    if (!seqFile.isValid())
    {
        error = messages::sequence::cNotCompiled;
        return false;
    }

    if (seqFile.getLabelOffset(seqInfo.getStartLabel()) == SequenceFile::cInvaldOffset)
    {
        error = messages::sequence::cInvalidStartLabel;
        return false;
    }

    return true;
}

static bool ValidateStreamTracks(const Sound::StreamSoundInfo& strmInfo, sead::BufferedSafeString& error)
{
    const Sound::StreamSoundInfo::Track::List& tracks = strmInfo.getTrackList();

    if (tracks.size() > cStrmTrackNum)
    {
        error.format(messages::stream::cTrackLimitFormat, cStrmTrackNum);
        return false;
    }

    if (tracks.isEmpty() && strmInfo.getAllocateChannelCount() == 0)
    {
        error = messages::stream::cNoTracksOrChannels;
        return false;
    }

    const WaveFile* mainWaveFile = nullptr;

    for (s32 i = 0; i < tracks.size(); i++)
    {
        const Sound::StreamSoundInfo::Track& track = *static_cast<const Sound::StreamSoundInfo::Track*>(tracks.nth(i)->val());

        if (!track.getWaveFileRef().isAttached())
        {
            if (track.getChannels_().isEmpty())
            {
                error.format(messages::playback::cTrackNoWaveFileFormat, i);
                return false;
            }

            continue;
        }

        const WaveFile& waveFile = *static_cast<const WaveFile*>(track.getWaveFileRef().getItem());

        if (!mainWaveFile)
        {
            mainWaveFile = &waveFile;
            continue;
        }

        if (mainWaveFile->getEncoding() != waveFile.getEncoding())
        {
            error = messages::stream::cTracksSameEncoding;
            return false;
        }

        if (mainWaveFile->getSampleRate() != waveFile.getSampleRate())
        {
            error = messages::stream::cTracksSameSampleRate;
            return false;
        }
    }

    return true;
}

static bool ValidateStreamSound(const Sound::StreamSoundInfo& strmInfo, sead::BufferedSafeString& error)
{
    if (strmInfo.getPath().isEmpty())
    {
        error = messages::stream::cNoPath.text;
        return false;
    }

    if (strmInfo.getStreamType() == Sound::StreamSoundInfo::StreamType::NwStreamBinary)
    {
        if (!ValidateStreamTracks(strmInfo, error))
            return false;
    }
    else
    {
        //? Dont scream as we simply dont save
        // error = "Only BFSTM streams are supported";
        // return this;
    }

    return true;
}

static bool ValidateWaveSound(const Sound::WaveSoundInfo& waveInfo, sead::BufferedSafeString& error)
{
    if (!waveInfo.getWaveFileRef().isAttached())
    {
        error = messages::validation::cInvalidWaveFile;
        return false;
    }

    return true;
}

const Item* Sound::validate(sead::BufferedSafeString& error) const
{
    if (!Item::validateName(error))
        return this;

    if (getPlayerId() == Item::cInvalidId)
    {
        error = messages::validation::cInvalidPlayer;
        return this;
    }

    switch (mSoundType)
    {
        case SoundType::Seq:
            return ValidateSequenceSound(getSequenceSoundInfo(), error) ? nullptr : this;

        case SoundType::Strm:
            return ValidateStreamSound(getStreamSoundInfo(), error) ? nullptr : this;

        case SoundType::Wave:
            return ValidateWaveSound(getWaveSoundInfo(), error) ? nullptr : this;

        default:
            error = messages::validation::cInvalidSoundType;
            return this;
    }
}

const Item* SoundSet::validate(sead::BufferedSafeString& error) const
{
    if (!Item::validateName(error))
    {
        return this;
    }

    switch (getSoundSetType())
    {
        case SoundSetType::Wave:
            if (!Item::validateWaveArchive(getWaveArchiveType(), getWaveArchiveRef(), error))
                return this;

            break;

        case SoundSetType::Seq:
            break;

        default:
            error = messages::validation::cInvalidSoundType;
            return this;
    }

    if (getIsEmpty())
    {
        return nullptr;
    }

    if (getEndId() < getStartId())
    {
        error = messages::soundset::cErrorInvalidRange;
        return this;
    }

    const Bfsar* archive = getArchive();
    const u32 soundCount = archive ? static_cast<u32>(archive->getSoundList().size()) : 0;

    if (getStartId() >= soundCount)
    {
        error = messages::soundset::cErrorStartExceedsCount;
        return this;
    }

    if (getEndId() >= soundCount)
    {
        error = messages::soundset::cErrorEndExceedsCount;
        return this;
    }

    const Item::List& soundList = archive->getSoundList();

    for (const Item::ListNode* itemNode = archive->getItem(getStartId(), soundList); itemNode && itemNode->val()->getId() <= getEndId(); itemNode = soundList.next(itemNode))
    {
        SEAD_ASSERT(itemNode->val()->getItemType() == Item::ItemType::Sound);
        const Sound* sound = static_cast<const Sound*>(itemNode->val());

        if (sound->mOwnerSet)
        {
            error.format(messages::soundset::cErrorSoundInOtherSetFormat,sound->getFormattedName().cstr(), sound->mOwnerSet->getFormattedName().cstr());
            return this;
        }

        sound->mOwnerSet = this;
    }

    return nullptr;
}
