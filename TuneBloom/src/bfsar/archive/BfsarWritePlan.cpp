#include <bfsar/archive/Bfsar.h>

#include <bfsar/archive/BfsarSaveContext.h>

void Bfsar::delegateSoundSetWaveFiles_(BfsarSaveContext& ctx, const SoundSet* soundSet, const WaveArchive* warc)
{
    SEAD_ASSERT(soundSet->getSoundSetType() == SoundSet::SoundSetType::Wave);

    for (const Item::ListNode* itemNode = getItem(soundSet->getStartId(), getSoundList()); itemNode && itemNode->val()->getId() <= soundSet->getEndId(); itemNode = getSoundList().next(itemNode))
    {
        SEAD_ASSERT(itemNode->val()->getItemType() == Item::ItemType::Sound);
        const Sound* sound = static_cast<const Sound*>(itemNode->val());

        if (sound->getSoundType() != Sound::SoundType::Wave)
        {
            continue;
        }

        const Item* waveFileItem = sound->getWaveSoundInfo().getWaveFileRef().getItem();
        SEAD_ASSERT(waveFileItem);
        SEAD_ASSERT(waveFileItem->getItemType() == Item::ItemType::WaveFile);

        const WaveFile* waveFile = static_cast<const WaveFile*>(waveFileItem);

        VectorSet<const WaveFile*>& waveFiles = ctx.warcWaveFiles[warc];

        ctx.warcWaveFilesIndexes[warc].try_emplace(waveFile, waveFiles.size());
        waveFiles.insert(waveFile);
    }
}

static void DelegateBankWaveFiles(BfsarSaveContext& ctx, const Bank* bank, const WaveArchive* warc)
{
    const BankFile* bankFile = static_cast<const BankFile*>(bank->getFileRef().getItem());

    if (!bankFile)
        return;

    for (const Item* instrumentItem : bankFile->getInstrumentList())
    {
        SEAD_ASSERT(instrumentItem->getItemType() == Item::ItemType::BankFileInstrument);
        const BankFile::Instrument* instrument = static_cast<const BankFile::Instrument*>(instrumentItem);

        ForEachVelocityRegion(*instrument, [&](const BankFile::KeyRegion&, const BankFile::VelocityRegion& velocityRegion)
        {
            const Item* waveFileItem = velocityRegion.getWaveFileRef().getItem();
            SEAD_ASSERT(waveFileItem);
            SEAD_ASSERT(waveFileItem->getItemType() == Item::ItemType::WaveFile);

            const WaveFile* waveFile = static_cast<const WaveFile*>(waveFileItem);

            VectorSet<const WaveFile*>& waveFiles = ctx.warcWaveFiles[warc];

            ctx.warcWaveFilesIndexes[warc].try_emplace(waveFile, waveFiles.size());
            waveFiles.insert(waveFile);
        });
    }
}

void Bfsar::delegateWaveFiles_(BfsarSaveContext& ctx, const Item* item, const WaveArchive* warc)
{
    if (item->getItemType() == Item::ItemType::SoundSet)
    {
        delegateSoundSetWaveFiles_(ctx, static_cast<const SoundSet*>(item), warc);
    }
    else if (item->getItemType() == Item::ItemType::Bank)
    {
        DelegateBankWaveFiles(ctx, static_cast<const Bank*>(item), warc);
    }
}

void Bfsar::assignWaveArchives_(BfsarSaveContext& ctx)
{
    for (const Item* item : mSoundSetList)
    {
        SEAD_ASSERT(item->getItemType() == Item::ItemType::SoundSet);
        const SoundSet* soundSet = static_cast<const SoundSet*>(item);

        if (soundSet->getSoundSetType() != SoundSet::SoundSetType::Wave)
        {
            continue;
        }

        if (soundSet->getWaveArchiveType() == WaveArchiveType::Explicit)
        {
            const WaveArchive* warc = static_cast<const WaveArchive*>(soundSet->getWaveArchiveRef().getItem());
            SEAD_ASSERT(warc);

            ctx.waveSoundSetsWarcs[soundSet].insert(nullptr, warc);
        }

        u32 i = 0;
        for (const Item::ListNode* itemNode = getItem(soundSet->getStartId(), getSoundList()); itemNode && itemNode->val()->getId() <= soundSet->getEndId(); itemNode = getSoundList().next(itemNode))
        {
            SEAD_ASSERT(itemNode->val()->getItemType() == Item::ItemType::Sound);
            const Sound* sound = static_cast<const Sound*>(itemNode->val());

            if (sound->getSoundType() != Sound::SoundType::Wave)
            {
                continue;
            }

            SEAD_ASSERT_MSG(!ctx.wsdIndexes.contains(sound), "Wave Sound is included in multiple Wave Sound Sets");

            ctx.wsdIndexes[sound] = i;
            i++;
        }
    }

    for (const Item* item : mBankList)
    {
        SEAD_ASSERT(item->getItemType() == Item::ItemType::Bank);
        const Bank* bank = static_cast<const Bank*>(item);

        if (bank->getWaveArchiveType() != WaveArchiveType::Explicit)
        {
            continue;
        }

        const WaveArchive* warc = static_cast<const WaveArchive*>(bank->getWaveArchiveRef().getItem());
        SEAD_ASSERT(warc);

        ctx.banksWarcs[bank].insert(nullptr, warc);
    }
}

void Bfsar::delegateExplicitWaveFiles_(BfsarSaveContext& ctx)
{
    for (const Item* item : mSoundSetList)
    {
        SEAD_ASSERT(item->getItemType() == Item::ItemType::SoundSet);
        const SoundSet* soundSet = static_cast<const SoundSet*>(item);

        if (soundSet->getSoundSetType() != SoundSet::SoundSetType::Wave || soundSet->getWaveArchiveType() != WaveArchiveType::Explicit)
        {
            continue;
        }

        const WaveArchive* warc = static_cast<const WaveArchive*>(soundSet->getWaveArchiveRef().getItem());
        SEAD_ASSERT(warc);

        delegateWaveFiles_(ctx, soundSet, warc);
    }

    for (const Item* item : mBankList)
    {
        SEAD_ASSERT(item->getItemType() == Item::ItemType::Bank);
        const Bank* bank = static_cast<const Bank*>(item);

        if (bank->getWaveArchiveType() != WaveArchiveType::Explicit)
        {
            continue;
        }

        const WaveArchive* warc = static_cast<const WaveArchive*>(bank->getWaveArchiveRef().getItem());
        SEAD_ASSERT(warc);

        delegateWaveFiles_(ctx, bank, warc);
    }
}

static void AttachWarcToGroup(BfsarSaveContext& ctx, const WaveArchive* warc, u32 loadFlag, const Group* group)
{
    if (loadFlag & Group::ItemInfo::LoadFlag::LoadWarc)
    {
        ctx.itemAttachedGroups.try_emplace(warc).first->second.insert(group);
    }
}

static void AttachBankToGroup(BfsarSaveContext& ctx, const Bank* bank, u32 loadFlag, const Group* group)
{
    if (loadFlag & Group::ItemInfo::LoadFlag::LoadBank)
    {
        ctx.itemAttachedGroups.try_emplace(bank).first->second.insert(group);
    }

    if (bank->getWaveArchiveType() == WaveArchiveType::Explicit)
    {
        const WaveArchive* warc = static_cast<const WaveArchive*>(bank->getWaveArchiveRef().getItem());
        SEAD_ASSERT(warc);

        AttachWarcToGroup(ctx, warc, loadFlag, group);
    }
}

void Bfsar::attachSoundToGroup_(BfsarSaveContext& ctx, const Sound* sound, u32 loadFlag, const Group* group)
{
    if (sound->getSoundType() == Sound::SoundType::Seq)
    {
        if (loadFlag & Group::ItemInfo::LoadFlag::LoadSeq)
        {
            ctx.itemAttachedGroups.try_emplace(sound).first->second.insert(group);

            SEAD_ASSERT(sound->getSequenceSoundInfo().getSequenceFileRef().isAttached());
            const Item* seqFile = sound->getSequenceSoundInfo().getSequenceFileRef().getItem();

            ctx.itemAttachedGroups.try_emplace(seqFile).first->second.insert(group);
        }

        for (u32 i = 0; i < nw::snd::SoundArchive::SEQ_BANK_MAX; i++)
        {
            const ItemReference& bankRef = sound->getSequenceSoundInfo().getBankRef(i);
            if (!bankRef.isAttached())
            {
                continue;
            }

            const Bank* bank = static_cast<const Bank*>(bankRef.getItem());
            AttachBankToGroup(ctx, bank, loadFlag, group);
        }
    }
    else if (sound->getSoundType() == Sound::SoundType::Strm)
    {
        if (loadFlag == Group::ItemInfo::LoadFlag::LoadAll)
        {
            ctx.itemAttachedGroups.try_emplace(sound).first->second.insert(group);
        }
    }
    else
    {
        // TODO: this seems to be fine in v3 but this was set to a throw an error before
        // it's worth checking again later on, but this is it for now

        SEAD_ASSERT(isV3Bfsar());
    }
}

void Bfsar::attachSoundSetToGroup_(BfsarSaveContext& ctx, const SoundSet* soundSet, u32 loadFlag, const Group* group)
{
    if (soundSet->getSoundSetType() == SoundSet::SoundSetType::Wave)
    {
        if (loadFlag & Group::ItemInfo::LoadFlag::LoadWsd)
        {
            ctx.itemAttachedGroups.try_emplace(soundSet).first->second.insert(group);
        }

        if (soundSet->getWaveArchiveType() == WaveArchiveType::Explicit)
        {
            const WaveArchive* warc = static_cast<const WaveArchive*>(soundSet->getWaveArchiveRef().getItem());
            SEAD_ASSERT(warc);

            AttachWarcToGroup(ctx, warc, loadFlag, group);
        }
    }
    else if (soundSet->getSoundSetType() == SoundSet::SoundSetType::Seq)
    {
        for (const Item::ListNode* itemNode = getItem(soundSet->getStartId(), getSoundList()); itemNode && itemNode->val()->getId() <= soundSet->getEndId(); itemNode = getSoundList().next(itemNode))
        {
            SEAD_ASSERT(itemNode->val()->getItemType() == Item::ItemType::Sound);
            const Sound* sound = static_cast<const Sound*>(itemNode->val());

            attachSoundToGroup_(ctx, sound, loadFlag, group);
        }
    }
}

void Bfsar::attachItemsToGroups_(BfsarSaveContext& ctx)
{
    for (const Item* item : mGroupList)
    {
        SEAD_ASSERT(item->getItemType() == Item::ItemType::Group);
        const Group* group = static_cast<const Group*>(item);

        for (const Item* groupItem : group->getItemInfoList())
        {
            SEAD_ASSERT(groupItem->getItemType() == Item::ItemType::GroupItemInfo);
            const Group::ItemInfo* itemInfo = static_cast<const Group::ItemInfo*>(groupItem);

            if (itemInfo->getIsDisabled() || !itemInfo->getItemRef().isAttached())
            {
                continue;
            }

            if (itemInfo->getItemRefType() == Item::ItemType::Sound)
            {
                const Sound* sound = static_cast<const Sound*>(itemInfo->getItemRef().getItem());
                attachSoundToGroup_(ctx, sound, itemInfo->getLoadFlag(), group);
            }
            else if (itemInfo->getItemRefType() == Item::ItemType::SoundSet)
            {
                const SoundSet* soundSet = static_cast<const SoundSet*>(itemInfo->getItemRef().getItem());
                attachSoundSetToGroup_(ctx, soundSet, itemInfo->getLoadFlag(), group);
            }
            else if (itemInfo->getItemRefType() == Item::ItemType::Bank)
            {
                const Bank* bank = static_cast<const Bank*>(itemInfo->getItemRef().getItem());
                AttachBankToGroup(ctx, bank, itemInfo->getLoadFlag(), group);
            }
            else if (itemInfo->getItemRefType() == Item::ItemType::WaveArchive)
            {
                const WaveArchive* warc = static_cast<const WaveArchive*>(itemInfo->getItemRef().getItem());
                AttachWarcToGroup(ctx, warc, itemInfo->getLoadFlag(), group);
            }
        }

        for (const Item* owner : getExtraFileOwners_(group))
            ctx.itemAttachedGroups.try_emplace(owner).first->second.insert(group);
    }
}

static bool IsInList(const Item::List& list, const Item* item)
{
    for (const Item* listItem : list)
    {
        if (listItem == item)
            return true;
    }

    return false;
}

std::vector<const Item*> Bfsar::getExtraFileOwners_(const Group* group) const
{
    std::vector<const Item*> owners;

    if (group->getOutputType() != Group::OutputType::External)
        return owners;

    for (u32 origFileId : group->getOriginalExtraFiles())
    {
        const Item* owner = getOrigFileOwner_(origFileId);

        if (owner && (IsInList(mSequenceFileList, owner) || IsInList(mSoundSetList, owner) || IsInList(mBankList, owner) || IsInList(mWaveArchiveList, owner)))
            owners.push_back(owner);
    }

    return owners;
}

static bool SeqSoundHasSharedWarc(const Sound* sound)
{
    SEAD_ASSERT(sound->getSoundType() == Sound::SoundType::Seq);

    for (u32 i = 0; i < nw::snd::SoundArchive::SEQ_BANK_MAX; i++)
    {
        const ItemReference& bankRef = sound->getSequenceSoundInfo().getBankRef(i);
        if (!bankRef.isAttached())
        {
            continue;
        }

        const Bank* bank = static_cast<const Bank*>(bankRef.getItem());

        if (bank->getWaveArchiveType() == WaveArchiveType::AutomaticShared)
        {
            return true;
        }
    }

    return false;
}

bool Bfsar::groupHasSharedWaveArchive_(const Group* group)
{
    bool groupHasSharedWarc = false;
    if (group->getOutputType() != Group::OutputType::Link)
    {
        for (const Item* groupItem : group->getItemInfoList())
        {
            SEAD_ASSERT(groupItem->getItemType() == Item::ItemType::GroupItemInfo);
            const Group::ItemInfo* itemInfo = static_cast<const Group::ItemInfo*>(groupItem);

            if (itemInfo->getIsDisabled() || !itemInfo->getItemRef().isAttached())
            {
                continue;
            }

            if ((itemInfo->getLoadFlag() & Group::ItemInfo::LoadFlag::LoadWsd) == 0
             && (itemInfo->getLoadFlag() & Group::ItemInfo::LoadFlag::LoadBank) == 0)
            {
                continue;
            }

            if (itemInfo->getItemRefType() == Item::ItemType::Sound)
            {
                const Sound* sound = static_cast<const Sound*>(itemInfo->getItemRef().getItem());

                if (sound->getSoundType() == Sound::SoundType::Seq)
                {
                    if (SeqSoundHasSharedWarc(sound))
                    {
                        SEAD_ASSERT(itemInfo->getLoadFlag() == Group::ItemInfo::LoadFlag::LoadAll || ((itemInfo->getLoadFlag() & Group::ItemInfo::LoadFlag::LoadBank) && (itemInfo->getLoadFlag() & Group::ItemInfo::LoadFlag::LoadWarc)));

                        groupHasSharedWarc = true;
                        break;
                    }
                }
            }
            else if (itemInfo->getItemRefType() == Item::ItemType::SoundSet)
            {
                const SoundSet* soundSet = static_cast<const SoundSet*>(itemInfo->getItemRef().getItem());

                if (soundSet->getSoundSetType() == SoundSet::SoundSetType::Wave)
                {
                    if (soundSet->getWaveArchiveType() == WaveArchiveType::AutomaticShared)
                    {
                        SEAD_ASSERT(itemInfo->getLoadFlag() == Group::ItemInfo::LoadFlag::LoadAll);

                        groupHasSharedWarc = true;
                        break;
                    }
                }
                else if (soundSet->getSoundSetType() == SoundSet::SoundSetType::Seq)
                {
                    for (const Item::ListNode* itemNode = getItem(soundSet->getStartId(), getSoundList()); itemNode && itemNode->val()->getId() <= soundSet->getEndId(); itemNode = getSoundList().next(itemNode))
                    {
                        SEAD_ASSERT(itemNode->val()->getItemType() == Item::ItemType::Sound);
                        const Sound* sound = static_cast<const Sound*>(itemNode->val());

                        if (sound->getSoundType() == Sound::SoundType::Seq)
                        {
                            if (SeqSoundHasSharedWarc(sound))
                            {
                                SEAD_ASSERT(itemInfo->getLoadFlag() == Group::ItemInfo::LoadFlag::LoadAll || ((itemInfo->getLoadFlag() & Group::ItemInfo::LoadFlag::LoadBank) && (itemInfo->getLoadFlag() & Group::ItemInfo::LoadFlag::LoadWarc)));

                                groupHasSharedWarc = true;
                                break;
                            }
                        }
                    }

                    if (groupHasSharedWarc)
                    {
                        break;
                    }
                }
            }
            else if (itemInfo->getItemRefType() == Item::ItemType::Bank)
            {
                const Bank* bank = static_cast<const Bank*>(itemInfo->getItemRef().getItem());

                if (bank->getWaveArchiveType() == WaveArchiveType::AutomaticShared)
                {
                    SEAD_ASSERT(itemInfo->getLoadFlag() == Group::ItemInfo::LoadFlag::LoadAll);

                    groupHasSharedWarc = true;
                    break;
                }
            }
        }
    }

    return groupHasSharedWarc;
}

static constexpr const char* cGeneratedWaveArchiveSuffix       = "_WaveArchive@AutoGenerated";
static constexpr s32         cGeneratedWaveArchiveOwnerNameMax = 229;

WaveArchive* Bfsar::createGeneratedWaveArchive_(BfsarSaveContext& ctx, const Item* owner)
{
    WaveArchive* warc = new WaveArchive();
    warc->mId = mWaveArchiveList.size();

    warc->mEnableName = false;
    warc->mName.format("%.*s%s", cGeneratedWaveArchiveOwnerNameMax, owner->getNameOrNull().cstr(), cGeneratedWaveArchiveSuffix);

    mWaveArchiveList.pushBack(warc);
    ctx.generatedWarcs.push_back(warc);

    return warc;
}

void Bfsar::generateGroupWaveArchives_(BfsarSaveContext& ctx)
{
    for (const Item* item : mGroupList)
    {
        SEAD_ASSERT(item->getItemType() == Item::ItemType::Group);
        const Group* group = static_cast<const Group*>(item);

        if (!groupHasSharedWaveArchive_(group))
            continue;

        const WaveArchive* warc = createGeneratedWaveArchive_(ctx, group);

        ctx.itemAttachedGroups.try_emplace(warc).first->second.insert(group);
        ctx.groupTargetWarcs[group] = warc;
    }
}

static bool NeedsOwnWaveArchive(WaveArchiveType type, const VectorSet<const Group*>& attachGroups)
{
    if (type == WaveArchiveType::AutomaticIndividual)
        return true;

    if (type != WaveArchiveType::AutomaticShared)
        return false;

    if (attachGroups.size() == 0)
        return true;

    for (const Group* group : attachGroups)
    {
        if (group->getOutputType() == Group::OutputType::Link)
            return true;
    }

    return false;
}

void Bfsar::generateOwnerWaveArchives_(BfsarSaveContext& ctx, const Item* owner, WaveArchiveType type, VectorMap<const Group*, const WaveArchive*>& warcs)
{
    const VectorSet<const Group*>& attachGroups = ctx.itemAttachedGroups[owner];

    if (NeedsOwnWaveArchive(type, attachGroups))
    {
        const WaveArchive* warc = createGeneratedWaveArchive_(ctx, owner);

        warcs.insert(nullptr, warc);
        delegateWaveFiles_(ctx, owner, warc);
    }

    if (type != WaveArchiveType::AutomaticShared)
        return;

    for (const Group* group : attachGroups)
    {
        if (group->getOutputType() == Group::OutputType::Link)
            continue;

        const WaveArchive* warc = ctx.groupTargetWarcs[group];

        warcs.insert(group, warc);
        delegateWaveFiles_(ctx, owner, warc);
    }
}

void Bfsar::generateSoundSetWaveArchives_(BfsarSaveContext& ctx)
{
    for (const Item* item : mSoundSetList)
    {
        SEAD_ASSERT(item->getItemType() == Item::ItemType::SoundSet);
        const SoundSet* soundSet = static_cast<const SoundSet*>(item);

        if (soundSet->getSoundSetType() != SoundSet::SoundSetType::Wave)
            continue;

        generateOwnerWaveArchives_(ctx, soundSet, soundSet->getWaveArchiveType(), ctx.waveSoundSetsWarcs[soundSet]);
    }
}

void Bfsar::generateBankWaveArchives_(BfsarSaveContext& ctx)
{
    for (const Item* item : mBankList)
    {
        SEAD_ASSERT(item->getItemType() == Item::ItemType::Bank);
        const Bank* bank = static_cast<const Bank*>(item);

        if (!bank->getFileRef().isAttached())
            continue;

        generateOwnerWaveArchives_(ctx, bank, bank->getWaveArchiveType(), ctx.banksWarcs[bank]);
    }
}
