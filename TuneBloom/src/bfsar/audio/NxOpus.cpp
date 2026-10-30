#include <bfsar/audio/NxOpus.h>

#include <core/ByteBuilder.h>
#include <core/ByteView.h>

#include <opus.h>
#include <opus_multistream.h>

#include <algorithm>

namespace nxopus
{

static constexpr u32 cInfoChannelCount   = 0x01;
static constexpr u32 cInfoSampleRate     = 0x04;
static constexpr u32 cInfoPreSkip        = 0x14;
static constexpr u32 cLayoutStreamCount  = 0x00;
static constexpr u32 cLayoutCoupledCount = 0x01;
static constexpr u32 cLayoutMapping      = 0x04;
static constexpr u32 cMaxPacketSamples   = 5760;
static constexpr u32 cMaxStreamBytes     = 4000;
static constexpr u32 cFramesPerSecond    = 50;
static constexpr u8 cInfoVersion         = 0;
static constexpr u16 cVariableFrameSize  = 0;
static constexpr s32 cEncoderApplication = OPUS_APPLICATION_RESTRICTED_LOWDELAY;
static constexpr s32 cEncoderComplexity  = 10;

static constexpr u32 cSupportedSampleRates[] = { 48000, 24000, 16000, 12000, 8000 };
static constexpr u32 cChannelBitrateTolerance = 10;

struct PacketTotals
{
    u64 sampleCount = 0;
    u64 payloadSize = 0;
};

static Error CountPackets(const ByteView& packets, const Info& info, PacketTotals* out)
{
    const u64 end = info.dataOffset + info.dataSize;
    PacketTotals totals;

    for (u64 offset = info.dataOffset; offset + cPacketHeaderSize <= end;)
    {
        const u32 packetLength = packets.readU32(offset);
        offset += cPacketHeaderSize;

        if (packetLength == 0 || packetLength > end - offset)
            return Error::DataDamaged;

        const s32 samples = opus_packet_get_nb_samples(packets.at(offset), static_cast<opus_int32>(packetLength), static_cast<opus_int32>(info.sampleRate));

        if (samples < 0)
            return Error::DataDamaged;

        totals.sampleCount += static_cast<u64>(samples);
        totals.payloadSize += packetLength;
        offset += packetLength;
    }

    *out = totals;
    return Error::None;
}

static Error DecodePackets(const ByteView& packets, const Info& info, OpusMSDecoder* decoder, u64 skipSamples, std::vector<std::vector<s16>>* outChannels)
{
    const u64 end = info.dataOffset + info.dataSize;
    std::vector<opus_int16> packet(static_cast<size_t>(cMaxPacketSamples) * info.channelCount);
    u64 skipLeft = skipSamples;

    for (u64 offset = info.dataOffset; offset + cPacketHeaderSize <= end;)
    {
        const u32 packetLength = packets.readU32(offset);
        offset += cPacketHeaderSize;

        const s32 samples = opus_multistream_decode(decoder, packets.at(offset), static_cast<opus_int32>(packetLength), packet.data(), cMaxPacketSamples, 0);

        if (samples < 0)
            return Error::DecodeFailed;

        offset += packetLength;

        const u64 dropped = std::min<u64>(skipLeft, static_cast<u64>(samples));
        skipLeft -= dropped;

        for (u32 channel = 0; channel < info.channelCount; channel++)
        {
            std::vector<s16>& out = (*outChannels)[channel];

            for (u64 sample = dropped; sample < static_cast<u64>(samples); sample++)
                out.push_back(packet[sample * info.channelCount + channel]);
        }
    }

    return Error::None;
}

static void SetPairedLayout(Info* info)
{
    info->streamCount = (info->channelCount + 1) / 2;
    info->coupledStreamCount = info->channelCount / 2;
    info->channelMapping.resize(info->channelCount);

    for (u32 channel = 0; channel < info->channelCount; channel++)
        info->channelMapping[channel] = static_cast<u8>(channel);
}

static bool ReadMultiStreamLayout(const ByteView& view, u64 body, u64 chunkSize, Info* out)
{
    if (chunkSize < cLayoutMapping + out->channelCount)
        return false;

    out->streamCount = view.readU8(body + cLayoutStreamCount);
    out->coupledStreamCount = view.readU8(body + cLayoutCoupledCount);

    for (u32 channel = 0; channel < out->channelCount; channel++)
        out->channelMapping[channel] = view.readU8(body + cLayoutMapping + channel);

    return true;
}

bool IsSupportedSampleRate(u32 sampleRate)
{
    for (u32 supported : cSupportedSampleRates)
    {
        if (sampleRate == supported)
            return true;
    }

    return false;
}

bool HasInfoChunk(const void* data, u64 size)
{
    return data && size >= cChunkHeaderSize && ByteView(data, size, false).readU32(0) == cInfoChunkId;
}

bool ReadInfo(const void* data, u64 size, Info* out)
{
    if (!data)
        return false;

    const ByteView view(data, size, false);
    bool hasInfo = false;

    for (u64 offset = 0; offset + cChunkHeaderSize <= size;)
    {
        const u32 id = view.readU32(offset);
        const u64 chunkSize = std::min<u64>(view.readU32(offset + sizeof(u32)), size - offset - cChunkHeaderSize);
        const u64 body = offset + cChunkHeaderSize;

        if (id == cInfoChunkId && chunkSize >= cInfoChunkSize)
        {
            out->channelCount = view.readU8(body + cInfoChannelCount);
            out->sampleRate = view.readU32(body + cInfoSampleRate);
            out->preSkip = view.readU16(body + cInfoPreSkip);
            SetPairedLayout(out);
            hasInfo = true;
        }
        else if (id == cMultiStreamChunkId && hasInfo)
        {
            if (!ReadMultiStreamLayout(view, body, chunkSize, out))
                return false;
        }
        else if (id == cDataChunkId)
        {
            out->dataOffset = body;
            out->dataSize = chunkSize;

            return hasInfo;
        }

        offset = body + chunkSize;
    }

    return false;
}

u64 Measure(const void* data, u64 size)
{
    if (!HasInfoChunk(data, size))
        return 0;

    const ByteView view(data, size, false);
    bool hasInfo = false;

    for (u64 offset = 0; offset + cChunkHeaderSize <= size;)
    {
        const u32 id = view.readU32(offset);
        const u64 chunkSize = view.readU32(offset + sizeof(u32));

        if (chunkSize > size - offset - cChunkHeaderSize)
            return 0;

        hasInfo = hasInfo || (id == cInfoChunkId && chunkSize >= cInfoChunkSize);
        offset += cChunkHeaderSize + chunkSize;

        if (id == cDataChunkId)
            return hasInfo ? offset : 0;
    }

    return 0;
}

Error Decode(const void* data, u64 size, PreSkip preSkip, std::vector<std::vector<s16>>* outChannels, Info* outInfo)
{
    Info info;

    if (!ReadInfo(data, size, &info))
        return Error::NotOpus;

    if (info.channelCount < 1 || info.channelCount > cMaxChannelCount)
        return Error::ChannelCountUnsupported;

    if (!IsSupportedSampleRate(info.sampleRate))
        return Error::SampleRateUnsupported;

    const ByteView packets(data, size, true);
    PacketTotals totals;
    const Error countError = CountPackets(packets, info, &totals);

    if (countError != Error::None)
        return countError;

    const u64 sampleCount = totals.sampleCount;

    const u64 skipSamples = preSkip == PreSkip::Drop ? info.preSkip : 0;

    if (sampleCount <= skipSamples)
        return Error::NoAudibleSamples;

    s32 createError = OPUS_OK;
    OpusMSDecoder* decoder = opus_multistream_decoder_create(static_cast<opus_int32>(info.sampleRate), static_cast<s32>(info.channelCount), static_cast<s32>(info.streamCount), static_cast<s32>(info.coupledStreamCount), info.channelMapping.data(), &createError);

    if (createError == OPUS_BAD_ARG)
        return Error::ChannelCountUnsupported;

    if (!decoder || createError != OPUS_OK)
        return Error::DecoderCreateFailed;

    std::vector<std::vector<s16>> channels(info.channelCount);

    for (std::vector<s16>& channel : channels)
        channel.reserve(sampleCount - skipSamples);

    const Error error = DecodePackets(packets, info, decoder, skipSamples, &channels);
    opus_multistream_decoder_destroy(decoder);

    if (error != Error::None)
        return error;

    *outChannels = std::move(channels);
    *outInfo = info;

    return Error::None;
}

u32 MeasureChannelBitrate(const void* data, u64 size)
{
    Info info;
    PacketTotals totals;

    if (!ReadInfo(data, size, &info) || info.channelCount == 0 || !IsSupportedSampleRate(info.sampleRate))
        return 0;

    if (CountPackets(ByteView(data, size, true), info, &totals) != Error::None || totals.sampleCount == 0)
        return 0;

    return static_cast<u32>(totals.payloadSize * 8 * info.sampleRate / totals.sampleCount / info.channelCount);
}

static u32 GetBitrateDistance(u32 a, u32 b)
{
    return a > b ? a - b : b - a;
}

u32 FindNearestChannelBitrate(u32 bitrate)
{
    u32 nearest = cChannelBitrates[0];

    for (u32 preset : cChannelBitrates)
    {
        if (GetBitrateDistance(preset, bitrate) < GetBitrateDistance(nearest, bitrate))
            nearest = preset;
    }

    return nearest;
}

u32 SnapChannelBitrate(u32 measured)
{
    if (measured == 0)
        return 0;

    const u32 nearest = FindNearestChannelBitrate(measured);

    return GetBitrateDistance(nearest, measured) * cChannelBitrateTolerance <= nearest ? nearest : measured;
}

bool IsChannelBitratePreset(u32 bitrate)
{
    return bitrate != 0 && FindNearestChannelBitrate(bitrate) == bitrate;
}

static bool IsSilentFrame(const std::vector<f32>& frame)
{
    return std::all_of(frame.begin(), frame.end(), [](f32 sample) { return sample == 0.0f; });
}

static void SetVariableBitrate(OpusMSEncoder* encoder, bool isVariable, bool* isCurrentVariable)
{
    if (isVariable == *isCurrentVariable)
        return;

    opus_multistream_encoder_ctl(encoder, OPUS_SET_VBR(isVariable ? 1 : 0));
    *isCurrentVariable = isVariable;
}

static bool EncodePackets(OpusMSEncoder* encoder, const std::vector<const std::vector<f32>*>& channels, u32 streamCount, u32 frameSize, u64 firstSample, u64 samplesToFeed, ByteBuilder* out)
{
    const u32 channelCount = static_cast<u32>(channels.size());
    std::vector<f32> frame(static_cast<size_t>(frameSize) * channelCount);
    std::vector<u8> packet(static_cast<size_t>(cMaxStreamBytes) * streamCount);
    bool isVariable = true;

    for (u64 position = 0; position < samplesToFeed; position += frameSize)
    {
        for (u32 sample = 0; sample < frameSize; sample++)
        {
            const u64 source = firstSample + position + sample;

            for (u32 channel = 0; channel < channelCount; channel++)
                frame[static_cast<size_t>(sample) * channelCount + channel] = source < channels[channel]->size() ? (*channels[channel])[source] : 0.0f;
        }

        SetVariableBitrate(encoder, !IsSilentFrame(frame), &isVariable);

        const opus_int32 packetSize = opus_multistream_encode_float(encoder, frame.data(), static_cast<s32>(frameSize), packet.data(), static_cast<opus_int32>(packet.size()));

        if (packetSize < 0)
            return false;

        opus_uint32 finalRange = 0;
        opus_multistream_encoder_ctl(encoder, OPUS_GET_FINAL_RANGE(&finalRange));

        out->appendU32(static_cast<u32>(packetSize));
        out->appendU32(finalRange);
        out->appendBytes(packet.data(), static_cast<u64>(packetSize));
    }

    return true;
}

static std::vector<u8> BuildFile(const Info& layout, const std::vector<u8>& packets)
{
    const bool hasLayoutChunk = layout.streamCount > 1;
    const u32 layoutChunkSize = cLayoutMapping + layout.channelCount;
    const u32 dataChunkOffset = cChunkHeaderSize + cInfoChunkSize + (hasLayoutChunk ? cChunkHeaderSize + layoutChunkSize : 0);

    ByteBuilder builder(false);
    builder.appendU32(cInfoChunkId);
    builder.appendU32(cInfoChunkSize);
    builder.appendU8(cInfoVersion);
    builder.appendU8(static_cast<u8>(layout.channelCount));
    builder.appendU16(cVariableFrameSize);
    builder.appendU32(layout.sampleRate);
    builder.appendU32(dataChunkOffset);
    builder.appendU32(0);
    builder.appendU32(0);
    builder.appendU16(static_cast<u16>(layout.preSkip));
    builder.appendZeros(2);

    if (hasLayoutChunk)
    {
        builder.appendU32(cMultiStreamChunkId);
        builder.appendU32(layoutChunkSize);
        builder.appendU8(static_cast<u8>(layout.streamCount));
        builder.appendU8(static_cast<u8>(layout.coupledStreamCount));
        builder.appendZeros(2);
        builder.appendBytes(layout.channelMapping.data(), layout.channelMapping.size());
    }

    builder.appendU32(cDataChunkId);
    builder.appendU32(static_cast<u32>(packets.size()));
    builder.appendBytes(packets.data(), packets.size());

    return builder.take();
}

Error Encode(const std::vector<const std::vector<f32>*>& channels, u32 sampleCount, u32 sampleRate, s32 bitrate, PreSkip preSkip, std::vector<u8>* out)
{
    if (channels.empty() || channels.size() > cMaxChannelCount)
        return Error::ChannelCountUnsupported;

    if (!IsSupportedSampleRate(sampleRate))
        return Error::SampleRateUnsupported;

    Info layout;
    layout.channelCount = static_cast<u32>(channels.size());
    layout.sampleRate = sampleRate;
    SetPairedLayout(&layout);

    s32 createError = OPUS_OK;
    OpusMSEncoder* encoder = opus_multistream_encoder_create(static_cast<opus_int32>(sampleRate), static_cast<s32>(layout.channelCount), static_cast<s32>(layout.streamCount), static_cast<s32>(layout.coupledStreamCount), layout.channelMapping.data(), cEncoderApplication, &createError);

    if (!encoder || createError != OPUS_OK)
        return Error::EncoderCreateFailed;

    opus_multistream_encoder_ctl(encoder, OPUS_SET_BITRATE(bitrate));
    opus_multistream_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(cEncoderComplexity));

    opus_int32 lookahead = 0;
    opus_multistream_encoder_ctl(encoder, OPUS_GET_LOOKAHEAD(&lookahead));
    layout.preSkip = static_cast<u32>(lookahead);
    const bool isDelayKept = preSkip == PreSkip::Keep;

    ByteBuilder packets(true);
    packets.reserve(sampleCount / 2);

    const bool isEncoded = EncodePackets(encoder, channels, layout.streamCount, sampleRate / cFramesPerSecond, isDelayKept ? layout.preSkip : 0, isDelayKept ? sampleCount : static_cast<u64>(sampleCount) + layout.preSkip, &packets);
    opus_multistream_encoder_destroy(encoder);

    if (!isEncoded)
        return Error::EncodeFailed;

    *out = BuildFile(layout, packets.take());
    return Error::None;
}

}
