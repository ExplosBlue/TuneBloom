#include <snd/snd_GroupFileReader.h>

#include <prim/seadMemUtil.h>

#include <ui/PopupMgr.h>
#include <ui/UI.h>

namespace nw { namespace snd { namespace internal {

GroupFileReader::GroupFileReader(const void* groupFile)
    : mInfoBlockBody(nullptr)
    , mFileBlockBody(nullptr)
    , mInfoExBlockBody(nullptr)
    , mIsInitialized(false)
{
    if (!groupFile)
        return;

    const char* grpFmt;
    {
        const ut::BinaryFileHeader* header = reinterpret_cast<const ut::BinaryFileHeader*>(groupFile);

        if (sead::MemUtil::compare(header->signature, "FGRP", 4) != 0 && sead::MemUtil::compare(header->signature, "CGRP", 4) != 0)
        {
            PopupMgr::instance()->pushCurrentItemError("File is not a valid group file");
            return;
        }

        grpFmt = sead::MemUtil::compare(header->signature, "CGRP", 4) == 0 ? "CGRP" : "FGRP";

        if (sead::MemUtil::compare(header->signature, "CGRP", 4) == 0)
        {
            u32 major = ((u32)header->version >> 24) & 0xFF;
            u32 minor = ((u32)header->version >> 16) & 0xFF;
            if (major != 1)
            {
                sead::FormatFixedSafeString<64> msg("CGRP version not supported (0x%08X)", (u32)header->version);
                PopupMgr::instance()->pushCurrentItemError(msg);
                return;
            }
        }
        else
        {
            if ((u32)header->version != 0x00010000)
            {
                sead::FormatFixedSafeString<64> msg("FGRP version not supported (0x%08X)", (u32)header->version);
                PopupMgr::instance()->pushCurrentItemError(msg);
                return;
            }
        }
    }

    const GroupFile::FileHeader* header = static_cast<const GroupFile::FileHeader*>(groupFile);

    const GroupFile::InfoBlock* infoBlock = header->GetInfoBlock();
    const GroupFile::FileBlock* fileBlock = header->GetFileBlock();
    const GroupFile::InfoExBlock* infoExBlock = header->GetInfoExBlock();

    if (!infoBlock)
    {
        PopupMgr::instance()->pushCurrentItemError(sead::FormatFixedSafeString<64>("%s: INFO block not found", grpFmt).cstr());
        return;
    }

    if (!fileBlock)
    {
        PopupMgr::instance()->pushCurrentItemError(sead::FormatFixedSafeString<64>("%s: FILE block not found", grpFmt).cstr());
        return;
    }

    if (!CheckBlockCorruptError(grpFmt, "INFO", infoBlock))
    {
        return;
    }

    if (!CheckBlockCorruptError(grpFmt, "FILE", fileBlock))
    {
        return;
    }

    if (infoExBlock)
    {
        if (!CheckBlockCorruptError(grpFmt, "INFX", infoExBlock))
        {
            return;
        }

        mInfoExBlockBody = &(infoExBlock->body);
    }

    mInfoBlockBody = &(infoBlock->body);
    mFileBlockBody = &(fileBlock->body);

    mIsInitialized = true;
}

bool GroupFileReader::ReadGroupItemLocationInfo(GroupItemLocationInfo* out, u32 index) const
{
    SEAD_ASSERT(out);

    if (!mInfoBlockBody)
        return false;

    const GroupFile::GroupItemInfo* groupItemInfo = mInfoBlockBody->GetGroupItemInfo(index);
    if (!groupItemInfo)
        return false;

    out->fileId = groupItemInfo->fileId;
    out->address = groupItemInfo->GetFileLocation(mFileBlockBody);

    out->fileSize = groupItemInfo->embeddedItemInfo.size; // Custom

    return true;
}

bool GroupFileReader::ReadGroupItemInfoEx(GroupFile::GroupItemInfoEx* out, u32 index) const
{
    SEAD_ASSERT(out);

    if (!mInfoExBlockBody)
        return false;

    const GroupFile::GroupItemInfoEx* groupItemInfoEx = mInfoExBlockBody->GetGroupItemInfoEx(index);
    if (!groupItemInfoEx)
        return false;

    *out = *groupItemInfoEx; // Copy
    return true;
}

} } } // namespace nw::snd::internal
