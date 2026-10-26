#include <aal/Bfstm.h>

#include <core/ByteBuilder.h>
#include <core/ByteView.h>
#include <ui/Messages.h>

#include <libs/dsp/dsp.h>

#include <math/seadMathCalcCommon.h>

#include <algorithm>
#include <cstring>

namespace aal
{

static constexpr u32 cHeaderByteOrder        = 0x04;
static constexpr u32 cHeaderSizeField        = 0x06;
static constexpr u32 cHeaderVersion          = 0x08;
static constexpr u32 cHeaderFileSize         = 0x0C;
static constexpr u32 cHeaderBlockCount       = 0x10;
static constexpr u32 cHeaderBlockRefs        = 0x14;
static constexpr u32 cHeaderSize             = 0x40;
static constexpr u32 cBlockRefSize           = 0x0C;
static constexpr u32 cBlockRefOffset         = 0x04;
static constexpr u32 cBlockRefSizeField      = 0x08;
static constexpr u32 cBlockHeaderSize        = 0x08;
static constexpr u32 cBlockAlignment         = 0x20;
static constexpr u32 cReferenceSize          = 0x08;
static constexpr u32 cReferenceOffset        = 0x04;
static constexpr u32 cNullOffset             = 0xFFFFFFFF;
static constexpr u16 cByteOrderMark          = 0xFEFF;
static constexpr u16 cWrittenBlockCount      = 3;

static constexpr u16 cInfoBlockType          = 0x4000;
static constexpr u16 cSeekBlockType          = 0x4001;
static constexpr u16 cDataBlockType          = 0x4002;
static constexpr u16 cRegionBlockType        = 0x4003;
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
static constexpr u32 cStreamRegionCount      = 0x03;
static constexpr u32 cStreamSampleRate       = 0x04;
static constexpr u32 cStreamLoopStart        = 0x08;
static constexpr u32 cStreamFrameCount       = 0x0C;
static constexpr u32 cStreamBlockCount       = 0x10;
static constexpr u32 cStreamOneBlockBytes    = 0x14;
static constexpr u32 cStreamOneBlockSamples  = 0x18;
static constexpr u32 cStreamLastBlockBytes   = 0x1C;
static constexpr u32 cStreamLastBlockSamples = 0x20;
static constexpr u32 cStreamLastBlockPadded  = 0x24;
static constexpr u32 cStreamSampleDataRef    = 0x30;
static constexpr u32 cStreamOriginalStart    = 0x44;
static constexpr u32 cStreamOriginalEnd      = 0x48;
static constexpr u32 cStreamInfoSizeBase     = 0x38;
static constexpr u32 cStreamInfoSizeOriginal = 0x4C;
static constexpr u32 cOriginalLoopVersion    = 0x00040000;

static constexpr u32 cAdpcmCoefficients      = 0x00;
static constexpr u32 cAdpcmPredScale         = 0x20;
static constexpr u32 cAdpcmYn1               = 0x22;
static constexpr u32 cAdpcmYn2               = 0x24;
static constexpr u32 cAdpcmLoopPredScale     = 0x26;
static constexpr u32 cAdpcmLoopYn1           = 0x28;
static constexpr u32 cAdpcmLoopYn2           = 0x2A;
static constexpr u32 cAdpcmInfoSize          = 0x2C;

static constexpr u32 cPrefetchSizeField      = 0x10;
static constexpr u32 cAdpcmFrameBytes        = 8;
static constexpr u32 cAdpcmFrameSamples      = 14;

static constexpr char cBfstmMagic[] = "FSTM";
static constexpr char cBfstpMagic[] = "FSTP";
static constexpr char cInfoMagic[]  = "INFO";
static constexpr char cSeekMagic[]  = "SEEK";
static constexpr char cDataMagic[]  = "DATA";

struct FileBlock
{
    u64 offset = 0;
    u64 size = 0;
};

struct BlockLayout
{
    u32 blockCount = 0;
    u32 lastBlockSamples = 0;
    u32 lastBlockBytes = 0;
    u32 lastBlockPaddedBytes = 0;
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
    layout.lastBlockSamples = bfstm.frameCount - (layout.blockCount - 1) * bfstm.oneBlockSamples;
    layout.lastBlockBytes = static_cast<u32>(GetSampleBytes(bfstm.encoding, layout.lastBlockSamples));
    layout.lastBlockPaddedBytes = sead::Mathu::roundUpPow2(layout.lastBlockBytes, cBlockAlignment);

    return layout;
}

static u64 GetChannelBytes(const Bfstm& bfstm, const BlockLayout& layout)
{
    if (layout.blockCount == 0)
        return 0;

    return static_cast<u64>(layout.blockCount - 1) * bfstm.oneBlockBytes + layout.lastBlockPaddedBytes;
}

static bool FindBlock(const ByteView& view, u16 type, FileBlock* out)
{
    const u32 count = view.readU16(cHeaderBlockCount);

    for (u32 i = 0; i < count; i++)
    {
        const u64 ref = cHeaderBlockRefs + static_cast<u64>(i) * cBlockRefSize;

        if (!view.has(ref, cBlockRefSize))
            return false;

        if (view.readU16(ref) != type)
            continue;

        out->offset = view.readU32(ref + cBlockRefOffset);
        out->size = view.readU32(ref + cBlockRefSizeField);

        return out->offset != cNullOffset && out->size >= cBlockHeaderSize && view.has(out->offset, out->size);
    }

    return false;
}

static bool ReadReference(const ByteView& view, u64 offset, u16 type, u64 base, u64 limit, u64* out)
{
    if (!view.has(offset, cReferenceSize) || view.readU16(offset) != type)
        return false;

    const u32 relative = view.readU32(offset + cReferenceOffset);
    *out = base + relative;

    return relative != cNullOffset && *out < limit;
}

static bool ReadAdpcmInfos(const ByteView& view, u64 infoBody, u64 infoEnd, Bfstm* out)
{
    u64 table = 0;

    if (!ReadReference(view, infoBody + cInfoChannelTableRef, cReferenceTableType, infoBody, infoEnd, &table) || !view.has(table, sizeof(u32)))
        return false;

    const u32 count = view.readU32(table);

    if (count != out->channels.size() || !view.has(table + sizeof(u32), static_cast<u64>(count) * cReferenceSize))
        return false;

    for (u32 i = 0; i < count; i++)
    {
        u64 channelInfo = 0;
        u64 adpcmInfo = 0;

        if (!ReadReference(view, table + sizeof(u32) + i * cReferenceSize, cChannelInfoType, table, infoEnd, &channelInfo) ||
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

        out->adpcmInfoOffsets.push_back(static_cast<u32>(adpcmInfo - (infoBody - cBlockHeaderSize)));
    }

    return true;
}

static bool ReadStreamInfo(const ByteView& view, const FileBlock& info, Bfstm* out)
{
    const u64 infoBody = info.offset + cBlockHeaderSize;
    const u64 infoEnd = info.offset + info.size;
    u64 streamInfo = 0;

    if (info.size < cBlockHeaderSize + cInfoBodySize || !ReadReference(view, infoBody + cInfoStreamInfoRef, cStreamInfoType, infoBody, infoEnd, &streamInfo))
        return false;

    out->hasOriginalLoop = out->version >= cOriginalLoopVersion;

    if (streamInfo + (out->hasOriginalLoop ? cStreamInfoSizeOriginal : cStreamInfoSizeBase) > infoEnd)
        return false;

    const u8 encoding = view.readU8(streamInfo + cStreamEncoding);

    if (encoding > static_cast<u8>(BfstmEncoding::DspAdpcm))
        return false;

    out->encoding = static_cast<BfstmEncoding>(encoding);
    out->isLoop = view.readU8(streamInfo + cStreamIsLoop) != 0;
    out->channels.resize(view.readU8(streamInfo + cStreamChannelCount));
    out->hasRegions = view.readU8(streamInfo + cStreamRegionCount) != 0;
    out->sampleRate = view.readU32(streamInfo + cStreamSampleRate);
    out->loopStartFrame = view.readU32(streamInfo + cStreamLoopStart);
    out->frameCount = view.readU32(streamInfo + cStreamFrameCount);
    out->oneBlockBytes = view.readU32(streamInfo + cStreamOneBlockBytes);
    out->oneBlockSamples = view.readU32(streamInfo + cStreamOneBlockSamples);
    out->sampleDataOffset = view.readU32(streamInfo + cStreamSampleDataRef + cReferenceOffset);
    out->originalLoopStartFrame = out->hasOriginalLoop ? view.readU32(streamInfo + cStreamOriginalStart) : out->loopStartFrame;
    out->originalLoopEndFrame = out->hasOriginalLoop ? view.readU32(streamInfo + cStreamOriginalEnd) : out->frameCount;
    out->streamInfoOffset = static_cast<u32>(streamInfo - info.offset);

    const BlockLayout layout = CalcBlockLayout(*out);

    const bool hasWholeFrames = out->encoding != BfstmEncoding::DspAdpcm || out->oneBlockSamples % cAdpcmFrameSamples == 0;

    if (out->channels.empty() || out->sampleRate == 0 || out->oneBlockBytes == 0 || out->oneBlockBytes % cBlockAlignment != 0 || !hasWholeFrames ||
        layout.blockCount != view.readU32(streamInfo + cStreamBlockCount) ||
        out->oneBlockBytes != GetSampleBytes(out->encoding, out->oneBlockSamples) || layout.lastBlockPaddedBytes != view.readU32(streamInfo + cStreamLastBlockPadded) ||
        out->originalLoopEndFrame > out->frameCount || out->originalLoopStartFrame > out->originalLoopEndFrame || (out->isLoop && out->loopStartFrame >= out->frameCount))
        return false;

    return out->encoding != BfstmEncoding::DspAdpcm || ReadAdpcmInfos(view, infoBody, infoEnd, out);
}

static bool ReadChannelData(const ByteView& view, const FileBlock& data, Bfstm* out)
{
    const BlockLayout layout = CalcBlockLayout(*out);
    const u64 channelCount = out->channels.size();
    const u64 samples = data.offset + cBlockHeaderSize + out->sampleDataOffset;
    const u64 interleavedBytes = GetChannelBytes(*out, layout) * channelCount;

    if (out->sampleDataOffset == cNullOffset || samples + interleavedBytes > data.offset + data.size)
        return false;

    for (BfstmChannel& channel : out->channels)
        channel.data.reserve(GetChannelBytes(*out, layout));

    for (u32 block = 0; block < layout.blockCount; block++)
    {
        const bool isLast = block + 1 == layout.blockCount;
        const u64 blockBytes = isLast ? layout.lastBlockPaddedBytes : out->oneBlockBytes;
        const u64 blockStart = samples + static_cast<u64>(block) * out->oneBlockBytes * channelCount;

        for (u32 i = 0; i < channelCount; i++)
        {
            const u8* source = view.at(blockStart + i * blockBytes);
            out->channels[i].data.insert(out->channels[i].data.end(), source, source + blockBytes);
        }
    }

    return true;
}

static bool ReadSeekHistory(const ByteView& view, const FileBlock& seek, Bfstm* out)
{
    const u64 entryCount = static_cast<u64>(CalcBlockLayout(*out).blockCount) * out->channels.size() * 2;

    if (cBlockHeaderSize + entryCount * sizeof(s16) > seek.size)
        return false;

    out->seekHistory.resize(entryCount);

    for (u64 i = 0; i < entryCount; i++)
        out->seekHistory[i] = view.readS16(seek.offset + cBlockHeaderSize + i * sizeof(s16));

    return true;
}

static std::vector<s16> CalcSeekHistory(const Bfstm& bfstm, const BlockLayout& layout)
{
    std::vector<std::vector<s16>> pcm(bfstm.channels.size());
    std::vector<s16> history;

    for (u32 i = 0; i < bfstm.channels.size(); i++)
        DecodeBfstmChannel(bfstm, i, &pcm[i]);

    history.reserve(static_cast<u64>(layout.blockCount) * bfstm.channels.size() * 2);

    for (u32 block = 0; block < layout.blockCount; block++)
    {
        const u64 firstSample = static_cast<u64>(block) * bfstm.oneBlockSamples;

        for (u32 i = 0; i < bfstm.channels.size(); i++)
        {
            const BfstmChannel& channel = bfstm.channels[i];
            const bool hasHistory = firstSample >= 2 && firstSample <= pcm[i].size();

            history.push_back(hasHistory ? pcm[i][firstSample - 1] : channel.yn1);
            history.push_back(hasHistory ? pcm[i][firstSample - 2] : channel.yn2);
        }
    }

    return history;
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

static void UpdateLoopContext(const Bfstm& bfstm, BfstmChannel* channel)
{
    const u32 contextFrame = bfstm.isLoop ? bfstm.loopStartFrame : 0;
    const u64 frameOffset = contextFrame / cAdpcmFrameSamples * static_cast<u64>(cAdpcmFrameBytes);

    if (frameOffset >= channel->data.size())
        return;

    ADPCMINFO info = MakeAdpcmInfo(*channel);
    getLoopContext(channel->data.data(), &info, contextFrame);

    channel->loopPredScale = channel->data[frameOffset];
    channel->loopYn1 = contextFrame != 0 ? info.loop_yn1 : channel->yn1;
    channel->loopYn2 = contextFrame != 0 ? info.loop_yn2 : channel->yn2;
}

static void WriteHeader(ByteBuilder* builder, const Bfstm& bfstm, const FileBlock (&blocks)[cWrittenBlockCount])
{
    static constexpr u16 cBlockTypes[cWrittenBlockCount] = { cInfoBlockType, cSeekBlockType, cDataBlockType };

    builder->putBytes(0, cBfstmMagic, sizeof(u32));
    builder->putU16(cHeaderByteOrder, cByteOrderMark);
    builder->putU16(cHeaderSizeField, cHeaderSize);
    builder->putU32(cHeaderVersion, bfstm.version);
    builder->putU32(cHeaderFileSize, static_cast<u32>(builder->size()));
    builder->putU16(cHeaderBlockCount, cWrittenBlockCount);

    for (u32 i = 0; i < cWrittenBlockCount; i++)
    {
        const u64 ref = cHeaderBlockRefs + i * cBlockRefSize;

        builder->putU16(ref, cBlockTypes[i]);
        builder->putU32(ref + cBlockRefOffset, static_cast<u32>(blocks[i].offset));
        builder->putU32(ref + cBlockRefSizeField, static_cast<u32>(blocks[i].size));
    }
}

static void WriteInfoBlock(ByteBuilder* builder, const Bfstm& bfstm, const BlockLayout& layout)
{
    const u64 info = builder->size();
    const u64 streamInfo = info + bfstm.streamInfoOffset;

    builder->appendBytes(bfstm.info.data(), bfstm.info.size());
    builder->putU8(streamInfo + cStreamIsLoop, bfstm.isLoop ? 1 : 0);
    builder->putU32(streamInfo + cStreamSampleRate, bfstm.sampleRate);
    builder->putU32(streamInfo + cStreamLoopStart, bfstm.loopStartFrame);
    builder->putU32(streamInfo + cStreamFrameCount, bfstm.frameCount);
    builder->putU32(streamInfo + cStreamBlockCount, layout.blockCount);
    builder->putU32(streamInfo + cStreamLastBlockBytes, layout.lastBlockBytes);
    builder->putU32(streamInfo + cStreamLastBlockSamples, layout.lastBlockSamples);
    builder->putU32(streamInfo + cStreamLastBlockPadded, layout.lastBlockPaddedBytes);

    if (bfstm.hasOriginalLoop)
    {
        builder->putU32(streamInfo + cStreamOriginalStart, bfstm.originalLoopStartFrame);
        builder->putU32(streamInfo + cStreamOriginalEnd, bfstm.originalLoopEndFrame);
    }

    for (u32 i = 0; i < bfstm.adpcmInfoOffsets.size(); i++)
    {
        const BfstmChannel& channel = bfstm.channels[i];
        const u64 adpcmInfo = info + bfstm.adpcmInfoOffsets[i];

        for (u32 j = 0; j < cBfstmCoefficientCount; j++)
            builder->putU16(adpcmInfo + cAdpcmCoefficients + j * sizeof(s16), static_cast<u16>(channel.coefficients[j]));

        builder->putU16(adpcmInfo + cAdpcmPredScale, channel.predScale);
        builder->putU16(adpcmInfo + cAdpcmYn1, static_cast<u16>(channel.yn1));
        builder->putU16(adpcmInfo + cAdpcmYn2, static_cast<u16>(channel.yn2));
        builder->putU16(adpcmInfo + cAdpcmLoopPredScale, channel.loopPredScale);
        builder->putU16(adpcmInfo + cAdpcmLoopYn1, static_cast<u16>(channel.loopYn1));
        builder->putU16(adpcmInfo + cAdpcmLoopYn2, static_cast<u16>(channel.loopYn2));
    }
}

static void WriteSeekBlock(ByteBuilder* builder, const Bfstm& bfstm, const BlockLayout& layout)
{
    const u64 seek = builder->size();
    const u64 entryCount = static_cast<u64>(layout.blockCount) * bfstm.channels.size() * 2;
    const std::vector<s16> history = bfstm.seekHistory.size() == entryCount ? bfstm.seekHistory : CalcSeekHistory(bfstm, layout);

    builder->appendBytes(cSeekMagic, sizeof(u32));
    builder->appendU32(0);

    for (s16 value : history)
        builder->appendU16(static_cast<u16>(value));

    builder->alignTo(cBlockAlignment);
    builder->putU32(seek + sizeof(u32), static_cast<u32>(builder->size() - seek));
}

static void WriteDataBlock(ByteBuilder* builder, const Bfstm& bfstm, const BlockLayout& layout)
{
    const u64 data = builder->size();

    builder->appendBytes(cDataMagic, sizeof(u32));
    builder->appendU32(0);
    builder->appendZeros(bfstm.sampleDataOffset);

    for (u32 block = 0; block < layout.blockCount; block++)
    {
        const bool isLast = block + 1 == layout.blockCount;
        const u64 blockBytes = isLast ? layout.lastBlockPaddedBytes : bfstm.oneBlockBytes;
        const u64 sourceOffset = static_cast<u64>(block) * bfstm.oneBlockBytes;

        for (const BfstmChannel& channel : bfstm.channels)
        {
            const u64 available = channel.data.size() > sourceOffset ? std::min<u64>(blockBytes, channel.data.size() - sourceOffset) : 0;

            builder->appendBytes(channel.data.data() + sourceOffset, available);
            builder->appendZeros(blockBytes - available);
        }
    }

    builder->putU32(data + sizeof(u32), static_cast<u32>(builder->size() - data));
}

static bool HasMagic(const void* data, u64 size, const char* magic)
{
    return data && size >= cHeaderSize && std::memcmp(data, magic, sizeof(u32)) == 0 && ByteView::IsByteOrderMark(data, size, cHeaderByteOrder);
}

static bool ReadInfoBlock(const ByteView& view, Bfstm* out)
{
    FileBlock info;
    FileBlock region;

    out->isBigEndian = view.isBigEndian();
    out->version = view.readU32(cHeaderVersion);

    if (view.readU32(cHeaderFileSize) > view.size() || !FindBlock(view, cInfoBlockType, &info) || std::memcmp(view.at(info.offset), cInfoMagic, sizeof(u32)) != 0 ||
        !ReadStreamInfo(view, info, out))
        return false;

    out->hasRegions = out->hasRegions || FindBlock(view, cRegionBlockType, &region);
    out->info.assign(view.at(info.offset), view.at(info.offset) + info.size);

    FileBlock prefetch;

    if (FindBlock(view, cPrefetchBlockType, &prefetch) && prefetch.size >= cPrefetchSizeField + sizeof(u32))
    {
        const u64 blockBytes = static_cast<u64>(out->oneBlockBytes) * out->channels.size();
        const u64 blocks = (view.readU32(prefetch.offset + cPrefetchSizeField) + blockBytes - 1) / blockBytes;

        out->prefetchFrameCount = static_cast<u32>(std::min<u64>(out->frameCount, blocks * out->oneBlockSamples));
    }

    return true;
}

bool IsBfstm(const void* data, u64 size)
{
    return HasMagic(data, size, cBfstmMagic);
}

bool ReadBfstmInfo(const void* data, u64 size, Bfstm* out)
{
    if (!HasMagic(data, size, cBfstmMagic) && !HasMagic(data, size, cBfstpMagic))
        return false;

    Bfstm bfstm;

    if (!ReadInfoBlock(ByteView(data, size, ByteView::IsBigEndianMark(data, size, cHeaderByteOrder)), &bfstm))
        return false;

    *out = std::move(bfstm);
    return true;
}

bool ReadBfstm(const void* data, u64 size, Bfstm* out, const char** outError)
{
    *outError = messages::resource::cBfstmDamaged;

    if (!IsBfstm(data, size))
        return false;

    const ByteView view(data, size, ByteView::IsBigEndianMark(data, size, cHeaderByteOrder));

    Bfstm bfstm;
    FileBlock seek;
    FileBlock samples;

    if (!ReadInfoBlock(view, &bfstm) || !FindBlock(view, cSeekBlockType, &seek) || !FindBlock(view, cDataBlockType, &samples) ||
        !ReadChannelData(view, samples, &bfstm) || !ReadSeekHistory(view, seek, &bfstm))
        return false;

    *out = std::move(bfstm);
    *outError = nullptr;

    return true;
}

bool CanWriteBfstm(const Bfstm& bfstm, const char** outReason)
{
    if (bfstm.encoding != BfstmEncoding::DspAdpcm || bfstm.adpcmInfoOffsets.size() != bfstm.channels.size())
    {
        *outReason = messages::resource::cBfstmNeedsDspAdpcm;
        return false;
    }

    if (bfstm.hasRegions)
    {
        *outReason = messages::resource::cBfstmRegionsUnsupported;
        return false;
    }

    return true;
}

bool WriteBfstm(const Bfstm& bfstm, std::vector<u8>* out)
{
    const char* reason = nullptr;
    const BlockLayout layout = CalcBlockLayout(bfstm);

    if (!CanWriteBfstm(bfstm, &reason) || layout.blockCount == 0 || bfstm.info.empty())
        return false;

    ByteBuilder builder(bfstm.isBigEndian);
    builder.resize(cHeaderSize);

    FileBlock blocks[cWrittenBlockCount];

    blocks[0].offset = builder.size();
    WriteInfoBlock(&builder, bfstm, layout);
    blocks[0].size = builder.size() - blocks[0].offset;

    blocks[1].offset = builder.size();
    WriteSeekBlock(&builder, bfstm, layout);
    blocks[1].size = builder.size() - blocks[1].offset;

    blocks[2].offset = builder.size();
    WriteDataBlock(&builder, bfstm, layout);
    blocks[2].size = builder.size() - blocks[2].offset;

    WriteHeader(&builder, bfstm, blocks);

    *out = builder.take();
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

static std::vector<s16> ExtendForBlockLoop(const std::vector<s16>& pcm, u32 loopStartFrame, u32 extension)
{
    std::vector<s16> extended(pcm);
    const u32 loopLength = static_cast<u32>(pcm.size()) - loopStartFrame;

    extended.reserve(pcm.size() + extension);

    for (u32 i = 0; i < extension; i++)
        extended.push_back(pcm[loopStartFrame + i % loopLength]);

    return extended;
}

bool EncodeBfstm(const Bfstm& shape, const std::vector<std::vector<s16>>& channels, u32 sampleRate, bool isLoop, u32 loopStartFrame, Bfstm* out, const char** outError)
{
    if (!CanWriteBfstm(shape, outError))
        return false;

    if (channels.size() != shape.channels.size())
    {
        *outError = messages::resource::cBfstmChannelCountMismatch;
        return false;
    }

    const u32 sourceSampleCount = static_cast<u32>(channels.front().size());

    if (sourceSampleCount == 0 || (isLoop && loopStartFrame >= sourceSampleCount))
    {
        *outError = messages::wavefile::cInvalidLoopRange;
        return false;
    }

    const u32 alignedLoopStart = isLoop ? (loopStartFrame + shape.oneBlockSamples - 1) / shape.oneBlockSamples * shape.oneBlockSamples : 0;
    const u32 extension = isLoop ? alignedLoopStart - loopStartFrame : 0;

    Bfstm bfstm = shape;
    bfstm.seekHistory.clear();
    bfstm.isLoop = isLoop;
    bfstm.sampleRate = sampleRate;
    bfstm.loopStartFrame = alignedLoopStart;
    bfstm.frameCount = sourceSampleCount + extension;
    bfstm.originalLoopStartFrame = isLoop ? loopStartFrame : 0;
    bfstm.originalLoopEndFrame = sourceSampleCount;

    const u64 channelBytes = GetChannelBytes(bfstm, CalcBlockLayout(bfstm));

    for (u32 i = 0; i < channels.size(); i++)
    {
        std::vector<s16> pcm = ExtendForBlockLoop(channels[i], loopStartFrame, extension);
        pcm.resize(bfstm.frameCount, 0);

        BfstmChannel& channel = bfstm.channels[i];
        channel.data.assign(channelBytes, 0);

        ADPCMINFO info = {};
        encode(pcm.data(), channel.data.data(), &info, bfstm.frameCount);

        for (u32 j = 0; j < cBfstmCoefficientCount; j++)
            channel.coefficients[j] = info.coef[j];

        channel.predScale = channel.data.front();
        channel.yn1 = 0;
        channel.yn2 = 0;

        UpdateLoopContext(bfstm, &channel);
    }

    *out = std::move(bfstm);
    return true;
}

}
