#include <bfsar/audio/NxOpus.h>

#include <core/ByteView.h>

#include <opus.h>

#include <algorithm>

namespace nxopus
{

static constexpr u32 cInfoChannelCount = 0x01;
static constexpr u32 cInfoSampleRate   = 0x04;
static constexpr u32 cInfoPreSkip      = 0x14;
static constexpr u32 cMaxPacketSamples = 5760;
static constexpr u32 cMaxChannelCount  = 2;

static constexpr u32 cSupportedSampleRates[] = { 48000, 24000, 16000, 12000, 8000 };

static Error CountSamples(const ByteView& packets, const Info& info, u64* outSampleCount)
{
    const u64 end = info.dataOffset + info.dataSize;
    u64 sampleCount = 0;

    for (u64 offset = info.dataOffset; offset + cPacketHeaderSize <= end;)
    {
        const u32 packetLength = packets.readU32(offset);
        offset += cPacketHeaderSize;

        if (packetLength == 0 || packetLength > end - offset)
            return Error::DataDamaged;

        const s32 samples = opus_packet_get_nb_samples(packets.at(offset), static_cast<opus_int32>(packetLength), static_cast<opus_int32>(info.sampleRate));

        if (samples < 0)
            return Error::DataDamaged;

        sampleCount += static_cast<u64>(samples);
        offset += packetLength;
    }

    *outSampleCount = sampleCount;
    return Error::None;
}

static Error DecodePackets(const ByteView& packets, const Info& info, OpusDecoder* decoder, u64 skipSamples, std::vector<std::vector<s16>>* outChannels)
{
    const u64 end = info.dataOffset + info.dataSize;
    std::vector<opus_int16> packet(static_cast<size_t>(cMaxPacketSamples) * info.channelCount);
    u64 skipLeft = skipSamples;

    for (u64 offset = info.dataOffset; offset + cPacketHeaderSize <= end;)
    {
        const u32 packetLength = packets.readU32(offset);
        offset += cPacketHeaderSize;

        const s32 samples = opus_decode(decoder, packets.at(offset), static_cast<opus_int32>(packetLength), packet.data(), cMaxPacketSamples, 0);

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
            out->preSkip = view.readU32(body + cInfoPreSkip);
            hasInfo = true;
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
    u64 sampleCount = 0;
    const Error countError = CountSamples(packets, info, &sampleCount);

    if (countError != Error::None)
        return countError;

    const u64 skipSamples = preSkip == PreSkip::Drop ? info.preSkip : 0;

    if (sampleCount <= skipSamples)
        return Error::NoAudibleSamples;

    s32 createError = OPUS_OK;
    OpusDecoder* decoder = opus_decoder_create(static_cast<opus_int32>(info.sampleRate), static_cast<s32>(info.channelCount), &createError);

    if (!decoder || createError != OPUS_OK)
        return Error::DecoderCreateFailed;

    std::vector<std::vector<s16>> channels(info.channelCount);

    for (std::vector<s16>& channel : channels)
        channel.reserve(sampleCount - skipSamples);

    const Error error = DecodePackets(packets, info, decoder, skipSamples, &channels);
    opus_decoder_destroy(decoder);

    if (error != Error::None)
        return error;

    *outChannels = std::move(channels);
    *outInfo = info;

    return Error::None;
}

}
