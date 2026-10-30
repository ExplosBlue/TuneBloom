#include <bfsar/model/Group.h>

#include <bfsar/archive/Bfsar.h>
#include <bfsar/model/Bank.h>
#include <bfsar/model/Sound.h>
#include <bfsar/model/SoundSet.h>
#include <bfsar/model/WaveArchive.h>
#include <messages/Messages.h>

const char* Group::ItemInfo::sItemIdTypes[4] = {
    //"None",
    messages::common::cSound,
    messages::item::cTypeSoundSet,
    messages::common::cBank,
    messages::common::cWaveArchive
};

sead::FixedSafeString<256> Group::ItemInfo::getFormattedName() const
{
    sead::FixedSafeString<256> name;
    name.appendWithFormat("[%u] ", getId());

    if (mIsDisabled)
    {
        name.append(messages::group::cItemDisabled);
    }
    else if (mItemRef.isAttached())
    {
        name.append(mItemRef.getItem()->getNameOrNull());
    }
    else
    {
        name.append(messages::common::cNull);
    }

    return name;
}

u32 Group::ItemInfo::getLoadFlag() const
{
    if (!mItemRef.isAttached())
        return mDetachedLoadFlag;

    switch (mItemRefType)
    {
        case Item::ItemType::Sound:
        {
            const Sound* sound = static_cast<const Sound*>(mItemRef.getItem());
            if (sound->getSoundType() == Sound::SoundType::Seq)
            {
                return getLoadFlagForSequence_();
            }

            break;
        }

        case Item::ItemType::SoundSet:
        {
            const SoundSet* soundSet = static_cast<const SoundSet*>(mItemRef.getItem());
            if (soundSet->getSoundSetType() == SoundSet::SoundSetType::Seq)
            {
                return getLoadFlagForSequence_();
            }
            else if (soundSet->getSoundSetType() == SoundSet::SoundSetType::Wave)
            {
                return getLoadFlagForWaveSoundSet_();
            }

            break;
        }

        case Item::ItemType::Bank:
            return getLoadFlagForBank_();

        case Item::ItemType::WaveArchive:
            return getLoadFlagForWaveArchive_();

        default:
            break;
    }

    return LoadFlag::LoadAll;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc++11-narrowing"

u32 Group::ItemInfo::getLoadFlagForSequence_() const
{
    static u32 sFlags[(u32)SequenceItems::Num] = {
        LoadFlag::LoadAll,
        LoadFlag::LoadSeq  | LoadFlag::LoadBank,
        LoadFlag::LoadSeq  | LoadFlag::LoadWarc,
        LoadFlag::LoadBank | LoadFlag::LoadWarc,
        LoadFlag::LoadSeq,
        LoadFlag::LoadBank,
        LoadFlag::LoadWarc,
    };

    SEAD_ASSERT(mLoadItem < (u32)SequenceItems::Num);
    return sFlags[mLoadItem];
}

u32 Group::ItemInfo::getLoadFlagForWaveSoundSet_() const
{
    static u32 sFlags[(u32)WaveSoundSetItems::Num] = {
        LoadFlag::LoadAll,
        LoadFlag::LoadWsd,
        LoadFlag::LoadWarc,
    };

    SEAD_ASSERT(mLoadItem < (u32)WaveSoundSetItems::Num);
    return sFlags[mLoadItem];
}

u32 Group::ItemInfo::getLoadFlagForBank_() const
{
    static u32 sFlags[(u32)BankItems::Num] = {
        LoadFlag::LoadAll,
        LoadFlag::LoadBank,
        LoadFlag::LoadWarc,
    };

    SEAD_ASSERT(mLoadItem < (u32)BankItems::Num);
    return sFlags[mLoadItem];
}

u32 Group::ItemInfo::getLoadFlagForWaveArchive_() const
{
    static u32 sFlags[(u32)WaveArchiveItems::Num] = {
        LoadFlag::LoadAll
    };

    SEAD_ASSERT(mLoadItem < (u32)WaveArchiveItems::Num);
    return sFlags[mLoadItem];
}

#pragma clang diagnostic pop

const char** Group::ItemInfo::getLoadItems(u32* outCount) const
{
    return GetLoadItems(mItemRef.getItem(), mItemRefType, outCount);
}

const char** Group::ItemInfo::GetLoadItems(const Item* item, ItemType itemType, u32* outCount)
{
    SEAD_ASSERT(outCount);

    static const char* sSequenceSoundLoadItems[] = {
        messages::common::cAll,
        messages::group::cLoadSequenceAndBank,
        messages::group::cLoadSequenceAndWaveArchive,
        messages::group::cLoadBankAndWaveArchive,
        messages::common::cSequence,
        messages::common::cBank,
        messages::common::cWaveArchive
    };

    static const char* sWaveSoundSetLoadItems[] = {
        messages::common::cAll,
        messages::common::cWaveSoundSet,
        messages::common::cWaveArchive
    };

    static const char* sSequenceSetLoadItems[] = {
        messages::common::cAll,
        messages::group::cLoadSequenceSetAndBank,
        messages::group::cLoadSequenceSetAndWaveArchive,
        messages::group::cLoadBankAndWaveArchive,
        messages::group::cLoadSequenceSet,
        messages::common::cBank,
        messages::common::cWaveArchive
    };

    static const char* sBankLoadItems[] = {
        messages::common::cAll,
        messages::common::cBank,
        messages::common::cWaveArchive
    };

    static const char* sWaveArchiveLoadItems[] = {
        messages::common::cAll,
    };

    if (!item)
    {
        *outCount = 0;
        return nullptr;
    }

    switch (itemType)
    {
        case Item::ItemType::Sound:
        {
            const Sound* sound = static_cast<const Sound*>(item);
            if (sound->getSoundType() == Sound::SoundType::Seq)
            {
                *outCount = IM_ARRAYSIZE(sSequenceSoundLoadItems);
                return sSequenceSoundLoadItems;
            }

            break;
        }

        case Item::ItemType::SoundSet:
        {
            const SoundSet* soundSet = static_cast<const SoundSet*>(item);
            if (soundSet->getSoundSetType() == SoundSet::SoundSetType::Seq)
            {
                *outCount = IM_ARRAYSIZE(sSequenceSetLoadItems);
                return sSequenceSetLoadItems;
            }
            else if (soundSet->getSoundSetType() == SoundSet::SoundSetType::Wave)
            {
                *outCount = IM_ARRAYSIZE(sWaveSoundSetLoadItems);
                return sWaveSoundSetLoadItems;
            }

            break;
        }

        case Item::ItemType::Bank:
            *outCount = IM_ARRAYSIZE(sBankLoadItems);
            return sBankLoadItems;

        case Item::ItemType::WaveArchive:
            *outCount = IM_ARRAYSIZE(sWaveArchiveLoadItems);
            return sWaveArchiveLoadItems;

        default:
            break;
    }

    *outCount = 0;
    return nullptr;
}

void Group::ItemInfo::setLoadItems(u32 loadFlags)
{
    switch (mItemRefType)
    {
        case Item::ItemType::Sound:
        {
            const Sound* sound = static_cast<const Sound*>(mItemRef.getItem());
            if (sound->getSoundType() == Sound::SoundType::Seq)
            {
                setLoadItemsForSequence_(loadFlags);
            }

            break;
        }

        case Item::ItemType::SoundSet:
        {
            const SoundSet* soundSet = static_cast<const SoundSet*>(mItemRef.getItem());
            if (soundSet->getSoundSetType() == SoundSet::SoundSetType::Seq)
            {
                setLoadItemsForSequence_(loadFlags);
            }
            else if (soundSet->getSoundSetType() == SoundSet::SoundSetType::Wave)
            {
                setLoadItemsForWaveSoundSet_(loadFlags);
            }

            break;
        }

        case Item::ItemType::Bank:
            setLoadItemsForBank_(loadFlags);
            break;

        case Item::ItemType::WaveArchive:
            setLoadItemsForWaveArchive_(loadFlags);
            break;

        default:
            break;
    }
}

void Group::ItemInfo::setLoadItemsForSequence_(u32 loadFlags)
{
    if (loadFlags == LoadFlag::LoadAll)
    {
        mLoadItem = (u32)SequenceItems::All;
    }
    else if (loadFlags == (LoadFlag::LoadSeq | LoadFlag::LoadBank))
    {
        mLoadItem = (u32)SequenceItems::SequenceAndBank;
    }
    else if (loadFlags == (LoadFlag::LoadSeq | LoadFlag::LoadWarc))
    {
        mLoadItem = (u32)SequenceItems::SequenceAndWarc;
    }
    else if (loadFlags == (LoadFlag::LoadBank | LoadFlag::LoadWarc))
    {
        mLoadItem = (u32)SequenceItems::BankAndWarc;
    }
    else if (loadFlags == LoadFlag::LoadSeq)
    {
        mLoadItem = (u32)SequenceItems::Sequence;
    }
    else if (loadFlags == LoadFlag::LoadBank)
    {
        mLoadItem = (u32)SequenceItems::Bank;
    }
    else if (loadFlags == LoadFlag::LoadWarc)
    {
        mLoadItem = (u32)SequenceItems::Warc;
    }
}

void Group::ItemInfo::setLoadItemsForWaveSoundSet_(u32 loadFlags)
{
    if (loadFlags == LoadFlag::LoadAll)
    {
        mLoadItem = (u32)WaveSoundSetItems::All;
    }
    else if (loadFlags == LoadFlag::LoadWsd)
    {
        mLoadItem = (u32)WaveSoundSetItems::Wsd;
    }
    else if (loadFlags == LoadFlag::LoadWarc)
    {
        mLoadItem = (u32)WaveSoundSetItems::Warc;
    }
}

void Group::ItemInfo::setLoadItemsForBank_(u32 loadFlags)
{
    if (loadFlags == LoadFlag::LoadAll)
    {
        mLoadItem = (u32)BankItems::All;
    }
    else if (loadFlags == LoadFlag::LoadBank)
    {
        mLoadItem = (u32)BankItems::Bank;
    }
    else if (loadFlags == LoadFlag::LoadWarc)
    {
        mLoadItem = (u32)BankItems::Warc;
    }
}

void Group::ItemInfo::setLoadItemsForWaveArchive_(u32 loadFlags)
{
    if (loadFlags == LoadFlag::LoadAll)
    {
        mLoadItem = (u32)WaveArchiveItems::All;
    }
}
