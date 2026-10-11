#include <bfsar/files/BfgrpFile.h>

#include <core/Debug.h>

#include <bfsar/archive/BfsarSaveContext.h>
#include <bfsar/model/Group.h>

#include <snd/snd_GroupFile.h>

#include <algorithm>
#include <vector>

static u32 GetMaskedGroupItemId(const Group::ItemInfo& itemInfo)
{
    if (itemInfo.getIsDisabled() || !itemInfo.getItemRef().isAttached())
        return 0;

    nw::snd::internal::ItemType itemType;
    switch (itemInfo.getItemRefType())
    {
        case Item::ItemType::Sound:
            itemType = nw::snd::internal::ItemType_Sound;
            break;

        case Item::ItemType::SoundSet:
            itemType = nw::snd::internal::ItemType_SoundGroup;
            break;

        case Item::ItemType::Bank:
            itemType = nw::snd::internal::ItemType_Bank;
            break;

        case Item::ItemType::WaveArchive:
            itemType = nw::snd::internal::ItemType_WaveArchive;
            break;

        default:
            itemType = nw::snd::internal::ItemType(0);
            break;
    }

    return nw::snd::internal::Util::GetMaskedItemId(itemInfo.getItemRef().getItemId(), itemType);
}

struct GroupItemEntry
{
    u32 itemId;
    u32 loadFlag;
};

static std::vector<GroupItemEntry> CollectGroupItemEntries(const Group& group, bool sortById)
{
    std::vector<GroupItemEntry> entries;

    for (const Item* item : group.getItemInfoList())
    {
        SEAD_ASSERT(item->getItemType() == Item::ItemType::GroupItemInfo);
        const Group::ItemInfo* itemInfo = static_cast<const Group::ItemInfo*>(item);

        const u32 itemId = GetMaskedGroupItemId(*itemInfo);

        if (itemId != 0 || !sortById)
            entries.push_back({itemId, itemInfo->getLoadFlag()});
    }

    if (sortById)
        std::stable_sort(entries.begin(), entries.end(), [](const GroupItemEntry& a, const GroupItemEntry& b) { return a.itemId < b.itemId; });

    return entries;
}

u32 BfgrpFile::doWrite(sead::FileHandle* handle, sead::WriteStream* stream, bool isLast) const
{
    LOG_FUNC();
    SEAD_ASSERT(mGroup);
    SEAD_ASSERT(mSaveContext);

    const VectorSet<u32>& itemFiles = mSaveContext->getGroupItemFiles(mGroup);
    const std::vector<File>& files = mSaveContext->files;

    LOG_U32("itemFiles.size()", itemFiles.size());

    FileWriter writer(handle, stream);
    writer.openFile(GetInnerFileMagic(mFormat, InnerFileKind::Group), 3, mVersion);

    u32 fileSize = 0;

    //? Info Block
    {
        LOG_FMT("Writing INFO block with %zu items", itemFiles.size());
        writer.openBlock(nw::snd::internal::ElementType_GroupFile_InfoBlock, "INFO");

        writer.openReferenceTable("ItemInfoTable", itemFiles.size());

        for (u32 i : itemFiles)
        {
            const File& file = files[i];

            writer.addReferenceTableReference("ItemInfoTable", nw::snd::internal::ElementType_GroupFile_GroupItemInfo);

            stream->writeU32(file.id);

            sead::FormatFixedSafeString<32> refName("File%u", file.id);
            writer.openSizedReference(refName);

            if (mGroup->getOutputType() == Group::OutputType::Link)
            {
                writer.closeNullSizedReference(refName);
            }
        }

        writer.closeReferenceTable("ItemInfoTable");

        writer.align(0x20);
        writer.closeBlock();
    }

    //? File Block
    {
        LOG_FMT("Writing FILE block, outputType=%d", (int)mGroup->getOutputType());
        writer.openBlock(nw::snd::internal::ElementType_GroupFile_FileBlock, "FILE");

        writer.align(0x20);

        if (mGroup->getOutputType() != Group::OutputType::Link)
        {
            mEmbeddedFileInfos.clear();

            for (u32 i : itemFiles)
            {
                const File& file = files[i];

                const InnerFile* innerFile = file.innerFile;
                SEAD_ASSERT(innerFile);

                writer.align(0x20);

                PrepareInnerFileForWrite(*mSaveContext, file, mGroup);

                u32 pos = writer.getPosition();
                {
                    innerFile->write(handle, stream, mEndian, file.id == itemFiles.size() - 1);
                }
                u32 size = writer.getPosition() - pos;
                LOG_FMT("Embedded file %u at pos=%u size=%u", file.id, pos, size);

                mEmbeddedFileInfos[file.id] = {pos, size};

                if (mGroup->getOutputType() == Group::OutputType::External)
                    innerFile->clearWriteInfo();

                writer.closeSizedReference(
                    sead::FormatFixedSafeString<32>("File%u", file.id),
                    nw::snd::internal::ElementType_General_ByteStream,
                    size
                );
            }
        }

        writer.align(0x20);
        writer.closeBlock();
    }

    //? InfoEx Block
    {
        const std::vector<GroupItemEntry> entries = CollectGroupItemEntries(*mGroup, mSortItemsById);
        LOG_FMT("Writing INFX block with %zu itemInfo entries", entries.size());
        writer.openBlock(nw::snd::internal::ElementType_GroupFile_InfoExBlock, "INFX");

        writer.openReferenceTable("ItemInfoExTable", entries.size());

        for (const GroupItemEntry& entry : entries)
        {
            writer.addReferenceTableReference("ItemInfoExTable", nw::snd::internal::ElementType_GroupFile_GroupItemInfoEx);

            stream->writeU32(entry.itemId);
            stream->writeU32(entry.loadFlag);
        }

        writer.closeReferenceTable("ItemInfoExTable");

        fileSize = writer.getPosition();

        if (!isLast)
        {
            writer.align(0x20);
        }

        writer.closeBlock();
    }

    writer.closeFile();

    LOG_U32("fileSize", fileSize);

    mGroup = nullptr;
    mSaveContext = nullptr;

    return fileSize;
}
