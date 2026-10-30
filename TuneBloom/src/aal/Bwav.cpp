#include <aal/Bwav.h>

#include <core/ByteBuilder.h>
#include <core/ByteView.h>
#include <messages/Messages.h>
#include <bfsar/audio/AudioProcessing.h>
#include <bfsar/audio/NxOpus.h>

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
static constexpr u32 cOpusPrefetchAudio   = 240;

static constexpr u32 cPrefetchSamplesPcm16    = 0x1000;
static constexpr u32 cPrefetchSamplesDspAdpcm = 0x3800;
static constexpr u32 cPrefetchSamplesOpus     = 0x9000;

static constexpr char cBwavMagic[] = "BWAV";

static constexpr BwavPan cSurroundPans[] = { BwavPan::Left, BwavPan::Right, BwavPan::Center, BwavPan::Lfe, BwavPan::RearLeft, BwavPan::RearRight };

struct ChannelDataSize
{
    u64 exact = 0;
    u64 stored = 0;
};

static u64 GetOpusPrefetchSampleCount(u32 sampleCount, bool hasTailField)
{
    return sampleCount == 0 && !hasTailField ? 0 : static_cast<u64>(sampleCount) + cOpusPrefetchAudio;
}

static bool HasOpusTailField(const BwavChannel& channel)
{
    return channel.coefficients[0] != 0 || channel.coefficients[1] != 0;
}

static u64 GetExactSampleBytes(BwavCodec codec, u32 sampleCount)
{
    if (codec != BwavCodec::DspAdpcm)
        return static_cast<u64>(sampleCount) * sizeof(s16);

    return getBytesForAdpcmSamples(sampleCount);
}

static u64 GetSampleBytes(BwavCodec codec, u32 sampleCount, bool hasOpusTailField)
{
    if (codec == BwavCodec::Pcm16)
        return static_cast<u64>(sampleCount) * sizeof(s16);

    if (codec == BwavCodec::Opus)
        return GetOpusPrefetchSampleCount(sampleCount, hasOpusTailField) * sizeof(s16);

    return (static_cast<u64>(sampleCount) + cAdpcmFrameSamples - 1) / cAdpcmFrameSamples * cAdpcmFrameBytes;
}

static bool HoldsOpusPackets(const BwavChannel& channel)
{
    return channel.codec == BwavCodec::Opus && nxopus::HasInfoChunk(channel.data.data(), channel.data.size());
}

static bool MeasureChannelData(const ByteView& view, u64 infoOffset, ChannelDataSize* out)
{
    const BwavCodec codec = static_cast<BwavCodec>(view.readU16(infoOffset + cChannelCodec));
    const u32 sampleCount = view.readU32(infoOffset + cChannelSamples);
    const u64 dataOffset = view.readU32(infoOffset + cChannelDataOffset);

    if (codec == BwavCodec::Opus && sampleCount != 0 && dataOffset < view.size() && nxopus::HasInfoChunk(view.at(dataOffset), view.size() - dataOffset))
    {
        out->exact = nxopus::Measure(view.at(dataOffset), view.size() - dataOffset);
        out->stored = out->exact;

        return out->exact != 0;
    }

    out->exact = GetExactSampleBytes(codec, sampleCount);
    out->stored = GetSampleBytes(codec, sampleCount, view.readU32(infoOffset + cChannelCoefficients) != 0);

    return true;
}

static u32 GetStoredSampleCount(const BwavChannel& channel)
{
    if (HoldsOpusPackets(channel))
        return channel.sampleCount;

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
    return codec == static_cast<u16>(BwavCodec::Pcm16) || codec == static_cast<u16>(BwavCodec::DspAdpcm) || codec == static_cast<u16>(BwavCodec::Opus);
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

        const bool useSampleBytes = useFullSamples && !HoldsOpusPackets(channel);
        const u32 bytes = static_cast<u32>(useSampleBytes ? GetSampleBytes(channel.codec, channel.sampleCount, HasOpusTailField(channel)) : channel.data.size());
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

static void DecodeOpusPackets(const BwavChannel& channel, std::vector<s16>* out)
{
    std::vector<std::vector<s16>> decoded;
    nxopus::Info info;

    if (nxopus::Decode(channel.data.data(), channel.data.size(), nxopus::PreSkip::Keep, &decoded, &info) != nxopus::Error::None)
    {
        out->clear();
        return;
    }

    *out = std::move(decoded.front());
    out->resize(channel.sampleCount, 0);
}

static std::vector<u8> MakeOpusPrefetchData(const BwavChannel& source, u32 sampleCount, bool isBigEndian, u32* outTailAudio)
{
    std::vector<s16> decoded;
    DecodeOpusPackets(source, &decoded);

    const u64 audioCount = std::min<u64>(sampleCount == 0 ? 0 : static_cast<u64>(sampleCount) + cOpusPrefetchAudio, decoded.size());
    const u64 totalCount = GetOpusPrefetchSampleCount(sampleCount, false);

    ByteBuilder builder(isBigEndian);

    for (u64 i = 0; i < totalCount; i++)
        builder.appendU16(static_cast<u16>(i < audioCount ? decoded[i] : 0));

    *outTailAudio = audioCount > sampleCount ? static_cast<u32>(audioCount - sampleCount) : 0;
    return builder.take();
}

static void SetOpusPrefetchTail(BwavChannel* channel, u32 tailAudio, u32 tailOffset, bool isBigEndian)
{
    ByteBuilder builder(isBigEndian);
    builder.appendU32(tailAudio);
    builder.appendU32(tailOffset);

    const std::vector<u8> fields = builder.take();
    const ByteView view(fields.data(), fields.size(), isBigEndian);

    for (u32 i = 0; i < fields.size() / sizeof(s16); i++)
        channel->coefficients[i] = view.readS16(i * sizeof(s16));
}

static BwavPan GetEncodePan(u32 channelIndex, u32 channelCount)
{
    if (channelCount == std::size(cSurroundPans))
        return cSurroundPans[channelIndex];

    if (channelIndex + 1 == channelCount && channelCount % 2 != 0)
        return BwavPan::Center;

    return channelIndex % 2 == 0 ? BwavPan::Left : BwavPan::Right;
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
        ChannelDataSize dataSize;

        if (channel.sampleCount > channel.fullSampleCount || !MeasureChannelData(view, infoOffset, &dataSize))
        {
            *outError = messages::resource::cBwavDamaged;
            return false;
        }

        if (dataSize.exact == 0)
            continue;

        if (!view.has(dataOffset, dataSize.exact))
        {
            *outError = messages::resource::cBwavDamaged;
            return false;
        }

        const u64 storedBytes = std::min<u64>(dataSize.stored, size - dataOffset);

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
        ChannelDataSize dataSize;

        if (!IsSupportedCodec(view.readU16(infoOffset + cChannelCodec)) || !MeasureChannelData(view, infoOffset, &dataSize))
            return 0;

        const u64 dataOffset = view.readU32(infoOffset + cChannelDataOffset);

        if (dataSize.stored == 0)
            continue;

        if (dataOffset > size)
            return 0;

        end = std::max(end, dataOffset + std::min(dataSize.stored, size - dataOffset));
    }

    return end;
}

u32 CalcBwavSampleCrc(const Bwav& bwav)
{
    sead::HashCRC32::Context context;
    u32 crc = 0;

    for (const BwavChannel& channel : bwav.channels)
    {
        const u64 bytes = std::min<u64>(GetSampleBytes(channel.codec, channel.sampleCount, HasOpusTailField(channel)), channel.data.size());

        crc = sead::HashCRC32::calcHashWithContext(&context, channel.data.data(), static_cast<u32>(bytes));
    }

    return crc;
}

u32 GetBwavPrefetchSampleCount(BwavCodec codec)
{
    switch (codec)
    {
        case BwavCodec::Pcm16:
            return cPrefetchSamplesPcm16;

        case BwavCodec::Opus:
            return cPrefetchSamplesOpus;

        default:
            break;
    }

    return cPrefetchSamplesDspAdpcm;
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

    std::vector<u32> opusTailAudio(full.channels.size(), 0);

    for (u32 i = 0; i < full.channels.size(); i++)
    {
        const BwavChannel& source = full.channels[i];

        BwavChannel channel = source;
        channel.fullSampleCount = source.sampleCount;
        channel.sampleCount = std::min(source.sampleCount, sampleCount);
        channel.fullDataOffset = fullOffsets[i];

        if (HoldsOpusPackets(source))
        {
            channel.data = MakeOpusPrefetchData(source, channel.sampleCount, full.isBigEndian, &opusTailAudio[i]);
        }
        else
        {
            const u64 bytes = std::min<u64>(GetSampleBytes(source.codec, channel.sampleCount, HasOpusTailField(source)), source.data.size());
            channel.data.assign(source.data.begin(), source.data.begin() + static_cast<s64>(bytes));
        }

        prefetch.channels.push_back(std::move(channel));
    }

    const std::vector<u32> offsets = LayoutChannelData(prefetch, false);

    for (u32 i = 0; i < prefetch.channels.size(); i++)
    {
        BwavChannel& channel = prefetch.channels[i];

        if (HoldsOpusPackets(full.channels[i]) && channel.sampleCount != 0)
            SetOpusPrefetchTail(&channel, opusTailAudio[i], offsets[i] + channel.sampleCount * static_cast<u32>(sizeof(s16)), prefetch.isBigEndian);
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

void CopyBwavLoop(const Bwav& source, Bwav* target)
{
    for (u32 i = 0; i < std::min(source.channels.size(), target->channels.size()); i++)
    {
        const BwavChannel& from = source.channels[i];
        BwavChannel& to = target->channels[i];

        to.loopEndFrame = from.loopEndFrame;
        to.loopStartFrame = from.loopStartFrame;
        to.loopPredScale = from.loopPredScale;
        to.loopYn1 = from.loopYn1;
        to.loopYn2 = from.loopYn2;
    }
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

    if (HoldsOpusPackets(channel))
    {
        DecodeOpusPackets(channel, out);
        return;
    }

    const u32 sampleCount = GetStoredSampleCount(channel);

    out->assign(sampleCount, 0);

    if (channel.codec != BwavCodec::DspAdpcm)
    {
        const ByteView view(channel.data.data(), channel.data.size(), bwav.isBigEndian);

        for (u32 i = 0; i < sampleCount; i++)
            (*out)[i] = view.readS16(i * sizeof(s16));

        return;
    }

    ADPCMINFO info = MakeAdpcmInfo(channel);
    decode(const_cast<u8*>(channel.data.data()), out->data(), &info, sampleCount);
}

static void EncodeDspAdpcmChannel(const std::vector<s16>& pcm, BwavChannel* channel)
{
    const u32 sampleCount = static_cast<u32>(pcm.size());
    channel->data.assign(GetSampleBytes(BwavCodec::DspAdpcm, sampleCount, false), 0);

    if (sampleCount == 0)
        return;

    std::vector<s16> samples = pcm;
    ADPCMINFO info = {};
    encode(samples.data(), channel->data.data(), &info, sampleCount);

    for (u32 i = 0; i < cBwavCoefficientCount; i++)
        channel->coefficients[i] = info.coef[i];
}

static void EncodePcm16Channel(const std::vector<s16>& pcm, bool isBigEndian, BwavChannel* channel)
{
    ByteBuilder builder(isBigEndian);
    builder.reserve(pcm.size() * sizeof(s16));

    for (s16 sample : pcm)
        builder.appendU16(static_cast<u16>(sample));

    channel->data = builder.take();
}

static bool EncodeOpusChannel(const std::vector<s16>& pcm, const BwavFormat& format, BwavChannel* channel)
{
    std::vector<f32> samples(pcm.size());

    for (u64 i = 0; i < pcm.size(); i++)
        samples[i] = AudioProcessing::fromPcm16(pcm[i]);

    return nxopus::Encode({ &samples }, static_cast<u32>(pcm.size()), format.sampleRate, static_cast<s32>(format.opusChannelBitrate), nxopus::PreSkip::Keep, &channel->data) == nxopus::Error::None;
}

bool ReadBwavCodec(const void* data, u64 size, BwavCodec* out)
{
    if (!IsBwav(data, size))
        return false;

    const ByteView view(data, size, ByteView::IsBigEndianMark(data, size, cHeaderByteOrder));
    const u64 codecOffset = GetChannelInfoOffset(0) + cChannelCodec;

    if (view.readU16(cHeaderChannelCount) == 0 || !view.has(codecOffset, sizeof(u16)) || !IsSupportedCodec(view.readU16(codecOffset)))
        return false;

    *out = static_cast<BwavCodec>(view.readU16(codecOffset));
    return true;
}

bool EncodeBwav(const std::vector<std::vector<s16>>& channels, const BwavFormat& format, Bwav* out, const char** outError)
{
    if (format.codec == BwavCodec::Opus && !nxopus::IsSupportedSampleRate(format.sampleRate))
    {
        *outError = messages::opus::cEncodeSampleRate;
        return false;
    }

    Bwav bwav;
    bwav.isBigEndian = format.isBigEndian;
    bwav.channels.resize(channels.size());

    for (u32 i = 0; i < channels.size(); i++)
    {
        const std::vector<s16>& pcm = channels[i];
        const u32 sampleCount = static_cast<u32>(pcm.size());

        BwavChannel& channel = bwav.channels[i];
        channel.codec = format.codec;
        channel.pan = GetEncodePan(i, static_cast<u32>(channels.size()));
        channel.sampleRate = format.sampleRate;
        channel.fullSampleCount = sampleCount;
        channel.sampleCount = sampleCount;

        if (format.codec == BwavCodec::Pcm16)
            EncodePcm16Channel(pcm, format.isBigEndian, &channel);
        else if (format.codec == BwavCodec::DspAdpcm)
            EncodeDspAdpcmChannel(pcm, &channel);
        else if (sampleCount != 0 && !EncodeOpusChannel(pcm, format, &channel))
        {
            *outError = messages::opus::cEncodeError;
            return false;
        }
    }

    SetBwavLoop(&bwav, format.isLoop, format.loopStartFrame);

    if (bwav.isLoop() && format.codec == BwavCodec::DspAdpcm)
    {
        for (BwavChannel& channel : bwav.channels)
            channel.data.resize(getBytesForAdpcmSamples(channel.sampleCount));
    }

    bwav.sampleCrc = CalcBwavSampleCrc(bwav);

    *out = std::move(bwav);
    return true;
}

}
