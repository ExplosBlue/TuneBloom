#include <bfsar/writer/FileWriter.h>

#include <snd/snd_Util.h>

#include <math/seadMathCalcCommon.h>

#include <Debug.h>

void FileWriter::openFile(const sead::SafeString& magic, u32 numBlocks, u32 version)
{
    LOG_FUNC();
    LOG_FMT("magic=\"%.*s\" numBlocks=%u version=%u", magic.calcLength(), magic.cstr(), numBlocks, version);

    mBlocks.allocBuffer(numBlocks);

    mHeaderPos = getPosition();

    mStream->writeString(magic, magic.calcLength());

    mStream->writeU16(0xFEFF); // Endian

    mHeaderSize = sizeof(nw::ut::BinaryFileHeader);
    mHeaderSize += numBlocks * sizeof(nw::snd::internal::Util::ReferenceWithSize);
    mHeaderSize = sead::MathCalcCommon<u16>::roundUpPow2(mHeaderSize, 0x20);

    mStream->writeU16(mHeaderSize);

    mStream->writeU32(version);

    mStream->writeU32(0); // File Size

    mStream->writeU16(numBlocks);

    mStream->writeU16(0); // Reserved

    for (u32 i = 0; i < numBlocks; i++)
    {
        nw::snd::internal::Util::ReferenceWithSize ref;
        sead::MemUtil::fillZero(&ref, sizeof(ref));

        mStream->writeMemBlock(&ref, sizeof(ref));
    }

    u32 padBytes = mHeaderSize - getPosition();
    for (u32 i = 0; i < padBytes; i++)
    {
        mStream->writeU8(0);
    }
}

void FileWriter::closeFile()
{
    LOG_FUNC();

    if (mBlockOpen)
    {
        SEAD_ASSERT_MSG(false, "tried to close file with open block");
        return;
    }

    if (mReferences.size() > 0)
    {
        SEAD_ASSERT_MSG(false, "tried to close file with open References");
        return;
    }

    if (mSizedReferences.size() > 0)
    {
        SEAD_ASSERT_MSG(false, "tried to close file with open SizedReferences");
        return;
    }

    if (mReferenceTables.size() > 0)
    {
        SEAD_ASSERT_MSG(false, "tried to close file with open ReferenceTables");
        return;
    }

    if (mSizedReferenceTables.size() > 0)
    {
        SEAD_ASSERT_MSG(false, "tried to close file with open SizedReferenceTables");
        return;
    }

    if (mOffsetBases.size() > 0)
    {
        SEAD_ASSERT_MSG(false, "tried to close file with OffsetBases mismatch");
        return;
    }

    u32 size = mHeaderSize;
    for (const Block* block = mBlocks.front(); block; block = mBlocks.next(block))
    {
        size += block->size;
    }

    LOG_FMT("mHeaderPos=%u totalSize=%u", mHeaderPos, size);

    seek(mHeaderPos + offsetof(nw::ut::BinaryFileHeader, fileSize));
    mStream->writeU32(size);

    //? Write blocks
    seek(mHeaderPos + sizeof(nw::ut::BinaryFileHeader));

    s32 offset = mHeaderSize;
    for (const Block* block = mBlocks.front(); block; block = mBlocks.next(block))
    {
        mStream->writeU16(block->type);
        mStream->writeU16(0); // Padding

        if (block->size != 0)
        {
            mStream->writeS32(offset);
            mStream->writeU32(block->size);
        }
        else
        {
            mStream->writeS32(nw::snd::internal::Util::Reference::INVALID_OFFSET);
            mStream->writeU32(0xFFFFFFFF);
        }

        offset += block->size;
    }

    //? Restore position to the end
    seek(mHeaderPos + size);
}

void FileWriter::openBlock(u16 typeId, const sead::SafeString& magic)
{
    LOG_FUNC();
    LOG_FMT("typeId=0x%04X magic=\"%.*s\"", typeId, magic.calcLength(), magic.cstr());

    if (mBlockOpen)
    {
        SEAD_ASSERT_MSG(false, "block open/close mismatch");
        return;
    }

    Block* block = mBlocks.birthBack();
    SEAD_ASSERT(block);

    block->type = typeId;
    block->size = 0;

    mBlockPos = getPosition();
    LOG_FMT("mBlockPos=%u", mBlockPos);

    mOffsetBase = mBlockPos + sizeof(nw::ut::BinaryBlockHeader);

    mStream->writeString(magic, magic.calcLength());

    mStream->writeU32(0); // Size

    mBlockOpen = true;
}

void FileWriter::closeBlock()
{
    LOG_FUNC();

    if (!mBlockOpen)
    {
        SEAD_ASSERT_MSG(false, "block open/close mismatch");
        return;
    }

    if (mOffsetBases.size() > 0)
    {
        SEAD_ASSERT_MSG(false, "tried to close block with OffsetBases mismatch");
        return;
    }

    u32 blockSize = getPosition() - mBlockPos;
    LOG_FMT("mBlockPos=%u blockSize=%u", mBlockPos, blockSize);

    seek(mBlockPos + offsetof(nw::ut::BinaryBlockHeader, size));
    mStream->writeU32(blockSize);

    Block* block = mBlocks.back();
    SEAD_ASSERT(block);

    block->size = blockSize;

    seek(mBlockPos + blockSize);

    mBlockOpen = false;
}

void FileWriter::nullBlock(u16 typeId)
{
    LOG_FUNC();
    LOG_U16("typeId", typeId);

    Block* block = mBlocks.birthBack();
    block->type = typeId;
    block->size = 0;
}

void FileWriter::align(u32 alignment)
{
    LOG_FUNC();
    LOG_U32("alignment", alignment);

    if (!mBlockOpen)
    {
        SEAD_ASSERT_MSG(false, "block not open");
        return;
    }

    while ((getPosition() - mBlockPos) % alignment != 0)
    {
        mStream->writeU8(0);
    }
}

void FileWriter::openReference(const sead::SafeString& name)
{
    LOG_FUNC();
    LOG_STR(name.cstr());

    if (mReferences.contains(name.cstr()))
    {
        SEAD_ASSERT_MSG(false, "reference already exists");
        return;
    }

    u32 pos = getPosition();
    LOG_FMT("pos=%u mOffsetBase=%u mBlockPos=%u", pos, mOffsetBase, mBlockPos);

    {
        nw::snd::internal::Util::Reference ref;
        sead::MemUtil::fillZero(&ref, sizeof(ref));

        mStream->writeMemBlock(&ref, sizeof(ref));
    }

    mReferences[name.cstr()] = Reference(pos, mOffsetBase - mBlockPos);
}

void FileWriter::closeReference(const sead::SafeString& name, u16 typeId)
{
    LOG_FUNC();
    LOG_STR(name.cstr());

    if (!mReferences.contains(name.cstr()))
    {
        SEAD_ASSERT_MSG(false, "reference not found");
        return;
    }

    const Reference& ref = mReferences[name.cstr()];

    s32 offset = getPosition() - ref.offset - mBlockPos;
    LOG_FMT("typeId=0x%04X offset=%d ref.pos=%u ref.offset=%u", typeId, offset, ref.pos, ref.offset);

    u32 prevPos = getPosition();
    seek(ref.pos);

    mStream->writeU16(typeId);
    mStream->writeU16(0); // Padding
    mStream->writeS32(offset);

    seek(prevPos);

    mReferences.erase(name.cstr());
}

void FileWriter::closeReference(const sead::SafeString& name, u16 typeId, s32 offset)
{
    LOG_FUNC();
    LOG_FMT("name=\"%s\" typeId=0x%04X offset=%d", name.cstr(), typeId, offset);

    if (!mReferences.contains(name.cstr()))
    {
        SEAD_ASSERT_MSG(false, "reference not found");
        return;
    }

    const Reference& ref = mReferences[name.cstr()];

    u32 prevPos = getPosition();
    seek(ref.pos);

    mStream->writeU16(typeId);
    mStream->writeU16(0); // Padding
    mStream->writeS32(offset);

    seek(prevPos);

    mReferences.erase(name.cstr());
}

void FileWriter::closeNullReference(const sead::SafeString& name)
{
    LOG_FUNC();
    LOG_STR(name.cstr());

    if (!mReferences.contains(name.cstr()))
    {
        SEAD_ASSERT_MSG(false, "reference not found");
        return;
    }

    const Reference& ref = mReferences[name.cstr()];

    s32 offset = nw::snd::internal::Util::Reference::INVALID_OFFSET;

    u32 prevPos = getPosition();
    seek(ref.pos);

    mStream->writeU16(0); // Type
    mStream->writeU16(0); // Padding
    mStream->writeS32(offset);

    seek(prevPos);

    mReferences.erase(name.cstr());
}

void FileWriter::openSizedReference(const sead::SafeString& name)
{
    LOG_FUNC();
    LOG_STR(name.cstr());

    if (mSizedReferences.contains(name.cstr()))
    {
        SEAD_ASSERT_MSG(false, "sized reference already exists");
        return;
    }

    u32 pos = getPosition();
    LOG_FMT("pos=%u mOffsetBase=%u mBlockPos=%u", pos, mOffsetBase, mBlockPos);

    {
        nw::snd::internal::Util::ReferenceWithSize ref;
        sead::MemUtil::fillZero(&ref, sizeof(ref));

        mStream->writeMemBlock(&ref, sizeof(ref));
    }

    mSizedReferences[name.cstr()] = Reference(pos, mOffsetBase - mBlockPos);
}

void FileWriter::closeSizedReference(const sead::SafeString& name, u16 typeId, u32 size)
{
    LOG_FUNC();
    LOG_FMT("name=\"%s\" typeId=0x%04X size=%u", name.cstr(), typeId, size);

    if (!mSizedReferences.contains(name.cstr()))
    {
        SEAD_ASSERT_MSG(false, "sized reference not found");
        return;
    }

    const Reference& ref = mSizedReferences[name.cstr()];

    s32 offset = getPosition() - ref.offset - mBlockPos - size;
    LOG_FMT("computed offset=%d ref.pos=%u ref.offset=%u", offset, ref.pos, ref.offset);

    u32 prevPos = getPosition();
    seek(ref.pos);

    mStream->writeU16(typeId);
    mStream->writeU16(0); // Padding
    mStream->writeS32(offset);
    mStream->writeU32(size);

    seek(prevPos);

    mSizedReferences.erase(name.cstr());
}

void FileWriter::closeSizedReference(const sead::SafeString& name, u16 typeId, s32 offset, u32 size)
{
    LOG_FUNC();
    LOG_FMT("name=\"%s\" typeId=0x%04X offset=%d size=%u", name.cstr(), typeId, offset, size);

    if (!mSizedReferences.contains(name.cstr()))
    {
        SEAD_ASSERT_MSG(false, "sized reference not found");
        return;
    }

    const Reference& ref = mSizedReferences[name.cstr()];

    u32 prevPos = getPosition();
    seek(ref.pos);

    mStream->writeU16(typeId);
    mStream->writeU16(0); // Padding
    mStream->writeS32(offset);
    mStream->writeU32(size);

    seek(prevPos);

    mSizedReferences.erase(name.cstr());
}

void FileWriter::closeNullSizedReference(const sead::SafeString& name)
{
    LOG_FUNC();
    LOG_STR(name.cstr());

    if (!mSizedReferences.contains(name.cstr()))
    {
        SEAD_ASSERT_MSG(false, "sized reference not found");
        return;
    }

    const Reference& ref = mSizedReferences[name.cstr()];

    s32 offset = nw::snd::internal::Util::Reference::INVALID_OFFSET;

    u32 prevPos = getPosition();
    seek(ref.pos);

    mStream->writeU16(0); // Type
    mStream->writeU16(0); // Padding
    mStream->writeS32(offset);
    mStream->writeU32(0xFFFFFFFF); // Size

    seek(prevPos);

    mSizedReferences.erase(name.cstr());
}

void FileWriter::pushOffsetBase()
{
    LOG_FUNC();
    LOG_FMT("mOffsetBase=%u -> new=%u", mOffsetBase, getPosition());

    mOffsetBases.push(mOffsetBase);

    mOffsetBase = getPosition();
}

void FileWriter::popOffsetBase()
{
    LOG_FUNC();
    LOG_FMT("restored mOffsetBase=%u", mOffsetBases.top());

    mOffsetBase = mOffsetBases.top();
    mOffsetBases.pop();
}

void FileWriter::openReferenceTable(const sead::SafeString& name, u32 count)
{
    LOG_FUNC();
    LOG_FMT("name=\"%s\" count=%u", name.cstr(), count);

    if (mReferenceTables.contains(name.cstr()))
    {
        SEAD_ASSERT_MSG(false, "reference table already exists");
        return;
    }

    u32 pos = getPosition();
    LOG_FMT("pos=%u mOffsetBase=%u mBlockPos=%u", pos, mOffsetBase, mBlockPos);

    {
        mStream->writeU32(count);

        for (u32 i = 0; i < count; i++)
        {
            nw::snd::internal::Util::Reference ref;
            sead::MemUtil::fillZero(&ref, sizeof(ref));

            mStream->writeMemBlock(&ref, sizeof(ref));
        }
    }

    mReferenceTables[name.cstr()] = ReferenceTable(count, pos, mOffsetBase - mBlockPos);
}

void FileWriter::closeReferenceTable(const sead::SafeString& name)
{
    LOG_FUNC();
    LOG_STR(name.cstr());

    if (!mReferenceTables.contains(name.cstr()))
    {
        SEAD_ASSERT_MSG(false, "reference table not found");
        return;
    }

    const ReferenceTable& table = mReferenceTables[name.cstr()];
    LOG_FMT("count=%d pos=%u", (s32)table.references.size(), table.pos);

    u32 prevPos = getPosition();
    seek(table.pos + offsetof(nw::snd::internal::Util::ReferenceTable, item));

    for (u32 i = 0; i < table.references.size(); i++)
    {
        const ReferenceTable::Reference& ref = table.references[i];

        mStream->writeU16(ref.type);
        mStream->writeU16(0); // Padding
        mStream->writeS32(ref.offset);
    }

    seek(prevPos);

    mReferenceTables.erase(name.cstr());
}

void FileWriter::addReferenceTableReference(const sead::SafeString& name, u16 typeId)
{
    LOG_FUNC();
    LOG_FMT("name=\"%s\" typeId=0x%04X", name.cstr(), typeId);

    if (!mReferenceTables.contains(name.cstr()))
    {
        SEAD_ASSERT_MSG(false, "reference table not found");
        return;
    }

    ReferenceTable& table = mReferenceTables[name.cstr()];

    s32 offset = getPosition() - table.offset - mBlockPos;
    LOG_FMT("offset=%d table.offset=%u mBlockPos=%u", offset, table.offset, mBlockPos);

    table.add(typeId, offset);
}

void FileWriter::addReferenceTableNullReference(const sead::SafeString& name, u16 typeId)
{
    LOG_FUNC();
    LOG_FMT("name=\"%s\" typeId=0x%04X", name.cstr(), typeId);

    if (!mReferenceTables.contains(name.cstr()))
    {
        SEAD_ASSERT_MSG(false, "reference table not found");
        return;
    }

    ReferenceTable& table = mReferenceTables[name.cstr()];

    table.add(typeId, -1);
}

void FileWriter::openSizedReferenceTable(const sead::SafeString& name, u32 count)
{
    LOG_FUNC();
    LOG_FMT("name=\"%s\" count=%u", name.cstr(), count);

    if (mSizedReferenceTables.contains(name.cstr()))
    {
        SEAD_ASSERT_MSG(false, "sized reference table already exists");
        return;
    }

    u32 pos = getPosition();
    LOG_FMT("pos=%u mOffsetBase=%u mBlockPos=%u", pos, mOffsetBase, mBlockPos);

    {
        mStream->writeU32(count);

        for (u32 i = 0; i < count; i++)
        {
            nw::snd::internal::Util::ReferenceWithSize ref;
            sead::MemUtil::fillZero(&ref, sizeof(ref));

            mStream->writeMemBlock(&ref, sizeof(ref));
        }
    }

    mSizedReferenceTables[name.cstr()] = ReferenceTable(count, pos, mOffsetBase - mBlockPos);
}

void FileWriter::closeSizedReferenceTable(const sead::SafeString& name)
{
    LOG_FUNC();
    LOG_STR(name.cstr());

    if (!mSizedReferenceTables.contains(name.cstr()))
    {
        SEAD_ASSERT_MSG(false, "sized reference table not found");
        return;
    }

    const ReferenceTable& table = mSizedReferenceTables[name.cstr()];
    LOG_FMT("count=%d pos=%u", (s32)table.references.size(), table.pos);

    u32 prevPos = getPosition();
    seek(table.pos + offsetof(nw::snd::internal::Util::ReferenceWithSizeTable, item));

    for (u32 i = 0; i < table.references.size(); i++)
    {
        const ReferenceTable::Reference& ref = table.references[i];

        mStream->writeU16(ref.type);
        mStream->writeU16(0); // Padding
        mStream->writeS32(ref.offset);
        mStream->writeU32(ref.size);
    }

    seek(prevPos);

    mSizedReferenceTables.erase(name.cstr());
}

void FileWriter::addSizedReferenceTableReference(const sead::SafeString& name, u16 typeId, u32 size)
{
    LOG_FUNC();
    LOG_FMT("name=\"%s\" typeId=0x%04X size=%u", name.cstr(), typeId, size);

    if (!mSizedReferenceTables.contains(name.cstr()))
    {
        SEAD_ASSERT_MSG(false, "sized reference table not found");
        return;
    }

    ReferenceTable& table = mSizedReferenceTables[name.cstr()];

    s32 offset = getPosition() - table.offset - mBlockPos;
    LOG_FMT("offset=%d table.offset=%u mBlockPos=%u", offset, table.offset, mBlockPos);

    table.add(typeId, offset, size);
}

void FileWriter::setSizedReferenceTableReferenceSize(const sead::SafeString& name, u32 size)
{
    LOG_FUNC();
    LOG_FMT("name=\"%s\" size=%u", name.cstr(), size);

    if (!mSizedReferenceTables.contains(name.cstr()))
    {
        SEAD_ASSERT_MSG(false, "sized reference table not found");
        return;
    }

    ReferenceTable& table = mSizedReferenceTables[name.cstr()];

    table.adjustSize(size);
}
