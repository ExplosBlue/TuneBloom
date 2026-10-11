#include <bfsar/audio/WavWriter.h>

#include <bfsar/io/FileIo.h>
#include <bfsar/io/FileWriter.h>

#include <algorithm>

static constexpr u32 cRiffHeaderSize = 44;
static constexpr u32 cSmplChunkSize  = 60;
static constexpr u32 cSmplLoopOffset = 28;
static constexpr u32 cChunkHeadSize  = 8;
static constexpr u32 cBytesPerSample = 2;

u32 CalcPcm16WavSize(u64 sampleCount, u32 channelCount, bool hasLoop)
{
    const u64 size = cRiffHeaderSize + sampleCount * channelCount * cBytesPerSample + (hasLoop ? cChunkHeadSize + cSmplChunkSize : 0);

    return static_cast<u32>(std::min<u64>(size, 0xFFFFFFFF));
}

void WriteWavLoop(sead::WriteStream* stream, const WavLoop& loop)
{
    const u8 zeros[cSmplLoopOffset] = {};

    stream->writeMemBlock("smpl", 4);
    stream->writeU32(cSmplChunkSize);
    stream->writeMemBlock(zeros, sizeof(zeros));

    // Loop count, sampler data size
    stream->writeU32(1);
    stream->writeU32(0);

    // Cue point
    stream->writeU32(0);
    stream->writeU32(0);
    stream->writeU32(loop.start);
    stream->writeU32(loop.end);
    stream->writeU32(0);
    stream->writeU32(0);
}

bool WritePcm16Wav(const sead::SafeString& path, const Pcm16Wav& wav)
{
    const u32 channelCount = static_cast<u32>(wav.channels.size());
    if (channelCount == 0)
        return false;

    size_t sampleCount = wav.channels[0].size();
    for (const std::vector<s16>& channel : wav.channels)
        sampleCount = std::min(sampleCount, channel.size());

    if (sampleCount == 0)
        return false;

    fileio::StagedFile file;
    if (!file.open(path))
        return false;

    CheckedFileStreamSrc streamSrc(file.getHandle());
    sead::WriteStream stream(&streamSrc, sead::Stream::Modes::eBinary);
    stream.setBinaryEndian(sead::Endian::eLittle);

    const u32 blockAlign = channelCount * cBytesPerSample;
    const u32 dataSize = static_cast<u32>(sampleCount) * blockAlign;
    const u32 loopSize = wav.loop.enabled ? cChunkHeadSize + cSmplChunkSize : 0;

    stream.writeMemBlock("RIFF", 4);
    stream.writeU32(cRiffHeaderSize - cChunkHeadSize + dataSize + loopSize + CalcWavRegionsSize(wav.regions));
    stream.writeMemBlock("WAVE", 4);

    stream.writeMemBlock("fmt ", 4);
    stream.writeU32(16);
    stream.writeU16(1);
    stream.writeU16(static_cast<u16>(channelCount));
    stream.writeU32(wav.sampleRate);
    stream.writeU32(wav.sampleRate * blockAlign);
    stream.writeU16(static_cast<u16>(blockAlign));
    stream.writeU16(cBytesPerSample * 8);

    stream.writeMemBlock("data", 4);
    stream.writeU32(dataSize);

    std::vector<s16> frames(sampleCount * channelCount);

    for (u32 ch = 0; ch < channelCount; ch++)
    {
        const s16* src = wav.channels[ch].data();

        for (size_t i = 0; i < sampleCount; i++)
            frames[i * channelCount + ch] = sead::Endian::convertS16(sead::Endian::getHostEndian(), sead::Endian::eLittle, src[i]);
    }

    stream.writeMemBlock(frames.data(), dataSize);

    if (wav.loop.enabled)
        WriteWavLoop(&stream, wav.loop);

    WriteWavRegions(&stream, wav.regions);

    return !streamSrc.hasFailed() && file.commit();
}
