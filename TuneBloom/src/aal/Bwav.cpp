#include <aal/Bwav.h>

#include <core/ByteBuilder.h>
#include <core/ByteView.h>
#include <ui/Messages.h>

#include <libs/dsp/dsp.h>

#include <codec/seadHashCRC32.h>
#include <math/seadMathCalcCommon.h>

#include <algorithm>
#include <cstring>

namespace aal
{

static constexpr u32 cHeaderSize          = 0x10;
static constexpr u32 cHeaderByteOrder     = 0x04;
static constexpr u32 cHeaderVersion       = 0x06;
static constexpr u32 cHeaderSampleCrc     = 0x08;
static constexpr u32 cHeaderPrefetch      = 0x0C;
static constexpr u32 cHeaderChannelCount  = 0x0E;
static constexpr u32 cChannelInfoSize     = 0x4C;
static constexpr u32 cChannelCodec        = 0x00;
static constexpr u32 cChannelPan          = 0x02;
static constexpr u32 cChannelSampleRate   = 0x04;
static constexpr u32 cChannelFullSamples  = 0x08;
static constexpr u32 cChannelSamples      = 0x0C;
static constexpr u32 cChannelCoefficients = 0x10;
static constexpr u32 cChannelFullOffset   = 0x30;
static constexpr u32 cChannelDataOffset   = 0x34;
static constexpr u32 cChannelReserved     = 0x38;
static constexpr u32 cChannelLoopEnd      = 0x3C;
static constexpr u32 cChannelLoopStart    = 0x40;
static constexpr u32 cChannelPredScale    = 0x44;
static constexpr u32 cChannelYn1          = 0x46;
static constexpr u32 cChannelYn2          = 0x48;
static constexpr u32 cChannelPadding      = 0x4A;
static constexpr u32 cSampleAlignment     = 0x40;
static constexpr u32 cAdpcmFrameBytes     = 8;
static constexpr u32 cAdpcmFrameSamples   = 14;
static constexpr u32 cSupportedVersion    = 1;
static constexpr u16 cByteOrderMark       = 0xFEFF;
static constexpr u32 cStereoChannelCount  = 2;

static constexpr char cBwavMagic[] = "BWAV";

static u64 GetExactSampleBytes(BwavCodec codec, u32 sampleCount)
{
    if (codec == BwavCodec::Pcm16)
        return static_cast<u64>(sampleCount) * sizeof(s16);

    return getBytesForAdpcmSamples(sampleCount);
}

static u64 GetSampleBytes(BwavCodec codec, u32 sampleCount)
{
    if (codec == BwavCodec::Pcm16)
        return static_cast<u64>(sampleCount) * sizeof(s16);

    return (static_cast<u64>(sampleCount) + cAdpcmFrameSamples - 1) / cAdpcmFrameSamples * cAdpcmFrameBytes;
}

static u32 GetStoredSampleCount(const BwavChannel& channel)
{
    const u64 bytes = channel.data.size();
    u64 available = bytes / sizeof(s16);

    if (channel.codec == BwavCodec::DspAdpcm)
    {
        const u64 partialBytes = bytes % cAdpcmFrameBytes;
        available = bytes / cAdpcmFrameBytes * cAdpcmFrameSamples + (partialBytes > 1 ? (partialBytes - 1) * 2 : 0);
    }

    return static_cast<u32>(std::min<u64>(channel.sampleCount, available));
}

static bool IsSupportedCodec(u16 codec)
{
    return codec == static_cast<u16>(BwavCodec::Pcm16) || codec == static_cast<u16>(BwavCodec::DspAdpcm);
}

static u64 GetChannelInfoOffset(u32 channelIndex)
{
    return cHeaderSize + static_cast<u64>(channelIndex) * cChannelInfoSize;
}

static std::vector<u32> LayoutChannelData(const Bwav& bwav, bool useFullSamples)
{
    std::vector<u32> offsets;
    offsets.reserve(bwav.channels.size());

    u32 offset = sead::Mathu::roundUpPow2(static_cast<u32>(GetChannelInfoOffset(static_cast<u32>(bwav.channels.size()))), cSampleAlignment);

    for (const BwavChannel& channel : bwav.channels)
    {
        offsets.push_back(offset);

        const u32 bytes = static_cast<u32>(useFullSamples ? GetSampleBytes(channel.codec, channel.sampleCount) : channel.data.size());
        offset = sead::Mathu::roundUpPow2(offset + bytes, cSampleAlignment);
    }

    return offsets;
}

static ADPCMINFO MakeAdpcmInfo(const BwavChannel& channel)
{
    ADPCMINFO info = {};

    for (u32 i = 0; i < cBwavCoefficientCount; i++)
        info.coef[i] = channel.coefficients[i];

    return info;
}

static void UpdateLoopContext(BwavChannel* channel)
{
    const u32 contextFrame = channel->loopEndFrame != cBwavNoLoop ? channel->loopStartFrame : 0;
    const u64 frameOffset = contextFrame / cAdpcmFrameSamples * static_cast<u64>(cAdpcmFrameBytes);

    if (channel->codec != BwavCodec::DspAdpcm || frameOffset >= channel->data.size() || contextFrame >= GetStoredSampleCount(*channel))
        return;

    ADPCMINFO info = MakeAdpcmInfo(*channel);
    getLoopContext(channel->data.data(), &info, contextFrame);

    channel->loopPredScale = channel->data[frameOffset];
    channel->loopYn1 = info.loop_yn1;
    channel->loopYn2 = info.loop_yn2;
}

static BwavPan GetEncodePan(u32 channelIndex, u32 channelCount)
{
    if (channelCount != cStereoChannelCount)
        return BwavPan::Center;

    return channelIndex == 0 ? BwavPan::Left : BwavPan::Right;
}

static void ReadChannelInfo(const ByteView& view, u64 offset, BwavChannel* channel)
{
    channel->codec = static_cast<BwavCodec>(view.readU16(offset + cChannelCodec));
    channel->pan = static_cast<BwavPan>(view.readU16(offset + cChannelPan));
    channel->sampleRate = view.readU32(offset + cChannelSampleRate);
    channel->fullSampleCount = view.readU32(offset + cChannelFullSamples);
    channel->sampleCount = view.readU32(offset + cChannelSamples);

    for (u32 i = 0; i < cBwavCoefficientCount; i++)
        channel->coefficients[i] = view.readS16(offset + cChannelCoefficients + i * sizeof(s16));

    channel->fullDataOffset = view.readU32(offset + cChannelFullOffset);
    channel->reserved = view.readU32(offset + cChannelReserved);
    channel->loopEndFrame = view.readU32(offset + cChannelLoopEnd);
    channel->loopStartFrame = view.readU32(offset + cChannelLoopStart);
    channel->loopPredScale = view.readU16(offset + cChannelPredScale);
    channel->loopYn1 = view.readS16(offset + cChannelYn1);
    channel->loopYn2 = view.readS16(offset + cChannelYn2);
    channel->padding = view.readU16(offset + cChannelPadding);
}

static void WriteChannelInfo(ByteBuilder* builder, const BwavChannel& channel, u32 fullDataOffset, u32 dataOffset)
{
    builder->appendU16(static_cast<u16>(channel.codec));
    builder->appendU16(static_cast<u16>(channel.pan));
    builder->appendU32(channel.sampleRate);
    builder->appendU32(channel.fullSampleCount);
    builder->appendU32(channel.sampleCount);

    for (s16 coefficient : channel.coefficients)
        builder->appendU16(static_cast<u16>(coefficient));

    builder->appendU32(fullDataOffset);
    builder->appendU32(dataOffset);
    builder->appendU32(channel.reserved);
    builder->appendU32(channel.loopEndFrame);
    builder->appendU32(channel.loopStartFrame);
    builder->appendU16(channel.loopPredScale);
    builder->appendU16(static_cast<u16>(channel.loopYn1));
    builder->appendU16(static_cast<u16>(channel.loopYn2));
    builder->appendU16(channel.padding);
}

bool IsBwav(const void* data, u64 size)
{
    return data && size >= cHeaderSize && std::memcmp(data, cBwavMagic, sizeof(u32)) == 0 && ByteView::IsByteOrderMark(data, size, cHeaderByteOrder);
}

bool ReadBwav(const void* data, u64 size, Bwav* out, const char** outError)
{
    if (!IsBwav(data, size))
    {
        *outError = messages::resource::cBwavDamaged;
        return false;
    }

    const ByteView view(data, size, ByteView::IsBigEndianMark(data, size, cHeaderByteOrder));

    Bwav bwav;
    bwav.isBigEndian = view.isBigEndian();
    bwav.version = view.readU16(cHeaderVersion);
    bwav.sampleCrc = view.readU32(cHeaderSampleCrc);
    bwav.prefetchFlag = view.readU16(cHeaderPrefetch);

    const u32 channelCount = view.readU16(cHeaderChannelCount);

    if (bwav.version != cSupportedVersion)
    {
        *outError = messages::resource::cBwavVersionUnsupported;
        return false;
    }

    if (channelCount == 0 || channelCount > cBwavChannelMax || !view.has(cHeaderSize, GetChannelInfoOffset(channelCount) - cHeaderSize))
    {
        *outError = messages::resource::cBwavDamaged;
        return false;
    }

    bwav.channels.resize(channelCount);
    u64 dataEnd = GetChannelInfoOffset(channelCount);

    for (u32 i = 0; i < channelCount; i++)
    {
        BwavChannel& channel = bwav.channels[i];
        const u64 infoOffset = GetChannelInfoOffset(i);

        if (!IsSupportedCodec(view.readU16(infoOffset + cChannelCodec)))
        {
            *outError = messages::resource::cBwavCodecUnsupported;
            return false;
        }

        ReadChannelInfo(view, infoOffset, &channel);

        const u64 dataOffset = view.readU32(infoOffset + cChannelDataOffset);
        const u64 exactBytes = GetExactSampleBytes(channel.codec, channel.sampleCount);

        if (channel.sampleCount > channel.fullSampleCount)
        {
            *outError = messages::resource::cBwavDamaged;
            return false;
        }

        if (exactBytes == 0)
            continue;

        if (!view.has(dataOffset, exactBytes))
        {
            *outError = messages::resource::cBwavDamaged;
            return false;
        }

        const u64 storedBytes = std::min<u64>(GetSampleBytes(channel.codec, channel.sampleCount), size - dataOffset);

        channel.data.assign(view.at(dataOffset), view.at(dataOffset) + storedBytes);
        dataEnd = std::max(dataEnd, dataOffset + storedBytes);
    }

    bwav.trailing.assign(view.at(dataEnd), view.at(size));

    *out = std::move(bwav);
    return true;
}

std::vector<u8> WriteBwav(const Bwav& bwav)
{
    const std::vector<u32> offsets = LayoutChannelData(bwav, false);

    ByteBuilder builder(bwav.isBigEndian);
    builder.appendBytes(cBwavMagic, sizeof(u32));
    builder.appendU16(cByteOrderMark);
    builder.appendU16(bwav.version);
    builder.appendU32(bwav.sampleCrc);
    builder.appendU16(bwav.prefetchFlag);
    builder.appendU16(static_cast<u16>(bwav.channels.size()));

    for (u32 i = 0; i < bwav.channels.size(); i++)
    {
        const BwavChannel& channel = bwav.channels[i];
        const bool holdsFullData = !bwav.isPrefetch() && channel.sampleCount == channel.fullSampleCount;
        const u32 fullDataOffset = holdsFullData ? offsets[i] : channel.fullDataOffset;

        WriteChannelInfo(&builder, channel, fullDataOffset, offsets[i]);
    }

    for (const BwavChannel& channel : bwav.channels)
    {
        if (channel.data.empty())
            continue;

        builder.alignTo(cSampleAlignment);
        builder.appendBytes(channel.data.data(), channel.data.size());
    }

    builder.appendBytes(bwav.trailing.data(), bwav.trailing.size());

    return builder.take();
}

u64 MeasureBwav(const void* data, u64 size)
{
    if (!IsBwav(data, size))
        return 0;

    const ByteView view(data, size, ByteView::IsBigEndianMark(data, size, cHeaderByteOrder));
    const u32 channelCount = view.readU16(cHeaderChannelCount);

    if (channelCount == 0 || channelCount > cBwavChannelMax || !view.has(cHeaderSize, GetChannelInfoOffset(channelCount) - cHeaderSize))
        return 0;

    u64 end = GetChannelInfoOffset(channelCount);

    for (u32 i = 0; i < channelCount; i++)
    {
        const u64 infoOffset = GetChannelInfoOffset(i);
        const u16 codec = view.readU16(infoOffset + cChannelCodec);

        if (!IsSupportedCodec(codec))
            return 0;

        const u64 dataOffset = view.readU32(infoOffset + cChannelDataOffset);
        const u64 sampleBytes = GetSampleBytes(static_cast<BwavCodec>(codec), view.readU32(infoOffset + cChannelSamples));

        if (sampleBytes == 0)
            continue;

        if (dataOffset > size)
            return 0;

        end = std::max(end, dataOffset + std::min(sampleBytes, size - dataOffset));
    }

    return end;
}

u32 CalcBwavSampleCrc(const Bwav& bwav)
{
    sead::HashCRC32::Context context;
    u32 crc = 0;

    for (const BwavChannel& channel : bwav.channels)
    {
        const u64 bytes = std::min<u64>(GetSampleBytes(channel.codec, channel.sampleCount), channel.data.size());

        crc = sead::HashCRC32::calcHashWithContext(&context, channel.data.data(), static_cast<u32>(bytes));
    }

    return crc;
}

Bwav MakeBwavPrefetch(const Bwav& full, u32 sampleCount, u16 prefetchFlag)
{
    const std::vector<u32> fullOffsets = LayoutChannelData(full, true);

    Bwav prefetch;
    prefetch.isBigEndian = full.isBigEndian;
    prefetch.version = full.version;
    prefetch.sampleCrc = full.sampleCrc;
    prefetch.prefetchFlag = prefetchFlag;
    prefetch.channels.reserve(full.channels.size());

    for (u32 i = 0; i < full.channels.size(); i++)
    {
        const BwavChannel& source = full.channels[i];

        BwavChannel channel = source;
        channel.fullSampleCount = source.sampleCount;
        channel.sampleCount = std::min(source.sampleCount, sampleCount);
        channel.fullDataOffset = fullOffsets[i];

        const u64 bytes = std::min<u64>(GetSampleBytes(source.codec, channel.sampleCount), source.data.size());
        channel.data.assign(source.data.begin(), source.data.begin() + static_cast<s64>(bytes));

        prefetch.channels.push_back(std::move(channel));
    }

    return prefetch;
}

bool IsBwavComplete(const Bwav& bwav)
{
    for (const BwavChannel& channel : bwav.channels)
    {
        if (channel.sampleCount != channel.fullSampleCount)
            return false;
    }

    return !bwav.isPrefetch() && !bwav.channels.empty();
}

bool CanSetBwavLoop(const Bwav& bwav, bool isLoop, u32 loopStartFrame)
{
    for (const BwavChannel& channel : bwav.channels)
    {
        if (isLoop && loopStartFrame >= channel.fullSampleCount)
            return false;

        if (channel.codec == BwavCodec::DspAdpcm && isLoop && loopStartFrame >= GetStoredSampleCount(channel))
            return false;
    }

    return !bwav.channels.empty();
}

void SetBwavLoop(Bwav* bwav, bool isLoop, u32 loopStartFrame)
{
    for (BwavChannel& channel : bwav->channels)
    {
        channel.loopEndFrame = isLoop ? channel.fullSampleCount : cBwavNoLoop;
        channel.loopStartFrame = isLoop ? loopStartFrame : 0;

        UpdateLoopContext(&channel);
    }
}

void DecodeBwavChannel(const Bwav& bwav, u32 channelIndex, std::vector<s16>* out)
{
    const BwavChannel& channel = bwav.channels[channelIndex];
    const u32 sampleCount = GetStoredSampleCount(channel);

    out->assign(sampleCount, 0);

    if (channel.codec == BwavCodec::Pcm16)
    {
        const ByteView view(channel.data.data(), channel.data.size(), bwav.isBigEndian);

        for (u32 i = 0; i < sampleCount; i++)
            (*out)[i] = view.readS16(i * sizeof(s16));

        return;
    }

    ADPCMINFO info = MakeAdpcmInfo(channel);
    decode(const_cast<u8*>(channel.data.data()), out->data(), &info, sampleCount);
}

Bwav EncodeBwav(const std::vector<std::vector<s16>>& channels, u32 sampleRate, bool isLoop, u32 loopStartFrame)
{
    Bwav bwav;
    bwav.channels.resize(channels.size());

    for (u32 i = 0; i < channels.size(); i++)
    {
        std::vector<s16> pcm = channels[i];
        const u32 sampleCount = static_cast<u32>(pcm.size());

        BwavChannel& channel = bwav.channels[i];
        channel.codec = BwavCodec::DspAdpcm;
        channel.pan = GetEncodePan(i, static_cast<u32>(channels.size()));
        channel.sampleRate = sampleRate;
        channel.fullSampleCount = sampleCount;
        channel.sampleCount = sampleCount;
        channel.data.assign(GetSampleBytes(BwavCodec::DspAdpcm, sampleCount), 0);

        ADPCMINFO info = {};
        encode(pcm.data(), channel.data.data(), &info, sampleCount);

        for (u32 j = 0; j < cBwavCoefficientCount; j++)
            channel.coefficients[j] = info.coef[j];
    }

    SetBwavLoop(&bwav, isLoop, loopStartFrame);
    bwav.sampleCrc = CalcBwavSampleCrc(bwav);

    return bwav;
}

}
