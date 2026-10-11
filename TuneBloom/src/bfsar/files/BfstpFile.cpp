#include <bfsar/files/BfstpFile.h>

#include <bfsar/io/FileIo.h>

#include <snd/snd_ElementType.h>
#include <snd/snd_StreamSoundFile.h>

#include <algorithm>
#include <cstddef>
#include <cstring>

using nw::snd::internal::StreamSoundFile;
using nw::snd::internal::Util;

namespace
{

constexpr u32 cPrefetchVersion     = 0x00010100;
constexpr u32 cFileHeaderSize      = 0x40;
constexpr u32 cBlockHeaderSize     = sizeof(nw::ut::BinaryBlockHeader);
constexpr u32 cBlockAlignment      = 0x20;
constexpr u16 cPrefetchBlockCount  = 2;
constexpr u32 cInvalidOffset       = 0xFFFFFFFF;

// File header
constexpr u32 cHeaderByteOrder     = offsetof(nw::ut::BinaryFileHeader, byteOrder);
constexpr u32 cHeaderSize          = offsetof(nw::ut::BinaryFileHeader, headerSize);
constexpr u32 cHeaderVersion       = offsetof(nw::ut::BinaryFileHeader, version);
constexpr u32 cHeaderFileSize      = offsetof(nw::ut::BinaryFileHeader, fileSize);
constexpr u32 cHeaderBlockCount    = offsetof(nw::ut::BinaryFileHeader, dataBlocks);
constexpr u32 cHeaderBlockRefs     = sizeof(nw::ut::BinaryFileHeader);
constexpr u32 cBlockRefSize        = sizeof(Util::ReferenceWithSize);
constexpr u32 cReferenceOffset     = offsetof(Util::Reference, offset);
constexpr u32 cBlockRefBlockSize   = offsetof(Util::ReferenceWithSize, size);

// INFO block
constexpr u32 cInfoChannelCount    = offsetof(StreamSoundFile::StreamSoundInfo, channelCount);
constexpr u32 cInfoBlockCount      = offsetof(StreamSoundFile::StreamSoundInfo, blockCount);
constexpr u32 cInfoOneBlockBytes   = offsetof(StreamSoundFile::StreamSoundInfo, oneBlockBytes);
constexpr u32 cInfoLastBlockBytes  = offsetof(StreamSoundFile::StreamSoundInfo, lastBlockPaddedBytes);
constexpr u32 cInfoSampleOffset    = offsetof(StreamSoundFile::StreamSoundInfo, sampleDataOffset) + cReferenceOffset;
constexpr u32 cInfoRegionOffset    = offsetof(StreamSoundFile::StreamSoundInfo, regionDataOffset) + cReferenceOffset;
constexpr u32 cInfoSizeMin         = offsetof(StreamSoundFile::StreamSoundInfo, originalLoopStart);

// PDAT block
constexpr u32 cPdatCount           = cBlockHeaderSize;
constexpr u32 cPdatItem            = cPdatCount + sizeof(u32);
constexpr u32 cPdatItemSize        = 0x14;
constexpr u32 cPdatItemPrefetchSize = 0x04;
constexpr u32 cPdatItemSampleRef   = 0x0C;
constexpr u32 cPdatHeadSize        = cPdatItem + cPdatItemSize;

constexpr u64 cStreamHeadReadSize  = 0x400;
constexpr u32 cStreamReadPassMax   = 3;

class FileView
{
public:
    FileView(const void* data, u64 size)
        : mData(static_cast<const u8*>(data))
        , mSize(size)
        , mIsBig(size > cHeaderByteOrder && mData[cHeaderByteOrder] == 0xFE)
    {
    }

    bool has(u64 offset, u64 size) const
    {
        return mData && offset <= mSize && size <= mSize - offset;
    }

    u64 size() const
    {
        return mSize;
    }

    const u8* at(u64 offset) const
    {
        return mData + offset;
    }

    bool isBigEndian() const
    {
        return mIsBig;
    }

    u16 readU16(u64 offset) const
    {
        const u8* p = mData + offset;

        return mIsBig ? static_cast<u16>(p[0] << 8 | p[1]) : static_cast<u16>(p[1] << 8 | p[0]);
    }

    u32 readU32(u64 offset) const
    {
        const u8* p = mData + offset;

        if (mIsBig)
            return static_cast<u32>(p[0]) << 24 | static_cast<u32>(p[1]) << 16 | static_cast<u32>(p[2]) << 8 | p[3];

        return static_cast<u32>(p[3]) << 24 | static_cast<u32>(p[2]) << 16 | static_cast<u32>(p[1]) << 8 | p[0];
    }

private:
    const u8* mData;
    u64 mSize;
    bool mIsBig;
};

class FileBuilder
{
public:
    FileBuilder(std::vector<u8>* bytes, bool isBig)
        : mBytes(*bytes)
        , mIsBig(isBig)
    {
    }

    void writeU16(u64 offset, u16 value)
    {
        mBytes[offset + (mIsBig ? 0 : 1)] = static_cast<u8>(value >> 8);
        mBytes[offset + (mIsBig ? 1 : 0)] = static_cast<u8>(value);
    }

    void writeU32(u64 offset, u32 value)
    {
        for (u32 i = 0; i < sizeof(u32); i++)
            mBytes[offset + (mIsBig ? sizeof(u32) - 1 - i : i)] = static_cast<u8>(value >> (8 * i));
    }

    void writeBytes(u64 offset, const void* data, u64 size)
    {
        std::memcpy(mBytes.data() + offset, data, size);
    }

    void writeBlockRef(u32 index, u16 typeId, u32 offset, u32 size)
    {
        const u64 ref = cHeaderBlockRefs + index * cBlockRefSize;

        writeU16(ref, typeId);
        writeU32(ref + cReferenceOffset, offset);
        writeU32(ref + cBlockRefBlockSize, size);
    }

private:
    std::vector<u8>& mBytes;
    bool mIsBig;
};

struct FileBlock
{
    u64 offset = 0;
    u64 size = 0;
};

struct PrefetchSource
{
    FileBlock info;
    u64 streamInfoOffset = 0;
    u64 sampleOffset = 0;
    u64 sampleSize = 0;
    u64 neededSize = 0;
};

}

static bool FindBlock(const FileView& file, u16 typeId, FileBlock* out)
{
    if (!file.has(0, cHeaderBlockRefs))
        return false;

    const u32 blockCount = file.readU16(cHeaderBlockCount);

    if (!file.has(cHeaderBlockRefs, static_cast<u64>(blockCount) * cBlockRefSize))
        return false;

    for (u32 i = 0; i < blockCount; i++)
    {
        const u64 ref = cHeaderBlockRefs + i * cBlockRefSize;

        if (file.readU16(ref) != typeId)
            continue;

        out->offset = file.readU32(ref + cReferenceOffset);
        out->size = file.readU32(ref + cBlockRefBlockSize);

        return out->offset != cInvalidOffset && out->size >= cBlockHeaderSize;
    }

    return false;
}

static u64 FindStreamInfo(const FileView& file, const FileBlock& info)
{
    const u64 body = info.offset + cBlockHeaderSize;
    const u64 blockEnd = info.offset + info.size;

    if (!file.has(info.offset, info.size) || info.size < cBlockHeaderSize + sizeof(StreamSoundFile::InfoBlockBody))
        return 0;

    if (file.readU16(body) != nw::snd::internal::ElementType_StreamSoundFile_StreamSoundInfo)
        return 0;

    const u64 streamInfoOffset = file.readU32(body + cReferenceOffset);
    u64 streamInfoEnd = info.size - cBlockHeaderSize;

    for (u32 i = 1; i < sizeof(StreamSoundFile::InfoBlockBody) / sizeof(Util::Reference); i++)
    {
        const u64 offset = file.readU32(body + i * sizeof(Util::Reference) + cReferenceOffset);

        if (offset != cInvalidOffset && offset > streamInfoOffset)
            streamInfoEnd = std::min(streamInfoEnd, offset);
    }

    if (streamInfoOffset > streamInfoEnd || streamInfoEnd - streamInfoOffset < cInfoSizeMin || body + streamInfoEnd > blockEnd)
        return 0;

    return body + streamInfoOffset;
}

static bool ReadPrefetchSource(const FileView& file, u32 streamBufferTimes, PrefetchSource* out)
{
    if (!file.has(0, cHeaderBlockRefs) || !MatchesInnerFileKind(file.at(0), InnerFileKind::Stream))
        return false;

    out->neededSize = cHeaderBlockRefs + static_cast<u64>(file.readU16(cHeaderBlockCount)) * cBlockRefSize;

    if (out->neededSize > file.size())
        return true;

    FileBlock data;

    if (!FindBlock(file, nw::snd::internal::ElementType_StreamSoundFile_InfoBlock, &out->info) ||
        !FindBlock(file, nw::snd::internal::ElementType_StreamSoundFile_DataBlock, &data) ||
        out->info.size % cBlockAlignment != 0)
        return false;

    out->neededSize = out->info.offset + out->info.size;

    if (out->neededSize > file.size())
        return true;

    out->streamInfoOffset = FindStreamInfo(file, out->info);

    if (out->streamInfoOffset == 0)
        return false;

    const u64 channelCount = *file.at(out->streamInfoOffset + cInfoChannelCount);
    const u64 blockCount = file.readU32(out->streamInfoOffset + cInfoBlockCount);
    const u64 oneBlockBytes = file.readU32(out->streamInfoOffset + cInfoOneBlockBytes);
    const u64 lastBlockBytes = file.readU32(out->streamInfoOffset + cInfoLastBlockBytes);
    const u64 sampleDataOffset = file.readU32(out->streamInfoOffset + cInfoSampleOffset);

    if (channelCount == 0 || blockCount == 0 || oneBlockBytes == 0 || sampleDataOffset == cInvalidOffset)
        return false;

    const u64 streamSize = ((blockCount - 1) * oneBlockBytes + lastBlockBytes) * channelCount;
    const u64 prefetchSize = static_cast<u64>(BfstpFile::cBlocksPerBufferTime) * streamBufferTimes * oneBlockBytes * channelCount;

    out->sampleOffset = data.offset + cBlockHeaderSize + sampleDataOffset;
    out->sampleSize = std::min(prefetchSize, streamSize);
    out->neededSize = out->sampleOffset + out->sampleSize;

    return out->sampleSize != 0 && out->sampleSize <= cInvalidOffset && out->neededSize <= data.offset + data.size;
}

bool BfstpFile::Build(std::vector<u8>* out, const void* strmFile, u64 strmFileSize, u32 streamBufferTimes)
{
    const FileView file(strmFile, strmFileSize);
    PrefetchSource source;

    out->clear();

    if (!ReadPrefetchSource(file, streamBufferTimes, &source) || source.neededSize > file.size())
        return false;

    const u32 infoOffset = cFileHeaderSize;
    const u32 infoSize = static_cast<u32>(source.info.size);
    const u32 pdatOffset = infoOffset + infoSize;
    const u32 sampleOffset = pdatOffset + cPdatHeadSize;
    const u32 sampleSize = static_cast<u32>(source.sampleSize);
    const u32 streamInfoOffset = infoOffset + static_cast<u32>(source.streamInfoOffset - source.info.offset);

    out->assign(static_cast<size_t>(sampleOffset) + sampleSize, 0);

    FileBuilder builder(out, file.isBigEndian());

    // File header
    builder.writeBytes(0, GetInnerFileMagic(ArchiveFormat::BFSAR, InnerFileKind::StreamPrefetch), cInnerFileMagicLength);
    builder.writeBytes(cHeaderByteOrder, file.at(cHeaderByteOrder), sizeof(u16));
    builder.writeU16(cHeaderSize, cFileHeaderSize);
    builder.writeU32(cHeaderVersion, cPrefetchVersion);
    builder.writeU32(cHeaderFileSize, static_cast<u32>(out->size()));
    builder.writeU16(cHeaderBlockCount, cPrefetchBlockCount);
    builder.writeBlockRef(0, nw::snd::internal::ElementType_StreamSoundFile_InfoBlock, infoOffset, infoSize);
    builder.writeBlockRef(1, nw::snd::internal::ElementType_StreamSoundFile_PrefetchDataBlock, pdatOffset, cPdatHeadSize + sampleSize);

    // INFO block of the stream file
    builder.writeBytes(infoOffset, file.at(source.info.offset), infoSize);
    builder.writeU32(streamInfoOffset + cInfoSampleOffset, sampleOffset);
    builder.writeU32(streamInfoOffset + cInfoRegionOffset, cInvalidOffset);

    // PDAT block
    builder.writeBytes(pdatOffset, "PDAT", cInnerFileMagicLength);
    builder.writeU32(pdatOffset + sizeof(u32), cPdatHeadSize + sampleSize);
    builder.writeU32(pdatOffset + cPdatCount, 1);
    builder.writeU32(pdatOffset + cPdatItem + cPdatItemPrefetchSize, sampleSize);
    builder.writeU32(pdatOffset + cPdatItem + cPdatItemSampleRef + cReferenceOffset, cPdatItemSize);
    builder.writeBytes(sampleOffset, file.at(source.sampleOffset), sampleSize);

    return true;
}

bool BfstpFile::BuildFromStreamFile(std::vector<u8>* out, const char* strmFilePath, u32 streamBufferTimes)
{
    std::vector<u8> strmFile;
    u64 readSize = cStreamHeadReadSize;

    out->clear();

    for (u32 pass = 0; pass < cStreamReadPassMax; pass++)
    {
        if (!fileio::ReadFileStart(strmFilePath, readSize, &strmFile))
            return false;

        PrefetchSource source;

        if (!ReadPrefetchSource(FileView(strmFile.data(), strmFile.size()), streamBufferTimes, &source))
            return false;

        if (source.neededSize <= strmFile.size())
            return Build(out, strmFile.data(), strmFile.size(), streamBufferTimes);

        if (strmFile.size() < readSize)
            return false;

        readSize = source.neededSize;
    }

    return false;
}

static bool FindPrefetchSamples(const FileView& file, FileBlock* outSamples)
{
    FileBlock info;
    FileBlock pdat;

    if (!file.has(0, cFileHeaderSize) || !MatchesInnerFileKind(file.at(0), InnerFileKind::StreamPrefetch))
        return false;

    if (!FindBlock(file, nw::snd::internal::ElementType_StreamSoundFile_InfoBlock, &info) ||
        !FindBlock(file, nw::snd::internal::ElementType_StreamSoundFile_PrefetchDataBlock, &pdat))
        return false;

    if (FindStreamInfo(file, info) == 0 || !file.has(pdat.offset, pdat.size) || pdat.size < cPdatHeadSize)
        return false;

    if (file.readU32(pdat.offset + cPdatCount) == 0)
        return false;

    const u64 item = pdat.offset + cPdatItem;
    const u64 sampleRef = file.readU32(item + cPdatItemSampleRef + cReferenceOffset);

    outSamples->offset = item + sampleRef;
    outSamples->size = file.readU32(item + cPdatItemPrefetchSize);

    return sampleRef != cInvalidOffset && outSamples->size != 0 && outSamples->offset >= pdat.offset + cPdatHeadSize &&
           outSamples->offset <= pdat.offset + pdat.size && outSamples->size <= pdat.offset + pdat.size - outSamples->offset;
}

bool BfstpFile::CheckLayout(const void* data, u64 size)
{
    FileBlock samples;

    return FindPrefetchSamples(FileView(data, size), &samples);
}

u32 BfstpFile::doWrite(sead::FileHandle* handle, sead::WriteStream* stream, bool isLast) const
{
    stream->writeMemBlock(mBytes.data(), static_cast<u32>(mBytes.size()));

    return static_cast<u32>(mBytes.size());
}
