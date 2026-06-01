#include <snd/snd_SequenceSoundFileReader.h>

#include <format>

#include <ui/PopupMgr.h>
#include <ui/UI.h>

namespace nw { namespace snd { namespace internal {

SequenceSoundFileReader::SequenceSoundFileReader(const void* sequenceFile)
    : mHeader(nullptr)
    , mDataBlockBody(nullptr)
    , mLabelBlockBody(nullptr)
{
    if (!sequenceFile)
    {
        return;
    }

    {
        const ut::BinaryFileHeader* header = reinterpret_cast<const ut::BinaryFileHeader*>(sequenceFile);

        if (sead::MemUtil::compare(header->signature, "FSEQ", 4) != 0 && sead::MemUtil::compare(header->signature, "CSEQ", 4) != 0)
        {
            PopupMgr::instance()->pushCurrentItemError("File is not a valid BFSEQ");
            return;
        }

        if (sead::MemUtil::compare(header->signature, "CSEQ", 4) == 0)
        {
            u32 major = ((u32)header->version >> 24) & 0xFF;
            if (major < 1 || major > 2)
            {
                sead::FormatFixedSafeString<64> msg("CSEQ version not supported (0x%08X)", (u32)header->version);
                PopupMgr::instance()->pushCurrentItemError(msg);
                return;
            }
        }
        else
        {
            if (!(0x00010000 <= (u32)header->version && (u32)header->version <= 0x00020000))
            {
                sead::FormatFixedSafeString<64> msg("BFSEQ version not supported (0x%08X)", (u32)header->version);
                PopupMgr::instance()->pushCurrentItemError(msg);
                return;
            }
        }
    }

    const SequenceSoundFile::FileHeader* header = reinterpret_cast<const SequenceSoundFile::FileHeader*>(sequenceFile);

    const SequenceSoundFile::DataBlock* dataBlock = header->GetDataBlock();
    if (!dataBlock)
    {
        PopupMgr::instance()->pushCurrentItemError("BFSEQ: DATA block not found");
        return;
    }

    if (!CheckBlockCorruptError("BFSEQ", "DATA", dataBlock))
    {
        return;
    }

    const SequenceSoundFile::LabelBlock* labelBlock = header->GetLabelBlock();
    if (!labelBlock)
    {
        PopupMgr::instance()->pushCurrentItemError("BFSEQ: LABL block not found");
        return;
    }

    if (!CheckBlockCorruptError("BFSEQ", "LABL", labelBlock))
    {
        return;
    }

    mHeader = header;
    mDataBlockBody = &dataBlock->body;
    mLabelBlockBody = &labelBlock->body;
}

const void* SequenceSoundFileReader::GetSequenceData() const
{
    SEAD_ASSERT(mDataBlockBody);
    return mDataBlockBody->GetSequenceData();
}

bool SequenceSoundFileReader::GetOffsetByLabel(const char* label, u32* offsetPtr) const
{
    SEAD_ASSERT(mLabelBlockBody);
    return mLabelBlockBody->GetOffsetByLabel(label, offsetPtr);
}

const char* SequenceSoundFileReader::GetLabelByOffset(u32 offset) const
{
    SEAD_ASSERT(mLabelBlockBody);
    return mLabelBlockBody->GetLabelByOffset(offset);
}

void SequenceSoundFileReader::createLabelCache()
{
    mLabelCache.clear();

    for (s32 i = 0; i < GetLabelCount(); i++)
    {
        const SequenceSoundFile::LabelInfo* labelInfo = mLabelBlockBody->GetLabelInfo(i);
        SEAD_ASSERT(labelInfo);

        u32 offset = labelInfo->referToSequenceData.offset;
        const char* label = labelInfo->label;

        mLabelCache[offset].emplace_back(label);
    }
}

const char* SequenceSoundFileReader::getLabelByOffsetFromCache(u32 offset)
{
    const auto& it = mLabelCache.find(offset);
    if (it != mLabelCache.end())
        return it->second.front().c_str();

    return mLabelCache[offset].emplace_back(std::format("_local_{:d}", offset)).c_str();
}

} } } // namespace nw::snd::internal
