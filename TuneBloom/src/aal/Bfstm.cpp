#include <aal/Bfstm.h>

#include <bfsar/files/BfstmFile.h>
#include <bfsar/model/ArchiveFormat.h>
#include <core/ByteView.h>
#include <core/NwFile.h>
#include <ui/Messages.h>

#include <libs/dsp/dsp.h>

#include <math/seadMathCalcCommon.h>

#include <algorithm>
#include <cstring>

namespace aal
{

static constexpr u32 cHeaderSize             = 0x40;
static constexpr u32 cBlockAlignment         = 0x20;
static constexpr u32 cLastBlockAlignmentWide = 0x40;

static constexpr u16 cInfoBlockType          = 0x4000;
static constexpr u16 cSeekBlockType          = 0x4001;
static constexpr u16 cDataBlockType          = 0x4002;
static constexpr u16 cPrefetchBlockType      = 0x4004;
static constexpr u16 cStreamInfoType         = 0x4100;
static constexpr u16 cReferenceTableType     = 0x0101;
static constexpr u16 cChannelInfoType        = 0x4102;
static constexpr u16 cAdpcmInfoType          = 0x0300;

static constexpr u32 cInfoStreamInfoRef      = 0x00;
static constexpr u32 cInfoChannelTableRef    = 0x10;
static constexpr u32 cInfoBodySize           = 0x18;

static constexpr u32 cStreamEncoding         = 0x00;
static constexpr u32 cStreamIsLoop           = 0x01;
static constexpr u32 cStreamChannelCount     = 0x02;
static constexpr u32 cStreamSampleRate       = 0x04;
static constexpr u32 cStreamLoopStart        = 0x08;
static constexpr u32 cStreamFrameCount       = 0x0C;
static constexpr u32 cStreamBlockCount       = 0x10;
static constexpr u32 cStreamOneBlockBytes    = 0x14;
static constexpr u32 cStreamOneBlockSamples  = 0x18;
static constexpr u32 cStreamLastBlockBytes   = 0x1C;
static constexpr u32 cStreamLastBlockPadded  = 0x24;
static constexpr u32 cStreamSampleDataRef    = 0x30;
static constexpr u32 cStreamOriginalStart    = 0x44;
static constexpr u32 cStreamOriginalEnd      = 0x48;
static constexpr u32 cStreamInfoSizeBase     = 0x38;
static constexpr u32 cStreamInfoSizeOriginal = 0x4C;

static constexpr u32 cAdpcmCoefficients      = 0x00;
static constexpr u32 cAdpcmPredScale         = 0x20;
static constexpr u32 cAdpcmYn1               = 0x22;
static constexpr u32 cAdpcmYn2               = 0x24;
static constexpr u32 cAdpcmLoopPredScale     = 0x26;
static constexpr u32 cAdpcmLoopYn1           = 0x28;
static constexpr u32 cAdpcmLoopYn2           = 0x2A;
static constexpr u32 cAdpcmInfoSize          = 0x2C;

static constexpr u32 cPdatCount              = 0x08;
static constexpr u32 cPdatItem               = 0x0C;
static constexpr u32 cPdatItemPrefetchSize   = 0x04;
static constexpr u32 cPdatItemSampleRef      = 0x0C;
static constexpr u32 cPdatHeadSize           = 0x20;

static constexpr u32 cSeekEntrySize          = 2 * sizeof(s16);
static constexpr u32 cAdpcmFrameSamples      = 14;

static constexpr char cInfoMagic[] = "INFO";

struct BlockLayout
{
    u32 blockCount = 0;
    u32 lastBlockBytes = 0;
};

static u64 GetSampleBytes(BfstmEncoding encoding, u32 sampleCount)
{
    switch (encoding)
    {
        case BfstmEncoding::Pcm8:
            return sampleCount;

        case BfstmEncoding::Pcm16:
            return static_cast<u64>(sampleCount) * sizeof(s16);

        default:
            break;
    }

    return getBytesForAdpcmSamples(sampleCount);
}

static BlockLayout CalcBlockLayout(const Bfstm& bfstm)
{
    BlockLayout layout;

    if (bfstm.oneBlockSamples == 0 || bfstm.frameCount == 0)
        return layout;

    layout.blockCount = (bfstm.frameCount + bfstm.oneBlockSamples - 1) / bfstm.oneBlockSamples;
    layout.lastBlockBytes = static_cast<u32>(GetSampleBytes(bfstm.encoding, bfstm.frameCount - (layout.blockCount - 1) * bfstm.oneBlockSamples));

    return layout;
}

static bool IsLastBlockPadding(u32 lastBlockBytes, u32 paddedBytes)
{
    return paddedBytes == sead::Mathu::roundUpPow2(lastBlockBytes, cBlockAlignment) || paddedBytes == sead::Mathu::roundUpPow2(lastBlockBytes, cLastBlockAlignmentWide);
}

static u64 GetBlockRefsEnd(const ByteView& view)
{
    return nwfile::cHeaderBlockRefs + static_cast<u64>(view.readU16(nwfile::cHeaderBlockCount)) * nwfile::cBlockRefSize;
}

static bool FindReadableBlock(const ByteView& view, u16 type, nwfile::Block* out)
{
    return nwfile::FindBlock(view, type, out) && view.has(out->offset, out->size);
}

static bool ReadReference(const ByteView& view, u64 offset, u16 type, u64 base, u64 limit, u64* out)
{
    if (!view.has(offset, nwfile::cReferenceSize) || view.readU16(offset) != type)
        return false;

    const u32 relative = view.readU32(offset + nwfile::cReferenceOffset);
    *out = base + relative;

    return relative != nwfile::cNullOffset && *out < limit;
}

static bool ReadAdpcmInfos(const ByteView& view, u64 infoBody, u64 infoEnd, Bfstm* out)
{
    u64 table = 0;

    if (!ReadReference(view, infoBody + cInfoChannelTableRef, cReferenceTableType, infoBody, infoEnd, &table) || !view.has(table, sizeof(u32)))
        return false;

    const u32 count = view.readU32(table);

    if (count != out->channels.size() || !view.has(table + sizeof(u32), static_cast<u64>(count) * nwfile::cReferenceSize))
        return false;

    for (u32 i = 0; i < count; i++)
    {
        u64 channelInfo = 0;
        u64 adpcmInfo = 0;

        if (!ReadReference(view, table + sizeof(u32) + i * nwfile::cReferenceSize, cChannelInfoType, table, infoEnd, &channelInfo) ||
            !ReadReference(view, channelInfo, cAdpcmInfoType, channelInfo, infoEnd, &adpcmInfo) || adpcmInfo + cAdpcmInfoSize > infoEnd)
            return false;

        BfstmChannel& channel = out->channels[i];

        for (u32 j = 0; j < cBfstmCoefficientCount; j++)
            channel.coefficients[j] = view.readS16(adpcmInfo + cAdpcmCoefficients + j * sizeof(s16));

        channel.predScale = view.readU16(adpcmInfo + cAdpcmPredScale);
        channel.yn1 = view.readS16(adpcmInfo + cAdpcmYn1);
        channel.yn2 = view.readS16(adpcmInfo + cAdpcmYn2);
        channel.loopPredScale = view.readU16(adpcmInfo + cAdpcmLoopPredScale);
        channel.loopYn1 = view.readS16(adpcmInfo + cAdpcmLoopYn1);
        channel.loopYn2 = view.readS16(adpcmInfo + cAdpcmLoopYn2);
    }

    return true;
}

static bool ReadStreamInfo(const ByteView& view, ArchiveFormat format, const nwfile::Block& info, Bfstm* out)
{
    const u64 infoBody = info.offset + nwfile::cBlockHeaderSize;
    const u64 infoEnd = info.offset + info.size;
    u64 streamInfo = 0;

    if (info.size < nwfile::cBlockHeaderSize + cInfoBodySize || !ReadReference(view, infoBody + cInfoStreamInfoRef, cStreamInfoType, infoBody, infoEnd, &streamInfo))
        return false;

    out->streamInfoStart = streamInfo;
    out->hasOriginalLoop = BfstmFile::IsOriginalLoopAvailable(out->version, format);

    if (streamInfo + (out->hasOriginalLoop ? cStreamInfoSizeOriginal : cStreamInfoSizeBase) > infoEnd)
        return false;

    const u8 encoding = view.readU8(streamInfo + cStreamEncoding);

    if (encoding > static_cast<u8>(BfstmEncoding::DspAdpcm))
        return false;

    out->encoding = static_cast<BfstmEncoding>(encoding);
    out->isLoop = view.readU8(streamInfo + cStreamIsLoop) != 0;
    out->channels.resize(view.readU8(streamInfo + cStreamChannelCount));
    out->sampleRate = view.readU32(streamInfo + cStreamSampleRate);
    out->loopStartFrame = view.readU32(streamInfo + cStreamLoopStart);
    out->frameCount = view.readU32(streamInfo + cStreamFrameCount);
    out->blockCount = view.readU32(streamInfo + cStreamBlockCount);
    out->oneBlockBytes = view.readU32(streamInfo + cStreamOneBlockBytes);
    out->oneBlockSamples = view.readU32(streamInfo + cStreamOneBlockSamples);
    out->lastBlockPaddedBytes = view.readU32(streamInfo + cStreamLastBlockPadded);
    out->sampleDataOffset = view.readU32(streamInfo + cStreamSampleDataRef + nwfile::cReferenceOffset);
    out->originalLoopStartFrame = out->hasOriginalLoop ? view.readU32(streamInfo + cStreamOriginalStart) : out->loopStartFrame;
    out->originalLoopEndFrame = out->hasOriginalLoop ? view.readU32(streamInfo + cStreamOriginalEnd) : out->frameCount;

    const BlockLayout layout = CalcBlockLayout(*out);
    const u32 lastBlockBytes = view.readU32(streamInfo + cStreamLastBlockBytes);

    const bool hasWholeFrames = out->encoding != BfstmEncoding::DspAdpcm || out->oneBlockSamples % cAdpcmFrameSamples == 0;

    if (out->channels.empty() || out->sampleRate == 0 || out->oneBlockBytes == 0 || out->oneBlockBytes % cBlockAlignment != 0 || !hasWholeFrames ||
        layout.blockCount != out->blockCount || out->oneBlockBytes != GetSampleBytes(out->encoding, out->oneBlockSamples) ||
        lastBlockBytes < layout.lastBlockBytes || !IsLastBlockPadding(lastBlockBytes, out->lastBlockPaddedBytes) ||
        out->originalLoopEndFrame > out->frameCount || out->originalLoopStartFrame > out->originalLoopEndFrame || (out->isLoop && out->loopStartFrame >= out->frameCount))
        return false;

    return out->encoding != BfstmEncoding::DspAdpcm || ReadAdpcmInfos(view, infoBody, infoEnd, out);
}

static u32 FindTailBlockBytes(const Bfstm& bfstm, ArchiveFormat format, u64 dataEnd)
{
    const u64 channelCount = bfstm.channels.size();
    const u64 blocksEnd = bfstm.sampleDataStart + bfstm.getChannelBytes() * channelCount;

    if (format != ArchiveFormat::BCSAR || bfstm.lastBlockPaddedBytes != bfstm.oneBlockBytes || dataEnd <= blocksEnd)
        return 0;

    const u64 tailBytes = (dataEnd - blocksEnd) / channelCount;

    if (tailBytes * channelCount != dataEnd - blocksEnd || tailBytes % cBlockAlignment != 0 || tailBytes >= bfstm.oneBlockBytes)
        return 0;

    return static_cast<u32>(tailBytes);
}

static bool ReadSampleLayout(const ByteView& view, ArchiveFormat format, Bfstm* out)
{
    nwfile::Block data;

    if (out->sampleDataOffset == nwfile::cNullOffset || !nwfile::FindBlock(view, cDataBlockType, &data))
        return false;

    out->sampleDataStart = data.offset + nwfile::cBlockHeaderSize + out->sampleDataOffset;

    if (out->sampleDataStart + out->getChannelBytes() * out->channels.size() > data.offset + data.size)
        return false;

    out->tailBlockBytes = FindTailBlockBytes(*out, format, data.offset + data.size);
    return true;
}

static bool ReadSeekLayout(const ByteView& view, Bfstm* out)
{
    nwfile::Block seek;

    if (out->encoding != BfstmEncoding::DspAdpcm || !FindReadableBlock(view, cSeekBlockType, &seek))
        return true;

    const u64 rowBytes = static_cast<u64>(out->channels.size()) * cSeekEntrySize;

    out->seekDataStart = seek.offset + nwfile::cBlockHeaderSize;
    out->seekRowCount = static_cast<u32>(std::max<u64>(out->blockCount, (seek.size - nwfile::cBlockHeaderSize) / rowBytes));

    return view.has(out->seekDataStart, out->seekRowCount * rowBytes);
}

static ADPCMINFO MakeAdpcmInfo(const BfstmChannel& channel)
{
    ADPCMINFO info = {};

    for (u32 i = 0; i < cBfstmCoefficientCount; i++)
        info.coef[i] = channel.coefficients[i];

    info.yn1 = channel.yn1;
    info.yn2 = channel.yn2;

    return info;
}

static const InnerFileFormatInfo* FindFormat(const void* data, u64 size, InnerFileKind kind)
{
    if (!data || size < cHeaderSize || !ByteView::IsByteOrderMark(data, size, nwfile::cHeaderByteOrder))
        return nullptr;

    return FindInnerFileFormat(data, kind);
}

static ByteView MakeView(const void* data, u64 size)
{
    return ByteView(data, size, ByteView::IsBigEndianMark(data, size, nwfile::cHeaderByteOrder));
}

static bool ReadInfoBlock(const ByteView& view, ArchiveFormat format, const nwfile::Block& info, Bfstm* out)
{
    out->isBigEndian = view.isBigEndian();
    out->version = view.readU32(nwfile::cHeaderVersion);
    out->infoBlockStart = info.offset;
    out->infoBlockSize = static_cast<u32>(info.size);

    return std::memcmp(view.at(info.offset), cInfoMagic, sizeof(u32)) == 0 && ReadStreamInfo(view, format, info, out);
}

static void ReadPrefetchData(const ByteView& view, Bfstm* out)
{
    nwfile::Block pdat;

    if (!FindReadableBlock(view, cPrefetchBlockType, &pdat) || pdat.size < cPdatHeadSize || view.readU32(pdat.offset + cPdatCount) == 0)
        return;

    const u64 item = pdat.offset + cPdatItem;
    const u32 sampleRef = view.readU32(item + cPdatItemSampleRef + nwfile::cReferenceOffset);
    const u64 dataStart = item + sampleRef;
    const u64 dataSize = view.readU32(item + cPdatItemPrefetchSize);
    const u64 blockEnd = pdat.offset + pdat.size;

    if (sampleRef == nwfile::cNullOffset || dataSize == 0 || dataStart < pdat.offset + cPdatHeadSize || dataStart > blockEnd || dataSize > blockEnd - dataStart)
        return;

    const u64 blockBytes = static_cast<u64>(out->oneBlockBytes) * out->channels.size();
    const u64 blocks = (dataSize + blockBytes - 1) / blockBytes;

    out->prefetchBlockStart = pdat.offset;
    out->prefetchDataStart = dataStart;
    out->prefetchDataSize = static_cast<u32>(dataSize);
    out->prefetchFrameCount = static_cast<u32>(std::min<u64>(out->frameCount, blocks * out->oneBlockSamples));
}

static bool ReadFileInfo(const void* data, u64 size, const InnerFileFormatInfo* format, Bfstm* out)
{
    if (!format)
        return false;

    const ByteView view = MakeView(data, size);
    nwfile::Block info;

    if (view.readU32(nwfile::cHeaderFileSize) > size || !FindReadableBlock(view, cInfoBlockType, &info) || !ReadInfoBlock(view, format->format, info, out))
        return false;

    ReadPrefetchData(view, out);
    return true;
}

bool IsBfstm(const void* data, u64 size)
{
    return FindFormat(data, size, InnerFileKind::Stream) != nullptr;
}

bool ReadBfstmInfo(const void* data, u64 size, Bfstm* out)
{
    const InnerFileFormatInfo* format = FindFormat(data, size, InnerFileKind::Stream);
    Bfstm bfstm;

    if (!ReadFileInfo(data, size, format ? format : FindFormat(data, size, InnerFileKind::StreamPrefetch), &bfstm))
        return false;

    *out = std::move(bfstm);
    return true;
}

bool ReadBfstpInfo(const void* data, u64 size, Bfstm* out)
{
    Bfstm bfstm;

    if (!ReadFileInfo(data, size, FindFormat(data, size, InnerFileKind::StreamPrefetch), &bfstm) || bfstm.prefetchDataSize == 0)
        return false;

    *out = std::move(bfstm);
    return true;
}

bool ReadBfstmHead(const void* data, u64 size, Bfstm* out, u64* outNeededSize)
{
    const InnerFileFormatInfo* format = FindFormat(data, size, InnerFileKind::Stream);

    if (!format)
        return false;

    const ByteView view = MakeView(data, size);
    nwfile::Block info;

    *outNeededSize = GetBlockRefsEnd(view);

    if (*outNeededSize > size)
        return true;

    if (!nwfile::FindBlock(view, cInfoBlockType, &info))
        return false;

    *outNeededSize = info.offset + info.size;

    if (*outNeededSize > size)
        return true;

    Bfstm bfstm;

    if (!ReadInfoBlock(view, format->format, info, &bfstm) || !ReadSampleLayout(view, format->format, &bfstm))
        return false;

    *out = std::move(bfstm);
    return true;
}

bool ReadBfstmLayout(const void* data, u64 size, Bfstm* out)
{
    const InnerFileFormatInfo* format = FindFormat(data, size, InnerFileKind::Stream);
    Bfstm bfstm;

    if (!ReadFileInfo(data, size, format, &bfstm))
        return false;

    const ByteView view = MakeView(data, size);

    if (!ReadSampleLayout(view, format->format, &bfstm) || !view.has(bfstm.sampleDataStart, bfstm.getChannelBytes() * bfstm.channels.size()) || !ReadSeekLayout(view, &bfstm))
        return false;

    *out = std::move(bfstm);
    return true;
}

void CopyBfstmChannel(const Bfstm& bfstm, const void* file, u32 channelIndex, u8* out)
{
    const u8* samples = static_cast<const u8*>(file) + bfstm.sampleDataStart;
    const u64 channelCount = bfstm.channels.size();

    for (u32 block = 0; block < bfstm.blockCount; block++)
    {
        const u64 blockBytes = block + 1 == bfstm.blockCount ? bfstm.lastBlockPaddedBytes : bfstm.oneBlockBytes;
        const u64 blockStart = static_cast<u64>(block) * bfstm.oneBlockBytes * channelCount;

        std::memcpy(out + static_cast<u64>(block) * bfstm.oneBlockBytes, samples + blockStart + channelIndex * blockBytes, blockBytes);
    }

    if (bfstm.tailBlockBytes == 0)
        return;

    const u64 blocksBytes = bfstm.getChannelBytes() - bfstm.tailBlockBytes;

    std::memcpy(out + blocksBytes, samples + blocksBytes * channelCount + channelIndex * bfstm.tailBlockBytes, bfstm.tailBlockBytes);
}

bool ReadBfstm(const void* data, u64 size, Bfstm* out, const char** outError)
{
    *outError = messages::resource::cBfstmDamaged;

    Bfstm bfstm;

    if (!ReadBfstmLayout(data, size, &bfstm))
        return false;

    for (u32 i = 0; i < bfstm.channels.size(); i++)
    {
        bfstm.channels[i].data.resize(bfstm.getChannelBytes());
        CopyBfstmChannel(bfstm, data, i, bfstm.channels[i].data.data());
    }

    *out = std::move(bfstm);
    *outError = nullptr;

    return true;
}

void DecodeBfstmChannel(const Bfstm& bfstm, u32 channelIndex, std::vector<s16>* out)
{
    const BfstmChannel& channel = bfstm.channels[channelIndex];
    const u32 sampleCount = GetSampleBytes(bfstm.encoding, bfstm.frameCount) <= channel.data.size() ? bfstm.frameCount : 0;

    out->assign(sampleCount, 0);

    if (bfstm.encoding == BfstmEncoding::DspAdpcm)
    {
        ADPCMINFO info = MakeAdpcmInfo(channel);
        decode(const_cast<u8*>(channel.data.data()), out->data(), &info, sampleCount);
        return;
    }

    const ByteView view(channel.data.data(), channel.data.size(), bfstm.isBigEndian);

    for (u32 i = 0; i < sampleCount; i++)
        (*out)[i] = bfstm.encoding == BfstmEncoding::Pcm16 ? view.readS16(i * sizeof(s16)) : static_cast<s16>(static_cast<s8>(view.readU8(i)) * 256);
}

}
