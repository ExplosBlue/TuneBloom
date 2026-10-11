#include <bfsar/archive/Bfsar.h>

#include <core/Debug.h>

#include <bfsar/io/PatriciaTree.h>
#include <bfsar/files/BfgrpFile.h>
#include <bfsar/archive/BfsarSaveContext.h>
#include <bfsar/files/BfwarFile.h>
#include <bfsar/files/BfwsdFile.h>

bool Bfsar::save_(sead::FileHandle &handle, const std::vector<GroupFileSave>& groupFileSaves, std::vector<SkippedStream> *outSkippedStreams)
{
    LOG_FUNC();
    LOG_U32("mVersion", mVersion);
    LOG_U32("mEndian", (u32)mEndian);
    LOG_U32("mFormat", (u32)mFormat);
    LOG_BOOL("mIncludeStringTable", mIncludeStringTable);
    LOG_FMT("Sounds: %d, SoundSets: %d, Banks: %d, WaveArchives: %d, Groups: %d, Players: %d",
        mSoundList.size(), mSoundSetList.size(), mBankList.size(), mWaveArchiveList.size(), mGroupList.size(), mPlayerList.size());

    BfsarSaveContext ctx(handle);
    ctx.stream.setBinaryEndian(mEndian);
    ctx.writer.openFile(getArchiveMagic(), nw::snd::internal::SoundArchiveFile::BLOCK_SIZE, mVersion);

    assignWaveArchives_(ctx);
    delegateExplicitWaveFiles_(ctx);
    attachItemsToGroups_(ctx);

    generateGroupWaveArchives_(ctx);
    generateSoundSetWaveArchives_(ctx);
    generateBankWaveArchives_(ctx);

    const bool streamFilesLast = areStreamFilesLast_();

    setupPrefetchFiles_(ctx);

    if (!streamFilesLast)
        setupStreamFiles_(ctx);

    setupSequenceFiles_(ctx);
    setupWaveSoundFiles_(ctx);
    setupBankFiles_(ctx);
    setupWaveArchiveFiles_(ctx);
    setupGroupFiles_(ctx);
    setupGeneratedWaveArchiveFiles_(ctx);

    if (streamFilesLast)
        setupStreamFiles_(ctx);

    collectGroupFiles_(ctx);
    collectEmbeddedFiles_(ctx);
    collectExternalGroupFiles_(ctx);
    collectStrings_(ctx);

    writeStringBlock_(ctx);
    writeInfoBlock_(ctx);
    writeFileBlock_(ctx);

    ctx.writer.closeFile();

    const bool written = !ctx.hasWriteFailed() && ctx.writer.isFileComplete() && writeExternalGroupFiles_(ctx, groupFileSaves);

    deleteGeneratedItems_(ctx);

    if (outSkippedStreams)
        *outSkippedStreams = std::move(ctx.skippedStreams);

    if (written)
        logSavedItems_();

    return written;
}

void Bfsar::collectStrings_(BfsarSaveContext& ctx)
{
    auto addListNames = [&](const Item::List& list)
    {
        for (const Item* item : list)
        {
            if (!item->isNameValid())
            {
                continue;
            }

            if (ctx.getStringId(item->getName()) != nw::snd::internal::DEFAULT_STRING_ID)
            {
                SEAD_ASSERT_MSG(false, "name(%s) is duped", item->getName().cstr());
                continue;
            }

            ctx.strings.push_back(item->getName().cstr());
        }
    };

    addListNames(mSoundList);
    addListNames(mSoundSetList);
    addListNames(mBankList);
    addListNames(mWaveArchiveList);
    addListNames(mGroupList);
    addListNames(mPlayerList);
}

void Bfsar::writeStringBlock_(BfsarSaveContext& ctx)
{
    if (mIncludeStringTable)
    {
        ctx.writer.openBlock(nw::snd::internal::ElementType_SoundArchiveFile_StringBlock, "STRG");

        ctx.writer.openReference("StringTable");
        ctx.writer.openReference("PatriciaTree");

        ctx.writer.closeReference("StringTable", nw::snd::internal::ElementType_SoundArchiveFile_StringTable);
        ctx.writer.pushOffsetBase();
        {
            ctx.stream.writeU32(ctx.strings.size());

            for (u32 i = 0; i < ctx.strings.size(); i++)
            {
                ctx.writer.openReference(sead::FormatFixedSafeString<64>("StringEntry%u", i).cstr());

                ctx.stream.writeU32(ctx.strings[i].size() + 1);
            }

            for (u32 i = 0; i < ctx.strings.size(); i++)
            {
                ctx.writer.closeReference(sead::FormatFixedSafeString<64>("StringEntry%u", i).cstr(), nw::snd::internal::ElementType_General_String);

                ctx.stream.writeString(ctx.strings[i].c_str(), ctx.strings[i].size() + 1);
            }

            ctx.writer.align(4);
        }
        ctx.writer.popOffsetBase();

        ctx.writer.closeReference("PatriciaTree", nw::snd::internal::ElementType_SoundArchiveFile_PatriciaTree);

        PatriciaTree tree;

        auto addListTree = [&](const Item::List& list, nw::snd::internal::ItemType type)
        {
            for (const Item* item : list)
            {
                if (item->isNameValid())
                {
                    SEAD_ASSERT(ctx.getStringId(item->getName()) != nw::snd::internal::DEFAULT_STRING_ID);

                    tree.insert(
                        item->getName().cstr(),
                        PatriciaTree::NodeData(ctx.getStringId(item->getName()), nw::snd::internal::Util::GetMaskedItemId(item->getId(), type))
                    );
                }
            }
        };

        addListTree(mSoundList, nw::snd::internal::ItemType_Sound);
        addListTree(mSoundSetList, nw::snd::internal::ItemType_SoundGroup);
        addListTree(mBankList, nw::snd::internal::ItemType_Bank);
        addListTree(mPlayerList, nw::snd::internal::ItemType_Player);
        addListTree(mGroupList, nw::snd::internal::ItemType_Group);
        addListTree(mWaveArchiveList, nw::snd::internal::ItemType_WaveArchive);

        ctx.stream.writeU32(tree.rootIdx);
        ctx.stream.writeU32(tree.nodeTable.size());

        for (u32 i = 0; i < tree.nodeTable.size(); i++)
        {
            const PatriciaTree::Node* node = &tree.nodeTable[i];
            const PatriciaTree::NodeData* data = &node->nodeData;

            ctx.stream.writeU16(node->flags);
            ctx.stream.writeU16(node->flags & PatriciaTree::Node::FLAG_LEAF ? 0xFFFF : node->bit);
            ctx.stream.writeU32(node->getLeftIdx());
            ctx.stream.writeU32(node->getRightIdx());

            ctx.stream.writeU32(data->stringId);
            ctx.stream.writeU32(data->itemId);
        }

        ctx.writer.align(0x20);
        ctx.writer.closeBlock();
    }
    else
    {
        ctx.writer.nullBlock(nw::snd::internal::ElementType_SoundArchiveFile_StringBlock);
    }
}

static const Item* GetFileOwner(const BfsarSaveContext& ctx, const File& file, Item::ItemType ownerType)
{
    const auto it = ctx.filesItems.find(file.id);
    SEAD_ASSERT(it != ctx.filesItems.end());
    SEAD_ASSERT(it->second.size() == 1);

    const Item* owner = it->second.front();
    SEAD_ASSERT(owner->getItemType() == ownerType);

    return owner;
}

template <typename Owner>
static const WaveArchive* GetOwnerWaveArchive(const std::unordered_map<const Owner*, VectorMap<const Group*, const WaveArchive*>>& ownersWarcs, const Owner* owner, const Group* embedGroup)
{
    const auto it = ownersWarcs.find(owner);
    SEAD_ASSERT(it != ownersWarcs.end());
    SEAD_ASSERT(it->second.size() > 0);

    const WaveArchive* warc = FindWaveArchiveForGroup(it->second, embedGroup);
    SEAD_ASSERT(warc);

    return warc;
}

void PrepareInnerFileForWrite(const BfsarSaveContext& ctx, const File& file, const Group* embedGroup)
{
    const InnerFile* innerFile = file.innerFile;

    if (const BfwsdFile* bfwsdFile = sead::DynamicCast<const BfwsdFile>(innerFile))
    {
        const SoundSet* soundSet = static_cast<const SoundSet*>(GetFileOwner(ctx, file, Item::ItemType::SoundSet));
        SEAD_ASSERT(soundSet->getSoundSetType() == SoundSet::SoundSetType::Wave);

        const WaveArchive* warc = GetOwnerWaveArchive(ctx.waveSoundSetsWarcs, soundSet, embedGroup);

        bfwsdFile->prepare(soundSet, warc, ctx.warcWaveFilesIndexes, !embedGroup || soundSet->getWaveArchiveType() != WaveArchiveType::AutomaticShared);
    }
    else if (const BankFile* bankFile = sead::DynamicCast<const BankFile>(innerFile))
    {
        const Bank* bank = static_cast<const Bank*>(GetFileOwner(ctx, file, Item::ItemType::Bank));
        const WaveArchive* warc = GetOwnerWaveArchive(ctx.banksWarcs, bank, embedGroup);

        bankFile->prepare(bank, warc, ctx.warcWaveFilesIndexes, !embedGroup || bank->getWaveArchiveType() != WaveArchiveType::AutomaticShared);
    }
    else if (const BfwarFile* bfwarFile = sead::DynamicCast<const BfwarFile>(innerFile))
    {
        bool isGroupWarc = false;

        if (embedGroup)
        {
            const auto it = ctx.groupTargetWarcs.find(embedGroup);

            isGroupWarc = it != ctx.groupTargetWarcs.end() && it->second == GetFileOwner(ctx, file, Item::ItemType::WaveArchive);
        }

        bfwarFile->prepare(!isGroupWarc);
    }
    else if (const BfgrpFile* bfgrpFile = sead::DynamicCast<const BfgrpFile>(innerFile))
    {
        SEAD_ASSERT(!embedGroup);

        bfgrpFile->prepare(static_cast<const Group*>(GetFileOwner(ctx, file, Item::ItemType::Group)), ctx);
    }
}

void Bfsar::writeFileBlock_(BfsarSaveContext& ctx)
{
    ctx.writer.openBlock(nw::snd::internal::ElementType_SoundArchiveFile_FileBlock, "FILE");

    u32 fileBlockPos = ctx.writer.getPosition() - sizeof(nw::ut::BinaryBlockHeader);
    ctx.writer.align(0x20);

    u32 lastFileId = nw::snd::SoundArchive::INVALID_ID;
    for (auto it = ctx.files.rbegin(); it != ctx.files.rend(); ++it)
    {
        if (getOrigIncludeInBfsar_(ctx, *it) && !ctx.nullDataFileIds.count(it->id))
        {
            lastFileId = it->id;
            break;
        }
    }

    struct GroupFileInfo
    {
        u32 startPos;
        std::unordered_map<u32, BfgrpFile::EmbeddedFileInfo> embeddedInfos;
    };

    std::unordered_map<u32, GroupFileInfo> groupFileInfos;

    // First pass: write all non-embedded files (including group files)
    for (const File &file : ctx.files)
    {
        if (file.external || !getOrigIncludeInBfsar_(ctx, file) || !file.innerFile)
            continue;

        if (ctx.embeddedFileIds.count(file.id) || ctx.nullDataFileIds.count(file.id))
            continue;

        ctx.writer.align(0x20);

        const InnerFile *innerFile = file.innerFile;
        const BfgrpFile *bfgrpFile = sead::DynamicCast<const BfgrpFile>(innerFile);

        PrepareInnerFileForWrite(ctx, file, nullptr);

        u32 startPos = ctx.writer.getPosition();
        u32 size = innerFile->write(&ctx.handle, &ctx.stream, mEndian, file.id == lastFileId);

        ctx.writer.closeSizedReference(
            sead::FormatFixedSafeString<32>("File%u", file.id),
            bfgrpFile ? 0 : nw::snd::internal::ElementType_General_ByteStream,
            startPos - fileBlockPos - sizeof(nw::ut::BinaryBlockHeader),
            size);

        if (bfgrpFile)
        {
            GroupFileInfo &info = groupFileInfos[file.id];
            info.startPos = startPos;
            info.embeddedInfos = bfgrpFile->getEmbeddedFileInfos();
        }

        innerFile->clearWriteInfo();
    }

    // Second pass: write embedded files (just close sized refs, no data write)
    for (const File &file : ctx.files)
    {
        if (!ctx.embeddedFileIds.count(file.id))
            continue;

        if (file.external || !getOrigIncludeInBfsar_(ctx, file) || !file.innerFile)
            continue;

        auto it = ctx.embeddedFileToGroupFile.find(file.id);
        SEAD_ASSERT(it != ctx.embeddedFileToGroupFile.end());
        u32 groupFileId = it->second;

        auto groupIt = groupFileInfos.find(groupFileId);
        SEAD_ASSERT(groupIt != groupFileInfos.end());
        const GroupFileInfo &groupInfo = groupIt->second;

        auto embeddedIt = groupInfo.embeddedInfos.find(file.id);
        SEAD_ASSERT(embeddedIt != groupInfo.embeddedInfos.end());
        const BfgrpFile::EmbeddedFileInfo &embeddedInfo = embeddedIt->second;

        ctx.writer.closeSizedReference(
            sead::FormatFixedSafeString<32>("File%u", file.id),
            nw::snd::internal::ElementType_General_ByteStream,
            (groupInfo.startPos - fileBlockPos - sizeof(nw::ut::BinaryBlockHeader)) + embeddedInfo.offset,
            embeddedInfo.size);

        file.innerFile->clearWriteInfo();
    }

    ctx.writer.closeBlock();
}

void Bfsar::deleteGeneratedItems_(BfsarSaveContext& ctx)
{
    for (InnerFile *innerFile : ctx.generatedInnerFiles)
    {
        delete innerFile;
    }

    for (const WaveArchive *warc : ctx.generatedWarcs)
    {
        delete warc;
    }

    updateList(mWaveArchiveList);
}

void Bfsar::logSavedItems_() const
{
    LOG_FMT("=== Archive saved (%.4s) ===", getArchiveMagic());
    {
        auto it = mGroupList.robustBegin();
        while (it != mGroupList.robustEnd()) { LOG_FMT("  Group[%d]: \"%s\"", it->val()->getId(), it->val()->getNameOrNull().cstr()); ++it; }
    }
    {
        auto it = mPlayerList.robustBegin();
        while (it != mPlayerList.robustEnd()) { LOG_FMT("  Player[%d]: \"%s\"", it->val()->getId(), it->val()->getNameOrNull().cstr()); ++it; }
    }
    {
        auto it = mBankList.robustBegin();
        while (it != mBankList.robustEnd()) { LOG_FMT("  Bank[%d]: \"%s\"", it->val()->getId(), it->val()->getNameOrNull().cstr()); ++it; }
    }
    {
        auto it = mBankFileList.robustBegin();
        while (it != mBankFileList.robustEnd()) { LOG_FMT("  BankFile[%d]: \"%s\"", it->val()->getId(), it->val()->getNameOrNull().cstr()); ++it; }
    }
    {
        auto it = mSoundList.robustBegin();
        while (it != mSoundList.robustEnd()) { const Sound* s = static_cast<const Sound*>(it->val()); LOG_FMT("  Sound[%d]: \"%s\" type=%d", s->getId(), s->getNameOrNull().cstr(), (int)s->getSoundType()); ++it; }
    }
    {
        auto it = mSoundSetList.robustBegin();
        while (it != mSoundSetList.robustEnd()) { LOG_FMT("  SoundSet[%d]: \"%s\"", it->val()->getId(), it->val()->getNameOrNull().cstr()); ++it; }
    }
    {
        auto it = mSequenceFileList.robustBegin();
        while (it != mSequenceFileList.robustEnd()) { LOG_FMT("  SeqFile[%d]: \"%s\"", it->val()->getId(), it->val()->getNameOrNull().cstr()); ++it; }
    }
    {
        auto it = mWaveArchiveList.robustBegin();
        while (it != mWaveArchiveList.robustEnd()) { LOG_FMT("  WaveArchive[%d]: \"%s\"", it->val()->getId(), it->val()->getNameOrNull().cstr()); ++it; }
    }
    {
        auto it = mWaveFileList.robustBegin();
        while (it != mWaveFileList.robustEnd()) { LOG_FMT("  WaveFile[%d]: \"%s\"", it->val()->getId(), it->val()->getNameOrNull().cstr()); ++it; }
    }
}
