#include <bfsar/files/BfstpFile.h>

#include <aal/Bfstm.h>

#include <core/ByteBuilder.h>
#include <bfsar/io/FileIo.h>

#include <math/seadMathCalcCommon.h>
#include <snd/snd_ElementType.h>
#include <snd/snd_StreamSoundFile.h>

#include <algorithm>
#include <cstddef>

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

constexpr u32 cHeaderByteOrder     = offsetof(nw::ut::BinaryFileHeader, byteOrder);
constexpr u32 cHeaderSize          = offsetof(nw::ut::BinaryFileHeader, headerSize);
constexpr u32 cHeaderVersion       = offsetof(nw::ut::BinaryFileHeader, version);
constexpr u32 cHeaderFileSize      = offsetof(nw::ut::BinaryFileHeader, fileSize);
constexpr u32 cHeaderBlockCount    = offsetof(nw::ut::BinaryFileHeader, dataBlocks);
constexpr u32 cHeaderBlockRefs     = sizeof(nw::ut::BinaryFileHeader);
constexpr u32 cBlockRefSize        = sizeof(Util::ReferenceWithSize);
constexpr u32 cReferenceOffset     = offsetof(Util::Reference, offset);
constexpr u32 cBlockRefBlockSize   = offsetof(Util::ReferenceWithSize, size);

constexpr u32 cInfoSampleOffset    = offsetof(StreamSoundFile::StreamSoundInfo, sampleDataOffset) + cReferenceOffset;
constexpr u32 cInfoRegionOffset    = offsetof(StreamSoundFile::StreamSoundInfo, regionDataOffset) + cReferenceOffset;
constexpr u32 cInfoSizeMin         = offsetof(StreamSoundFile::StreamSoundInfo, originalLoopStart);

constexpr u32 cPdatCount           = cBlockHeaderSize;
constexpr u32 cPdatItem            = cPdatCount + sizeof(u32);
constexpr u32 cPdatItemSize        = 0x14;
constexpr u32 cPdatItemPrefetchSize = 0x04;
constexpr u32 cPdatItemSampleRef   = 0x0C;
constexpr u32 cPdatHeadSize        = cPdatItem + cPdatItemSize;
constexpr u32 cSampleAlignment     = 0x40;

constexpr u32 cAalPrefetchVersion   = 0x00020100;
constexpr u32 cAalPrefetchBlocks    = 3;
constexpr u64 cStreamHeadReadSize  = 0x400;
constexpr u32 cStreamReadPassMax   = 3;

struct PrefetchShape
{
    u32 version = cPrefetchVersion;
    u32 blocksPerChannel = 0;
    u32 sampleRef = cPdatItemSize;
    u64 infoSize = 0;
    bool alignsSamples = false;
};

struct PrefetchSource
{
    aal::Bfstm stream;
    u64 sampleSize = 0;
    u64 neededSize = 0;
};

}

static void WriteBlockRef(ByteBuilder* builder, u32 index, u16 typeId, u32 offset, u32 size)
{
    const u64 ref = cHeaderBlockRefs + index * cBlockRefSize;

    builder->putU16(ref, typeId);
    builder->putU32(ref + cReferenceOffset, offset);
    builder->putU32(ref + cBlockRefBlockSize, size);
}

static bool CanCopyInfo(const aal::Bfstm& stream)
{
    return stream.infoBlockSize % cBlockAlignment == 0 && stream.streamInfoStart + cInfoSizeMin <= stream.infoBlockStart + stream.infoBlockSize;
}

static bool ReadPrefetchSource(const void* strmFile, u64 strmFileSize, u32 blocksPerChannel, PrefetchSource* out)
{
    if (!aal::ReadBfstmHead(strmFile, strmFileSize, &out->stream, &out->neededSize))
        return false;

    if (out->neededSize > strmFileSize)
        return true;

    const aal::Bfstm& stream = out->stream;
    const u64 channelCount = stream.channels.size();
    const u64 prefetchSize = static_cast<u64>(blocksPerChannel) * stream.oneBlockBytes * channelCount;

    out->sampleSize = std::min(prefetchSize, stream.getChannelBytes() * channelCount);
    out->neededSize = stream.sampleDataStart + out->sampleSize;

    return out->sampleSize != 0 && out->sampleSize <= cInvalidOffset && CanCopyInfo(stream);
}

static PrefetchShape MakeArchiveShape(u32 streamBufferTimes)
{
    PrefetchShape shape;
    shape.blocksPerChannel = BfstpFile::cBlocksPerBufferTime * streamBufferTimes;

    return shape;
}

static bool BuildWithShape(std::vector<u8>* out, const void* strmFile, u64 strmFileSize, const PrefetchShape& shape)
{
    const u8* file = static_cast<const u8*>(strmFile);
    PrefetchSource source;

    out->clear();

    if (!ReadPrefetchSource(strmFile, strmFileSize, shape.blocksPerChannel, &source) || source.neededSize > strmFileSize)
        return false;

    const aal::Bfstm& stream = source.stream;
    const u32 infoOffset = cFileHeaderSize;
    const u32 infoSize = stream.infoBlockSize;
    const u32 pdatOffset = infoOffset + infoSize;
    const bool movesSamples = shape.alignsSamples && shape.infoSize != infoSize;
    const u32 sampleRef = movesSamples ? sead::Mathu::roundUpPow2(pdatOffset + cPdatHeadSize, cSampleAlignment) - pdatOffset - cPdatItem : shape.sampleRef;
    const u32 sampleOffset = pdatOffset + cPdatItem + sampleRef;
    const u32 sampleSize = static_cast<u32>(source.sampleSize);
    const u32 streamInfoOffset = infoOffset + static_cast<u32>(stream.streamInfoStart - stream.infoBlockStart);

    ByteBuilder builder(stream.isBigEndian);
    builder.resize(static_cast<u64>(sampleOffset) + sampleSize);

    builder.putBytes(0, GetInnerFileMagic(ArchiveFormat::BFSAR, InnerFileKind::StreamPrefetch), cInnerFileMagicLength);
    builder.putBytes(cHeaderByteOrder, file + cHeaderByteOrder, sizeof(u16));
    builder.putU16(cHeaderSize, cFileHeaderSize);
    builder.putU32(cHeaderVersion, shape.version);
    builder.putU32(cHeaderFileSize, static_cast<u32>(builder.size()));
    builder.putU16(cHeaderBlockCount, cPrefetchBlockCount);
    WriteBlockRef(&builder, 0, nw::snd::internal::ElementType_StreamSoundFile_InfoBlock, infoOffset, infoSize);
    WriteBlockRef(&builder, 1, nw::snd::internal::ElementType_StreamSoundFile_PrefetchDataBlock, pdatOffset, sampleOffset - pdatOffset + sampleSize);

    builder.putBytes(infoOffset, file + stream.infoBlockStart, infoSize);
    builder.putU32(streamInfoOffset + cInfoSampleOffset, sampleOffset);
    builder.putU32(streamInfoOffset + cInfoRegionOffset, cInvalidOffset);

    builder.putBytes(pdatOffset, "PDAT", cInnerFileMagicLength);
    builder.putU32(pdatOffset + sizeof(u32), sampleOffset - pdatOffset + sampleSize);
    builder.putU32(pdatOffset + cPdatCount, 1);
    builder.putU32(pdatOffset + cPdatItem + cPdatItemPrefetchSize, sampleSize);
    builder.putU32(pdatOffset + cPdatItem + cPdatItemSampleRef + cReferenceOffset, sampleRef);
    builder.putBytes(sampleOffset, file + stream.sampleDataStart, sampleSize);

    *out = builder.take();
    return true;
}

bool BfstpFile::Build(std::vector<u8>* out, const void* strmFile, u64 strmFileSize, u32 streamBufferTimes)
{
    return BuildWithShape(out, strmFile, strmFileSize, MakeArchiveShape(streamBufferTimes));
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

        if (!ReadPrefetchSource(strmFile.data(), strmFile.size(), MakeArchiveShape(streamBufferTimes).blocksPerChannel, &source))
            return false;

        if (source.neededSize <= strmFile.size())
            return Build(out, strmFile.data(), strmFile.size(), streamBufferTimes);

        if (strmFile.size() < readSize)
            return false;

        readSize = source.neededSize;
    }

    return false;
}

bool BfstpFile::CheckLayout(const void* data, u64 size)
{
    aal::Bfstm prefetch;

    return aal::ReadBfstpInfo(data, size, &prefetch);
}

static bool ReadPrefetchShape(const void* data, u64 size, PrefetchShape* out)
{
    aal::Bfstm prefetch;

    if (!aal::ReadBfstpInfo(data, size, &prefetch) || prefetch.blockCount == 0)
        return false;

    const u64 channelCount = prefetch.channels.size();
    const u64 blockBytes = channelCount * prefetch.oneBlockBytes;
    const u64 streamBytes = channelCount * prefetch.getChannelBytes();
    const u32 measuredBlocks = static_cast<u32>((prefetch.prefetchDataSize + blockBytes - 1) / blockBytes);
    const bool holdsWholeStream = prefetch.prefetchDataSize >= streamBytes;

    out->version = prefetch.version;
    out->sampleRef = static_cast<u32>(prefetch.prefetchDataStart - prefetch.prefetchBlockStart - cPdatItem);
    out->blocksPerChannel = holdsWholeStream ? std::max(measuredBlocks, cAalPrefetchBlocks) : measuredBlocks;
    out->infoSize = prefetch.infoBlockSize;
    out->alignsSamples = prefetch.prefetchDataStart % cSampleAlignment == 0;

    return true;
}

bool BfstpFile::Rebuild(std::vector<u8>* out, const void* strmFile, u64 strmFileSize, const void* prefetch, u64 prefetchSize)
{
    PrefetchShape shape;

    out->clear();

    return ReadPrefetchShape(prefetch, prefetchSize, &shape) && BuildWithShape(out, strmFile, strmFileSize, shape);
}

bool BfstpFile::BuildForResource(std::vector<u8>* out, const void* strmFile, u64 strmFileSize, const void* samplePrefetch, u64 samplePrefetchSize)
{
    PrefetchShape shape;
    shape.version = cAalPrefetchVersion;
    shape.blocksPerChannel = cAalPrefetchBlocks;
    shape.alignsSamples = true;

    PrefetchShape sample;

    if (samplePrefetch && ReadPrefetchShape(samplePrefetch, samplePrefetchSize, &sample))
    {
        shape.version = sample.version;
        shape.blocksPerChannel = sample.blocksPerChannel;
    }

    out->clear();

    return BuildWithShape(out, strmFile, strmFileSize, shape);
}

u32 BfstpFile::doWrite(sead::FileHandle* handle, sead::WriteStream* stream, bool isLast) const
{
    stream->writeMemBlock(mBytes.data(), static_cast<u32>(mBytes.size()));

    return static_cast<u32>(mBytes.size());
}
