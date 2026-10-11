#include <bfsar/archive/Bfsar.h>

#include <bfsar/files/BfgrpFile.h>
#include <bfsar/archive/BfsarSaveContext.h>
#include <bfsar/files/BfstpFile.h>
#include <bfsar/files/BfwarFile.h>
#include <bfsar/files/BfwsdFile.h>

static bool ItemInGroupOfType(BfsarSaveContext& ctx, const Item* item, Group::OutputType outputType)
{
    for (const Group* group : ctx.itemAttachedGroups[item])
    {
        if (group->getOutputType() == outputType)
            return true;
    }

    return false;
}

static bool ItemInEmbedGroup(BfsarSaveContext& ctx, const Item* item)
{
    return ItemInGroupOfType(ctx, item, Group::OutputType::Embed);
}

static bool ItemInLinkGroup(BfsarSaveContext& ctx, const Item* item)
{
    return ItemInGroupOfType(ctx, item, Group::OutputType::Link);
}

void Bfsar::setupPrefetchFiles_(BfsarSaveContext& ctx)
{
    if (!hasStreamPrefetchFileId())
        return;

    std::vector<StreamSaveJob> jobs;
    std::vector<SkippedStream> skippedStreams;
    bool isPlanned = false;

    //? BFSTP
    for (const Item* item : mSoundList)
    {
        SEAD_ASSERT(item->getItemType() == Item::ItemType::Sound);
        const Sound* sound = static_cast<const Sound*>(item);

        if (sound->getSoundType() != Sound::SoundType::Strm || !sound->getStreamSoundInfo().hasPrefetch())
            continue;

        if (!isPlanned)
        {
            planStreamSaves(jobs, skippedStreams);
            isPlanned = true;
        }

        std::vector<u8> bytes;

        if (!buildStreamPrefetch_(sound, jobs, &bytes))
        {
            ctx.skippedStreams.push_back({sound, messages::stream::cPrefetchNotSaved});
            continue;
        }

        InnerFile* innerFile = new BfstpFile(std::move(bytes), mEndian, mFormat);
        ctx.generatedInnerFiles.push_back(innerFile);

        File file(ctx.files.size(), innerFile, true);
        file.isPrefetch = true;

        ctx.prefetchFileIds.try_emplace(sound, file.id);
        ctx.files.push_back(file);

        ctx.filesItems[file.id].insert(sound);
    }
}

void Bfsar::setupStreamFiles_(BfsarSaveContext& ctx)
{
    std::unordered_map<std::string, File> strmFiles;

    auto addStrmSoundFile = [&](const Sound* sound)
    {
        SEAD_ASSERT(sound->getSoundType() == Sound::SoundType::Strm);

        const char* path = sound->getStreamSoundInfo().getPath().cstr();

        const auto& it = strmFiles.find(path);
        if (it == strmFiles.end())
        {
            File file(ctx.files.size(), path);

            strmFiles.try_emplace(path, file);
            ctx.itemFileIds.try_emplace(sound, file);
            ctx.files.push_back(file);

            ctx.filesItems[file.id].insert(sound);
        }
        else
        {
            const File& file = it->second;

            ctx.itemFileIds.try_emplace(sound, file);

            ctx.filesItems[file.id].insert(sound);
        }
    };

    //? Stream files are placed first
    for (const Item* item : mSoundList)
    {
        SEAD_ASSERT(item->getItemType() == Item::ItemType::Sound);
        const Sound* sound = static_cast<const Sound*>(item);

        switch (sound->getSoundType())
        {
            case Sound::SoundType::Strm:
                addStrmSoundFile(sound);
                break;
            default:
                break;
        }
    }
}

void Bfsar::setupSequenceFiles_(BfsarSaveContext& ctx)
{
    std::unordered_map<const SequenceFile*, File> bfseqFiles;

    //? BFSEQ
    for (const Item* item : mSoundList)
    {
        SEAD_ASSERT(item->getItemType() == Item::ItemType::Sound);
        const Sound* sound = static_cast<const Sound*>(item);

        switch (sound->getSoundType())
        {
            case Sound::SoundType::Seq:
            {
                const ItemReference& seqFileRef = sound->getSequenceSoundInfo().getSequenceFileRef();
                const Item* seqFileItem = seqFileRef.getItem();
                SEAD_ASSERT(seqFileItem);
                SEAD_ASSERT(seqFileItem->getItemType() == Item::ItemType::SequenceFile);

                const SequenceFile* seqFile = static_cast<const SequenceFile*>(seqFileItem);

                const auto& it = bfseqFiles.find(seqFile);
                if (it == bfseqFiles.end())
                {
                    seqFile->setFormat(mFormat);
                    File file(ctx.files.size(), seqFile, true);
                    file.origId = getItemOrigFileId_(seqFile);

                    bfseqFiles.try_emplace(seqFile, file);
                    ctx.itemFileIds.try_emplace(sound, file);
                    ctx.files.push_back(file);

                    ctx.filesItems[file.id].insert(sound);
                }
                else
                {
                    const File& file = it->second;

                    ctx.itemFileIds.try_emplace(sound, file);

                    ctx.filesItems[file.id].insert(sound);
                }

                break;
            }
            default:
                break;
        }
    }
}

static const WaveArchive* GetOwnWaveArchive(BfsarSaveContext& ctx, const VectorMap<const Group*, const WaveArchive*>* warcMap, bool isOnlyWaveArchive)
{
    SEAD_ASSERT(warcMap);
    SEAD_ASSERT(isOnlyWaveArchive ? warcMap->size() == 1 : warcMap->size() > 0);

    const WaveArchive* warc = FindWaveArchiveForGroup(*warcMap, nullptr);
    SEAD_ASSERT(warc);
    SEAD_ASSERT(ctx.itemAttachedGroups[warc].size() == 0);

    return warc;
}

static bool PlaceOwnerFile(BfsarSaveContext& ctx, const Item* owner, WaveArchiveType type, const VectorMap<const Group*, const WaveArchive*>* warcMap)
{
    const bool isInGroup = ctx.itemAttachedGroups[owner].size() != 0;

    switch (type)
    {
        case WaveArchiveType::AutomaticShared:
        {
            const bool includeInBfsar = !isInGroup || ItemInLinkGroup(ctx, owner);

            if (includeInBfsar)
                ctx.includeGeneratedWarcInBfsar[GetOwnWaveArchive(ctx, warcMap, !isInGroup)] = true;

            return includeInBfsar;
        }

        case WaveArchiveType::AutomaticIndividual:
        {
            const bool includeInBfsar = !isInGroup || !ItemInEmbedGroup(ctx, owner);

            ctx.includeGeneratedWarcInBfsar[GetOwnWaveArchive(ctx, warcMap, true)] = includeInBfsar;

            return includeInBfsar;
        }

        case WaveArchiveType::Explicit:
            return !isInGroup || !ItemInEmbedGroup(ctx, owner);

        default:
            SEAD_ASSERT_MSG(false, "Invalid WaveArchiveType");
            return !isInGroup;
    }
}

template <typename Owner>
static const VectorMap<const Group*, const WaveArchive*>* FindWaveArchiveMap(const std::unordered_map<const Owner*, VectorMap<const Group*, const WaveArchive*>>& map, const Owner* owner)
{
    const auto it = map.find(owner);

    return it != map.end() ? &it->second : nullptr;
}

void Bfsar::setupWaveSoundFiles_(BfsarSaveContext& ctx)
{
    //? BFWSD
    for (const Item* item : mSoundSetList)
    {
        SEAD_ASSERT(item->getItemType() == Item::ItemType::SoundSet);
        const SoundSet* soundSet = static_cast<const SoundSet*>(item);

        if (soundSet->getSoundSetType() != SoundSet::SoundSetType::Wave)
        {
            continue;
        }

        const bool includeInBfsar = PlaceOwnerFile(ctx, soundSet, soundSet->getWaveArchiveType(), FindWaveArchiveMap(ctx.waveSoundSetsWarcs, soundSet));

        InnerFile* innerFile = new BfwsdFile(mEndian, getVersionForBfwsd(), mFormat);
        ctx.generatedInnerFiles.push_back(innerFile);

        File file(ctx.files.size(), innerFile, includeInBfsar);
        file.origId = getItemOrigFileId_(soundSet);

        ctx.itemFileIds.try_emplace(soundSet, file);
        ctx.files.push_back(file);

        ctx.filesItems[file.id].insert(soundSet);

        for (const Item::ListNode* itemNode = getItem(soundSet->getStartId(), getSoundList()); itemNode && itemNode->val()->getId() <= soundSet->getEndId(); itemNode = getSoundList().next(itemNode))
        {
            SEAD_ASSERT(itemNode->val()->getItemType() == Item::ItemType::Sound);
            const Sound* sound = static_cast<const Sound*>(itemNode->val());

            if (sound->getSoundType() != Sound::SoundType::Wave)
            {
                continue;
            }

            SEAD_ASSERT_MSG(!ctx.itemFileIds.contains(sound), "Wave Sound is included in multiple Wave Sound Sets");

            ctx.itemFileIds.try_emplace(sound, file);
        }
    }
}

void Bfsar::setupBankFiles_(BfsarSaveContext& ctx)
{
    //? BFBNK
    for (const Item* item : mBankList)
    {
        SEAD_ASSERT(item->getItemType() == Item::ItemType::Bank);
        const Bank* bank = static_cast<const Bank*>(item);

        const Item* bankFileItem = bank->getFileRef().getItem();

        if (!bankFileItem)
        {
            File nullFile(ctx.files.size(), nullptr, false);
            nullFile.origId = getItemOrigFileId_(bank);

            ctx.itemFileIds.try_emplace(bank, nullFile);
            ctx.files.push_back(nullFile);

            ctx.filesItems[nullFile.id].insert(bank);

            continue;
        }

        SEAD_ASSERT(bankFileItem->getItemType() == Item::ItemType::BankFile);

        const BankFile* bankFile = static_cast<const BankFile*>(bankFileItem);

        const bool includeInBfsar = PlaceOwnerFile(ctx, bank, bank->getWaveArchiveType(), FindWaveArchiveMap(ctx.banksWarcs, bank));

        bankFile->setup(mEndian, mFormat, getVersionForBfbnk());

        File file(ctx.files.size(), bankFile, includeInBfsar);
        file.origId = getItemOrigFileId_(bank);

        ctx.itemFileIds.try_emplace(bank, file);
        ctx.files.push_back(file);

        ctx.filesItems[file.id].insert(bank);
    }
}

void Bfsar::setupWaveArchiveFiles_(BfsarSaveContext& ctx)
{
    //? BFWAR
    for (const Item* item : mWaveArchiveList)
    {
        SEAD_ASSERT(item->getItemType() == Item::ItemType::WaveArchive);
        const WaveArchive* warc = static_cast<const WaveArchive*>(item);

        if (ctx.isGeneratedWaveArchive(warc))
        {
            continue;
        }

        InnerFile* innerFile = new BfwarFile(mEndian, getVersionForBfwar(), ctx.warcWaveFiles[warc], mFormat);
        ctx.generatedInnerFiles.push_back(innerFile);

        File file(ctx.files.size(), innerFile, !ItemInEmbedGroup(ctx, warc));
        file.origId = getItemOrigFileId_(warc);

        ctx.itemFileIds.try_emplace(warc, file);
        ctx.files.push_back(file);

        ctx.filesItems[file.id].insert(warc);
    }
}

bool Bfsar::hasGroupFileSlot_(const Group* group) const
{
    if (group->getExternalFileState() != Group::ExternalFileState::None)
        return group->hasFileSlot();

    for (const Item* item : mGroupList)
    {
        const Group* other = static_cast<const Group*>(item);

        if (other->getOutputType() == Group::OutputType::External && other->getExternalFileState() != Group::ExternalFileState::None)
            return other->hasFileSlot();
    }

    return mFormat == ArchiveFormat::BFSAR && isVersionOrLater(2, 2, 0);
}

void Bfsar::setupGroupFiles_(BfsarSaveContext& ctx)
{
    //? BFGRP
    for (const Item* item : mGroupList)
    {
        SEAD_ASSERT(item->getItemType() == Item::ItemType::Group);
        const Group* group = static_cast<const Group*>(item);

        const bool isExternal = group->getOutputType() == Group::OutputType::External;

        BfgrpFile* innerFile = new BfgrpFile(mEndian, getVersionForBfgrp(), mFormat, areGroupItemsSortedById_());
        ctx.generatedInnerFiles.push_back(innerFile);

        if (isExternal)
        {
            ctx.externalGroupFiles.emplace_back(group, innerFile);

            if (!hasGroupFileSlot_(group))
                continue;
        }

        File file(ctx.files.size(), innerFile, true);
        file.origId = getItemOrigFileId_(group);

        ctx.itemFileIds.try_emplace(group, file);
        ctx.files.push_back(file);

        ctx.filesItems[file.id].insert(group);

        if (isExternal)
        {
            ctx.externalGroupFileIds.insert(file.id);
            ctx.nullDataFileIds.insert(file.id);
        }
    }
}

void Bfsar::setupGeneratedWaveArchiveFiles_(BfsarSaveContext& ctx)
{
    //? Other BFWAR
    for (const WaveArchive* warc : ctx.generatedWarcs)
    {
        const VectorSet<const Group*>& attachGroups = ctx.itemAttachedGroups[warc];
        bool includeInBfsar = false;
        if (attachGroups.size() == 0)
        {
            includeInBfsar = ctx.includeGeneratedWarcInBfsar[warc];
        }

        InnerFile* innerFile = new BfwarFile(mEndian, getVersionForBfwar(), ctx.warcWaveFiles[warc], mFormat);
        ctx.generatedInnerFiles.push_back(innerFile);

        File file(ctx.files.size(), innerFile, includeInBfsar);

        ctx.itemFileIds.try_emplace(warc, file);
        ctx.files.push_back(file);

        ctx.filesItems[file.id].insert(warc);
    }
}

static void AttachWarcFileToGroup(BfsarSaveContext& ctx, const WaveArchive* warc, u32 loadFlag, const Group* group)
{
    if (loadFlag & Group::ItemInfo::LoadFlag::LoadWarc)
    {
        const auto& it = ctx.itemFileIds.find(warc);
        SEAD_ASSERT(it != ctx.itemFileIds.end());

        ctx.groupItemFiles[group].insert(it->second.id);
    }
}

static void AttachBankFileToGroup(BfsarSaveContext& ctx, const Bank* bank, u32 loadFlag, const Group* group)
{
    if ((loadFlag & Group::ItemInfo::LoadFlag::LoadBank) && bank->getFileRef().isAttached())
    {
        const auto& it = ctx.itemFileIds.find(bank);
        SEAD_ASSERT(it != ctx.itemFileIds.end());

        ctx.groupItemFiles[group].insert(it->second.id);
    }

    const WaveArchive* warc = nullptr;
    if (bank->getWaveArchiveType() == WaveArchiveType::Explicit)
    {
        warc = static_cast<const WaveArchive*>(bank->getWaveArchiveRef().getItem());
    }
    else if (bank->getFileRef().isAttached())
    {
        const VectorMap<const Group*, const WaveArchive*>& warcMap = ctx.banksWarcs[bank];
        SEAD_ASSERT(warcMap.size() > 0);

        warc = FindWaveArchiveForGroup(warcMap, group);
    }

    if (warc)
    {
        AttachWarcFileToGroup(ctx, warc, loadFlag, group);
    }
}

void Bfsar::attachSoundFileToGroup_(BfsarSaveContext& ctx, const Sound* sound, u32 loadFlag, const Group* group)
{
    if (sound->getSoundType() == Sound::SoundType::Seq)
    {
        if (loadFlag & Group::ItemInfo::LoadFlag::LoadSeq)
        {
            const auto& it = ctx.itemFileIds.find(sound);
            SEAD_ASSERT(it != ctx.itemFileIds.end());

            ctx.groupItemFiles[group].insert(it->second.id);
        }

        for (u32 i = 0; i < nw::snd::SoundArchive::SEQ_BANK_MAX; i++)
        {
            const ItemReference& bankRef = sound->getSequenceSoundInfo().getBankRef(i);
            if (!bankRef.isAttached())
            {
                continue;
            }

            const Bank* bank = static_cast<const Bank*>(bankRef.getItem());
            AttachBankFileToGroup(ctx, bank, loadFlag, group);
        }
    }
    else if (sound->getSoundType() == Sound::SoundType::Strm)
    {
        const u32 prefetchFileId = ctx.getPrefetchFileId(sound);

        if (prefetchFileId != nw::snd::SoundArchive::INVALID_ID)
        {
            ctx.groupItemFiles[group].insert(prefetchFileId);
        }
        else if (isV3Bfsar() && loadFlag == Group::ItemInfo::LoadFlag::LoadAll)
        {
            const auto& it = ctx.itemFileIds.find(sound);
            SEAD_ASSERT(it != ctx.itemFileIds.end());

            ctx.groupItemFiles[group].insert(it->second.id);
        }
    }
    else
    {
        // TODO: this seems to be fine in v3 but this was set to a throw an error before
        // it's worth checking again later on, but this is it for now

        SEAD_ASSERT(isV3Bfsar());
    }
}

void Bfsar::attachSoundSetFileToGroup_(BfsarSaveContext& ctx, const SoundSet* soundSet, u32 loadFlag, const Group* group)
{
    if (soundSet->getSoundSetType() == SoundSet::SoundSetType::Wave)
    {
        if (loadFlag & Group::ItemInfo::LoadFlag::LoadWsd)
        {
            const auto& it = ctx.itemFileIds.find(soundSet);
            SEAD_ASSERT(it != ctx.itemFileIds.end());

            ctx.groupItemFiles[group].insert(it->second.id);
        }

        const WaveArchive* warc = nullptr;
        if (soundSet->getWaveArchiveType() == WaveArchiveType::Explicit)
        {
            warc = static_cast<const WaveArchive*>(soundSet->getWaveArchiveRef().getItem());
        }
        else
        {
            const VectorMap<const Group*, const WaveArchive*>& warcMap = ctx.waveSoundSetsWarcs[soundSet];
            SEAD_ASSERT(warcMap.size() > 0);

            warc = FindWaveArchiveForGroup(warcMap, group);
        }

        if (warc)
        {
            AttachWarcFileToGroup(ctx, warc, loadFlag, group);
        }
    }
    else if (soundSet->getSoundSetType() == SoundSet::SoundSetType::Seq)
    {
        for (const Item::ListNode* itemNode = getItem(soundSet->getStartId(), getSoundList()); itemNode && itemNode->val()->getId() <= soundSet->getEndId(); itemNode = getSoundList().next(itemNode))
        {
            SEAD_ASSERT(itemNode->val()->getItemType() == Item::ItemType::Sound);
            const Sound* sound = static_cast<const Sound*>(itemNode->val());

            attachSoundFileToGroup_(ctx, sound, loadFlag, group);
        }
    }
    else
    {
        SEAD_ASSERT(false);
    }
}

void Bfsar::collectGroupFiles_(BfsarSaveContext& ctx)
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
                attachSoundFileToGroup_(ctx, sound, itemInfo->getLoadFlag(), group);
            }
            else if (itemInfo->getItemRefType() == Item::ItemType::SoundSet)
            {
                const SoundSet* soundSet = static_cast<const SoundSet*>(itemInfo->getItemRef().getItem());
                attachSoundSetFileToGroup_(ctx, soundSet, itemInfo->getLoadFlag(), group);
            }
            else if (itemInfo->getItemRefType() == Item::ItemType::Bank)
            {
                const Bank* bank = static_cast<const Bank*>(itemInfo->getItemRef().getItem());
                AttachBankFileToGroup(ctx, bank, itemInfo->getLoadFlag(), group);
            }
            else if (itemInfo->getItemRefType() == Item::ItemType::WaveArchive)
            {
                const WaveArchive* warc = static_cast<const WaveArchive*>(itemInfo->getItemRef().getItem());
                AttachWarcFileToGroup(ctx, warc, itemInfo->getLoadFlag(), group);
            }
        }

        const auto& it = ctx.groupTargetWarcs.find(group);
        if (it != ctx.groupTargetWarcs.end())
        {
            const WaveArchive* warc = it->second;
            SEAD_ASSERT(warc);

            AttachWarcFileToGroup(ctx, warc, Group::ItemInfo::LoadFlag::LoadAll, group);
        }

        if (group->getOutputType() == Group::OutputType::External)
            attachExtraFilesToGroup_(ctx, group);

        restoreGroupFileOrder_(ctx, group);
    }
}

void Bfsar::attachExtraFilesToGroup_(BfsarSaveContext& ctx, const Group* group)
{
    for (u32 origFileId : group->getOriginalExtraFiles())
    {
        for (const File& file : ctx.files)
        {
            if (file.origId == origFileId && file.innerFile)
                ctx.groupItemFiles[group].insert(file.id);
        }
    }
}

void Bfsar::restoreGroupFileOrder_(BfsarSaveContext& ctx, const Group* group)
{
    const std::vector<u32>& originalOrder = group->getOriginalFileOrder();
    const auto groupFiles = ctx.groupItemFiles.find(group);

    if (originalOrder.empty() || groupFiles == ctx.groupItemFiles.end() || groupFiles->second.size() != originalOrder.size())
        return;

    std::unordered_map<u32, u32> originalPlaces;

    for (u32 i = 0; i < originalOrder.size(); i++)
        originalPlaces[originalOrder[i]] = i;

    std::vector<u32> orderedFileIds(originalOrder.size(), nw::snd::SoundArchive::INVALID_ID);
    std::vector<u32> generatedFileIds;

    for (u32 fileId : groupFiles->second)
    {
        const auto place = originalPlaces.find(ctx.files[fileId].origId);

        if (place != originalPlaces.end())
            orderedFileIds[place->second] = fileId;
        else
            generatedFileIds.push_back(fileId);
    }

    u32 generatedIdx = 0;

    for (u32& fileId : orderedFileIds)
    {
        if (fileId != nw::snd::SoundArchive::INVALID_ID)
            continue;

        if (generatedIdx == generatedFileIds.size())
            return;

        fileId = generatedFileIds[generatedIdx++];
    }

    VectorSet<u32> restored;

    for (u32 fileId : orderedFileIds)
        restored.insert(fileId);

    groupFiles->second = restored;
}

void Bfsar::collectEmbeddedFiles_(BfsarSaveContext& ctx)
{
    for (const Item* groupItem : mGroupList)
    {
        const Group* group = static_cast<const Group*>(groupItem);
        const auto files = ctx.groupItemFiles.find(group);

        if (files == ctx.groupItemFiles.end() || group->getOutputType() != Group::OutputType::Embed)
            continue;

        const auto &it = ctx.itemFileIds.find(groupItem);
        SEAD_ASSERT(it != ctx.itemFileIds.end());

        u32 groupFileId = it->second.id;

        for (u32 fileId : files->second)
        {
            ctx.embeddedFileIds.insert(fileId);
            ctx.embeddedFileToGroupFile.try_emplace(fileId, groupFileId);
        }
    }
}

static bool HasGroupOfType(const VectorSet<const Group*>& groups, Group::OutputType outputType)
{
    for (const Group* group : groups)
    {
        if (group->getOutputType() == outputType)
            return true;
    }

    return false;
}

static bool IsOriginalExternalGroupSet(const std::vector<u32>& groupItemIds, const VectorSet<const Group*>& groups)
{
    std::unordered_set<u32> originalIds(groupItemIds.begin(), groupItemIds.end());

    if (originalIds.size() != groups.size())
        return false;

    for (const Group* group : groups)
    {
        if (group->getExternalFileState() == Group::ExternalFileState::None)
            return false;

        if (!originalIds.contains(nw::snd::internal::Util::GetMaskedItemId(group->getId(), nw::snd::internal::ItemType_Group)))
            return false;
    }

    return true;
}

void Bfsar::collectExternalGroupFiles_(BfsarSaveContext& ctx)
{
    for (const Item* groupItem : mGroupList)
    {
        const Group* group = static_cast<const Group*>(groupItem);

        for (u32 fileId : ctx.getGroupItemFiles(group))
            ctx.fileGroups[fileId].insert(group);
    }

    for (const File& file : ctx.files)
    {
        const auto groups = ctx.fileGroups.find(file.id);

        if (groups == ctx.fileGroups.end() || file.external || !HasGroupOfType(groups->second, Group::OutputType::External))
            continue;

        ctx.externalGroupFileIds.insert(file.id);

        if (ctx.embeddedFileIds.count(file.id) || HasGroupOfType(groups->second, Group::OutputType::Link))
            continue;

        const bool hadData = file.includeInBfsar && file.origId < mFileOriginalHasData.size() && mFileOriginalHasData[file.origId];

        if (!hadData || !IsOriginalExternalGroupSet(mFileAttachedGroups[file.origId], groups->second))
            ctx.nullDataFileIds.insert(file.id);
    }
}

bool Bfsar::getOrigIncludeInBfsar_(const BfsarSaveContext& ctx, const File& file) const
{
    if (file.isPrefetch || ctx.externalGroupFileIds.count(file.id))
        return true;

    if (file.origId < mFileOriginalIncludeInBfsar.size())
        return mFileOriginalIncludeInBfsar[file.origId];

    if (!isV3Bfsar())
        return (file.origId == nw::snd::SoundArchive::INVALID_ID && file.id < mFileOriginalIncludeInBfsar.size()) ? mFileOriginalIncludeInBfsar[file.id] : file.includeInBfsar;

    return file.includeInBfsar || ctx.embeddedFileIds.count(file.id) != 0;
}
