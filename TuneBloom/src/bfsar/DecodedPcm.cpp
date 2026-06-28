#include <bfsar/DecodedPcm.h>

#include <filedevice/seadFileDeviceMgr.h>
#include <prim/seadEndian.h>

DecodedPcm decodePcmForPreview(const WaveFile::RiffWaveInfo& info)
{
    DecodedPcm out;

    if (!info.isValid || info.sampleBytes == 0 || info.numChannels == 0)
        return out;

    if (info.sampleFormat != snd::SampleFormat::PcmS8 && info.sampleFormat != snd::SampleFormat::PcmS16)
        return out;

    sead::FileDevice* device = sead::FileDeviceMgr::instance()->findDevice("native");
    SEAD_ASSERT(device);

    sead::FileHandle handle;
    device->tryOpen(&handle, info.path, sead::FileDevice::FileOpenFlag::eReadOnly, 0);

    if (!handle.getDevice())
        return out;

    sead::FileDeviceReadStream stream(&handle, sead::Stream::Modes::eBinary);
    stream.setBinaryEndian(info.endian);
    stream.rewind();
    stream.skip(info.dataStart);

    const u32 numChannels = info.numChannels;
    const u32 sampleCount = info.sampleCount;

    out.channels.assign(numChannels, std::vector<float>(sampleCount, 0.0f));
    out.sampleRate = info.sampleRate;
    out.sampleCount = sampleCount;

    u8* raw = new u8[info.sampleBytes];
    stream.readMemBlock(raw, info.sampleBytes);

    if (info.sampleFormat == snd::SampleFormat::PcmS8)
    {
        for (u32 i = 0; i < sampleCount; i++)
        {
            for (u32 ch = 0; ch < numChannels; ch++)
            {
                const u8 raw8 = raw[i * numChannels + ch];
                const s8 signed8 = static_cast<s8>(static_cast<s32>(raw8) - 128);
                out.channels[ch][i] = static_cast<float>(signed8) / 128.0f;
            }
        }
    }
    else
    {
        const s16* samples16 = reinterpret_cast<const s16*>(raw);
        const bool needsSwap = (info.endian != sead::Endian::getHostEndian());

        for (u32 i = 0; i < sampleCount; i++)
        {
            for (u32 ch = 0; ch < numChannels; ch++)
            {
                s16 v = samples16[i * numChannels + ch];
                if (needsSwap)
                    v = sead::Endian::toHostS16(info.endian, v);
                out.channels[ch][i] = static_cast<float>(v) / 32768.0f;
            }
        }
    }

    delete[] raw;

    return out;
}
