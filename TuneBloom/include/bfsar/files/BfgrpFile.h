#pragma once

#include <bfsar/files/InnerFile.h>

#include <unordered_map>

class Group;
struct BfsarSaveContext;

class BfgrpFile : public InnerFile
{
    SEAD_RTTI_OVERRIDE(BfgrpFile, InnerFile);

public:
    BfgrpFile(sead::Endian::Types endian, u32 version, ArchiveFormat format, bool sortItemsById)
        : InnerFile()
        , mSortItemsById(sortItemsById)
        , mGroup(nullptr)
        , mSaveContext(nullptr)
    {
        mEndian = endian;
        mVersion = version;
        mFormat = format;
    }

    struct EmbeddedFileInfo
    {
        u32 offset;
        u32 size;
    };

    const std::unordered_map<u32, EmbeddedFileInfo>& getEmbeddedFileInfos() const
    {
        return mEmbeddedFileInfos;
    }

    void prepare(const Group* group, const BfsarSaveContext& saveContext) const
    {
        SEAD_ASSERT(!mGroup);
        mGroup = group;

        SEAD_ASSERT(!mSaveContext);
        mSaveContext = &saveContext;
    }

private:
    bool doRead(const void* fileAddr) override
    {
        return false;
    }

    u32 doWrite(sead::FileHandle* handle, sead::WriteStream* stream, bool isLast) const override;

private:
    bool mSortItemsById;
    mutable std::unordered_map<u32, EmbeddedFileInfo> mEmbeddedFileInfos;
    mutable const Group* mGroup;
    mutable const BfsarSaveContext* mSaveContext;
};
