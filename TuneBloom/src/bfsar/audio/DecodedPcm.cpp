#include <bfsar/audio/DecodedPcm.h>

#include <filedevice/seadFileDeviceMgr.h>
#include <prim/seadEndian.h>
#include <stream/seadFileDeviceStream.h>

#include <cstring>

static bool ReadRiffSampleData(const WaveFile::RiffWaveInfo& info, std::vector<u8>* out)
{
    sead::FileDevice* device = sead::FileDeviceMgr::instance()->findDevice("native");
    SEAD_ASSERT(device);

    sead::FileHandle handle;
    device->tryOpen(&handle, info.path, sead::FileDevice::FileOpenFlag::eReadOnly, 0);

    if (!handle.getDevice())
        return false;

    sead::FileDeviceReadStream stream(&handle, sead::Stream::Modes::eBinary);
    stream.rewind();
    stream.skip(info.dataStart);

    out->assign(info.sampleBytes, 0);
    stream.readMemBlock(out->data(), info.sampleBytes);

    return true;
}

static float DecodeRiffSample(const u8* raw, u16 format, u32 bytesPerSample)
{
    switch (bytesPerSample)
    {
        case 1:
            return static_cast<float>(static_cast<s32>(raw[0]) - 128) / 128.0f;

        case 2:
        {
            s16 value;
            std::memcpy(&value, raw, sizeof(value));
            return AudioProcessing::fromPcm16(sead::Endian::toHostS16(sead::Endian::eLittle, value));
        }

        case 3:
        {
            s32 value = raw[0] | (raw[1] << 8) | (raw[2] << 16);

            if (value & 0x800000)
                value -= 0x1000000;

            return static_cast<float>(value) / 8388608.0f;
        }

        default:
            break;
    }

    u32 bits;
    std::memcpy(&bits, raw, sizeof(bits));
    bits = sead::Endian::toHostU32(sead::Endian::eLittle, bits);

    if (format == cRiffFormatFloat)
    {
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    return static_cast<float>(static_cast<double>(static_cast<s32>(bits)) / 2147483648.0);
}

static void DecodeRiffFrames(const WaveFile::RiffWaveInfo& info, const u8* raw, std::vector<std::vector<float>>* out)
{
    const u32 numChannels = info.numChannels;
    const u32 bytesPerSample = info.bitsPerSample / 8;

    out->assign(numChannels, std::vector<float>(info.sampleCount, 0.0f));

    for (u32 i = 0; i < info.sampleCount; i++)
    {
        for (u32 ch = 0; ch < numChannels; ch++)
            (*out)[ch][i] = DecodeRiffSample(raw + (static_cast<size_t>(i) * numChannels + ch) * bytesPerSample, info.format, bytesPerSample);
    }
}

bool IsPlainPcm(const WaveFile::RiffWaveInfo& info)
{
    return info.format == cRiffFormatPcm && info.bitsPerSample <= 16 && info.numChannels <= snd::cWaveChannelMax;
}

bool decodeRiffChannels(const WaveFile::RiffWaveInfo& info, std::vector<std::vector<float>>* out)
{
    out->clear();

    if (!info.isValid || info.sampleCount == 0 || info.numChannels == 0)
        return false;

    std::vector<u8> raw;

    if (!ReadRiffSampleData(info, &raw))
        return false;

    DecodeRiffFrames(info, raw.data(), out);

    return true;
}

bool decodeRiffBytes(const u8* data, size_t size, DecodedPcm* out)
{
    *out = DecodedPcm();

    WaveFile::RiffWaveInfo info;

    if (!WaveFile::readRiffWavBytes(data, static_cast<u32>(size), static_cast<u32>(size), &info, true) || info.sampleCount == 0)
        return false;

    if (static_cast<u64>(info.dataStart) + info.sampleBytes > size)
        return false;

    DecodeRiffFrames(info, data + info.dataStart, &out->channels);

    out->sampleRate = info.sampleRate;
    out->sampleCount = info.sampleCount;
    out->isLoop = info.isLoop;
    out->loopStartFrame = info.loopStartFrame;
    out->loopEndFrame = info.loopEndFrame;

    return true;
}

DecodedPcm decodePcmForPreview(const WaveFile::RiffWaveInfo& info)
{
    DecodedPcm out;

    if (!info.isValid || info.sampleBytes == 0 || info.numChannels == 0)
        return out;

    if (info.sampleFormat != snd::SampleFormat::PcmS8 && info.sampleFormat != snd::SampleFormat::PcmS16)
        return out;

    std::vector<u8> raw;

    if (!ReadRiffSampleData(info, &raw))
        return out;

    const u32 numChannels = info.numChannels;
    const u32 sampleCount = info.sampleCount;

    out.channels.assign(numChannels, std::vector<float>(sampleCount, 0.0f));
    out.sampleRate = info.sampleRate;
    out.sampleCount = sampleCount;

    if (info.sampleFormat == snd::SampleFormat::PcmS8)
    {
        for (u32 ch = 0; ch < numChannels; ch++)
        {
            const u8* src = raw.data() + ch;
            float* dst = out.channels[ch].data();

            for (u32 i = 0; i < sampleCount; i++, src += numChannels)
                dst[i] = static_cast<float>(static_cast<s32>(*src) - 128) / 128.0f;
        }

        return out;
    }

    const s16* samples16 = reinterpret_cast<const s16*>(raw.data());
    const bool needsSwap = (info.endian != sead::Endian::getHostEndian());

    for (u32 ch = 0; ch < numChannels; ch++)
    {
        const s16* src = samples16 + ch;
        float* dst = out.channels[ch].data();

        if (needsSwap)
        {
            for (u32 i = 0; i < sampleCount; i++, src += numChannels)
                dst[i] = AudioProcessing::fromPcm16(sead::Endian::toHostS16(info.endian, *src));
        }
        else
        {
            for (u32 i = 0; i < sampleCount; i++, src += numChannels)
                dst[i] = AudioProcessing::fromPcm16(*src);
        }
    }

    return out;
}
