#include <ui/project/ItemViews.h>

#include <ui/widgets/Dialogs.h>
#include <ui/file/File.h>
#include <ui/items/ItemList.h>
#include <ui/items/ItemSelector.h>
#include <ui/widgets/Properties.h>
#include <ui/items/Selection.h>
#include <ui/file/UnsavedChanges.h>
#include <ui/widgets/Widgets.h>

#include <bfsar/model/Bank.h>

// Groups

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

static void GroupCreatePropertiesCallback(bool clear, Item *item, bool *validate)
{
    static u32 sOutputType = 0;

    if (clear)
    {
        sOutputType = 0;
        return;
    }

    if (!item && !validate)
    {
        static const char *sOutputTypes[] = {
            messages::group::cOutputEmbed,
            messages::group::cOutputLink,
            messages::group::cOutputExternal};

        DialogRow(messages::group::cOutputTypeLabel);
        ComboScroll("##OutputType", (s32 *)&sOutputType, sOutputTypes, IM_ARRAYSIZE(sOutputTypes));
    }
    else if (item && !validate)
    {
        Group *group = static_cast<Group *>(item);
        group->setOutputType(static_cast<Group::OutputType>(sOutputType));
    }
}

InstanciateItemCallback CreateGroupFunc(bool clear)
{
    return CreateItemFunc(clear, []() -> Item * { return new Group(); }, &GroupCreatePropertiesCallback);
}

void DrawGroupPropertiesUI()
{
    Group* group = static_cast<Group*>(sSelectedItem);

    {
        static const char* sOutputTypes[] = {
            messages::group::cOutputEmbed,
            messages::group::cOutputLink,
            messages::group::cOutputExternal
        };

        static const char* sOutputTypeDetails[] = {
            messages::group::cEmbedDetail,
            messages::group::cLinkDetail,
            messages::group::cExternalDetail
        };

        if (!BeginPropertyBox("GroupOutput"))
            return;

        const bool isExternal = group->getOutputType() == Group::OutputType::External;
        const bool isLocked = isExternal && group->getExternalFileState() == Group::ExternalFileState::Missing;

        ImGui::BeginDisabled(isLocked);

        s32 outputType = static_cast<s32>(group->getOutputType());
        if (DrawPanelHeaderCombo(messages::group::cOutputTypeLabel, "##OutputType", &outputType, sOutputTypes, sOutputTypeDetails, IM_ARRAYSIZE(sOutputTypes), messages::group::cOutputTypeDetail, isExternal))
        {
            group->setOutputType(static_cast<Group::OutputType>(outputType));
            SetUnsavedChanges(true);
        }

        ImGui::EndDisabled();

        if (isLocked)
            SetDisabledTooltip(messages::group::cOutputTypeLockedTip);

        if (isExternal)
            DrawGroupFileRow(group);

        EndPropertyBox();
    }
}

const char* Group::ItemInfo::sItemIdTypes[4] = {
    //"None",
    messages::item::cTypeSound,
    messages::item::cTypeSoundSet,
    messages::item::cTypeBank,
    messages::item::cTypeWaveArchive
};

static bool IsSameLoadItemKind(Item::ItemType itemType, const Item* oldItem, const Item* item)
{
    switch (itemType)
    {
        case Item::ItemType::Sound:
            return static_cast<const Sound*>(oldItem)->getSoundType() == static_cast<const Sound*>(item)->getSoundType();

        case Item::ItemType::SoundSet:
            return static_cast<const SoundSet*>(oldItem)->getSoundSetType() == static_cast<const SoundSet*>(item)->getSoundSetType();

        default:
            return true;
    }
}

void Group::ItemInfo::drawUI()
{
    bool disabled = mIsDisabled;
    PropertyRow(messages::group::cDisabledLabel);
    if (ImGui::Checkbox("##Disable", &disabled))
    {
        mIsDisabled = disabled;
        SetUnsavedChanges(true);
    }

    if (!BeginPropertyBox("GroupItem"))
        return;

    if (disabled)
    {
        ImGui::BeginDisabled();
    }

    {
        s32 itemRefType = static_cast<s32>(mItemRefType) - 1;
        if (DrawPanelHeaderCombo(messages::group::cItemTypeLabel, "##ItemType", &itemRefType, sItemIdTypes, nullptr, IM_ARRAYSIZE(sItemIdTypes)))
        {
            mItemRefType = static_cast<ItemType>(itemRefType + 1);
            mItemRef.detach();

            mLoadItem = 0; // All
            SetUnsavedChanges(true);
        }
    }

    {
        Item* oldItem = mItemRef.getItem();
        Item* item = oldItem;
        PropertyRow(messages::group::cItemLabel);
        if (ItemSelector("##Item", sBfsar.getItemList(mItemRefType), &item, false))
        {
            mItemRef.attach(item);
            SetUnsavedChanges(true);

            if (oldItem && item && !IsSameLoadItemKind(mItemRefType, oldItem, item))
                mLoadItem = 0; // All
        }
    }

    static sead::FixedSafeString<256> sError;

    {
        u32 itemCount = 0;
        const char** items = getLoadItems(&itemCount);

        bool enable = mItemRef.isAttached() && items;

        u32 oldLoadItem = mLoadItem;
        u32 loadItem = oldLoadItem;
        if (!enable)
        {
            ImGui::BeginDisabled();

            static const char* all = messages::group::cLoadAll;

            itemCount = 1;
            items = &all;
            oldLoadItem = 0;
            loadItem = 0;
        }

        PropertyRow(messages::group::cLoadItemsLabel);
        DetailTooltip(messages::group::cLoadItemsDetail);

        if (ComboScroll("##LoadItems", (s32 *)&loadItem, items, itemCount))
        {
            mLoadItem = loadItem;

            if (validate(sError))
            {
                ImGui::OpenPopup("###LoadItem");
                mLoadItem = oldLoadItem;
            }
            else
            {
                SetUnsavedChanges(true);
            }
        }

        if (!enable)
        {
            ImGui::EndDisabled();
        }
    }

    WarningPopup("###LoadItem", sError.cstr());

    if (disabled)
    {
        ImGui::EndDisabled();
    }

    EndPropertyBox();
}

u32 Group::ItemInfo::getLoadFlag() const
{
    if (!mItemRef.isAttached())
    {
        return LoadFlag::LoadAll;
    }

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
        messages::group::cLoadAll,
        messages::group::cLoadSequenceAndBank,
        messages::group::cLoadSequenceAndWaveArchive,
        messages::group::cLoadBankAndWaveArchive,
        messages::group::cLoadSequence,
        messages::group::cLoadBank,
        messages::group::cLoadWaveArchive
    };

    static const char* sWaveSoundSetLoadItems[] = {
        messages::group::cLoadAll,
        messages::group::cLoadWaveSoundSet,
        messages::group::cLoadWaveArchive
    };

    static const char* sSequenceSetLoadItems[] = {
        messages::group::cLoadAll,
        messages::group::cLoadSequenceSetAndBank,
        messages::group::cLoadSequenceSetAndWaveArchive,
        messages::group::cLoadBankAndWaveArchive,
        messages::group::cLoadSequenceSet,
        messages::group::cLoadBank,
        messages::group::cLoadWaveArchive
    };

    static const char* sBankLoadItems[] = {
        messages::group::cLoadAll,
        messages::group::cLoadBank,
        messages::group::cLoadWaveArchive
    };

    static const char* sWaveArchiveLoadItems[] = {
        messages::group::cLoadAll,
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

            if (!sBfsar.isV3Bfsar() && !(isStream && sound->getStreamSoundInfo().hasPrefetch()))
            {
                error = isStream && sBfsar.isStreamPrefetchBuildable() ? messages::group::cErrorStreamNeedsPrefetch : messages::group::cErrorSoundNotSequence;
                return this;
            }

            return nullptr;
        }

        case ItemType::SoundSet:
        {
            const SoundSet* soundSet = static_cast<const SoundSet*>(mItemRef.getItem());
            if (soundSet->getSoundSetType() == SoundSet::SoundSetType::Seq)
            {
                for (const Item::ListNode* itemNode = sBfsar.getItem(soundSet->getStartId(), sBfsar.getSoundList()); itemNode && itemNode->val()->getId() <= soundSet->getEndId(); itemNode = sBfsar.getSoundList().next(itemNode))
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
