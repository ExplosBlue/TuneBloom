#include <bfsar/files/OpusStream.h>

#include <bfsar/archive/Bfsar.h>
#include <bfsar/files/BfstmFile.h>
#include <bfsar/files/StreamWaveOwner.h>
#include <bfsar/audio/NxOpus.h>
#include <bfsar/model/Sound.h>
#include <bfsar/files/WaveFile.h>
#include <bfsar/audio/WaveFileEditDecode.h>

#include <messages/Messages.h>
#include <bfsar/model/MessageLog.h>

#include <prim/seadMemUtil.h>
#include <filedevice/seadFileDeviceMgr.h>
#include <stream/seadFileDeviceStream.h>

#include <algorithm>
#include <vector>

namespace opusstream
{

    namespace
    {

        const char *GetDecodeErrorMessage(nxopus::Error error)
        {
            switch (error)
            {
                case nxopus::Error::NotOpus:
                    return messages::opus::cNotOpusStream;

                case nxopus::Error::ChannelCountUnsupported:
                    return messages::opus::cChannelCountUnsupported;

                case nxopus::Error::SampleRateUnsupported:
                    return messages::opus::cSampleRateUnsupported;

                case nxopus::Error::DataDamaged:
                    return messages::opus::cAudioDataDamaged;

                case nxopus::Error::NoAudibleSamples:
                    return messages::opus::cNoAudibleSamples;

                case nxopus::Error::DecoderCreateFailed:
                    return messages::opus::cDecoderCreateFailed;

                default:
                    break;
            }

            return messages::opus::cDecodeFailed;
        }

        u8 *loadStreamFile(sead::FileDevice *device, const Bfsar &archive, const sead::SafeString &streamPath, const sead::SafeString *filePathOverride, sead::FileDevice::LoadArg *loadArg)
        {
            if (filePathOverride)
            {
                loadArg->path = *filePathOverride;
                return device->tryLoad(*loadArg);
            }

            const sead::SafeString *archivePaths[2] = {&archive.getFilePath(), &archive.getLoadedArchivePath()};
            for (const sead::SafeString *archivePath : archivePaths)
            {
                sead::FixedSafeString<512> dir;

                if (!Bfsar::GetStreamFolder(*archivePath, &dir))
                    continue;

                sead::FixedSafeString<512> path;
                path.format("%s/%s", dir.cstr(), streamPath.cstr());

                *loadArg = sead::FileDevice::LoadArg();
                loadArg->path = path;

                u8 *fileData = device->tryLoad(*loadArg);

                if (fileData)
                    return fileData;
            }

            return nullptr;
        }

    }

    bool ReadInfoHeader(const void *data, u32 size, Info &outInfo)
    {
        nxopus::Info info;

        if (!nxopus::ReadInfo(data, size, &info))
            return false;

        outInfo.channelCount = static_cast<u8>(info.channelCount);
        outInfo.sampleRate = info.sampleRate;
        outInfo.preSkip = info.preSkip;
        outInfo.sampleCount = 0;

        return info.channelCount >= 1;
    }

    bool IsOpusStream(const void *data, u32 size)
    {
        return nxopus::HasInfoChunk(data, size);
    }

    u32 ReadChannelBitrate(const void *data, u32 size)
    {
        return nxopus::SnapChannelBitrate(nxopus::MeasureChannelBitrate(data, size));
    }

    u32 GetEncodeChannelBitrate(const Sound::StreamSoundInfo &stream)
    {
        const u32 bitrate = stream.getOpusChannelBitrate();

        return bitrate != 0 ? bitrate : cDefaultChannelBitrate;
    }

    bool DecodeToPcm16(const void *data, u32 size, Info &outInfo, std::vector<s16 *> &outChannelData, const char **outError)
    {
        std::vector<std::vector<s16>> channels;
        nxopus::Info info;
        const nxopus::Error error = nxopus::Decode(data, size, nxopus::PreSkip::Drop, &channels, &info);

        if (error != nxopus::Error::None)
        {
            if (outError)
                *outError = GetDecodeErrorMessage(error);

            return false;
        }

        std::vector<s16 *> buffers;

        for (const std::vector<s16> &channel : channels)
        {
            s16 *buffer = reinterpret_cast<s16 *>(new u8[channel.size() * sizeof(s16)]);
            std::copy(channel.begin(), channel.end(), buffer);
            buffers.push_back(buffer);
        }

        outInfo.channelCount = static_cast<u8>(info.channelCount);
        outInfo.sampleRate = info.sampleRate;
        outInfo.preSkip = info.preSkip;
        outInfo.sampleCount = static_cast<u32>(channels.front().size());

        outChannelData = std::move(buffers);

        return true;
    }

    class WaveBuilder
    {
    public:
        static void attach(StreamWaveOwner &owner, Sound::StreamSoundInfo &stream, const sead::SafeString &streamName, const Info &info, const std::vector<s16 *> &channelBuffers, Item *popupItem);

    private:
        static WaveFile *createWave(StreamWaveOwner &owner, const Info &info, bool isLoop, u32 loopStartFrame, u32 loopEndFrame);
        static void attachChannel(WaveFile *wave, u8 *channelData, u32 sampleCount, u32 loopEndFrame);
    };

    WaveFile *WaveBuilder::createWave(StreamWaveOwner &owner, const Info &info, bool isLoop, u32 loopStartFrame, u32 loopEndFrame)
    {
        WaveFile *wave = new WaveFile();
        owner.prepareStreamWave(wave);
        wave->mIsFromStreamFile = true;

        wave->mDataEndian = sead::Endian::Types::eLittle;
        wave->mEncoding = WaveFile::Encoding::Pcm16;
        wave->mIsLoop = isLoop;
        wave->mSampleRate = info.sampleRate;
        wave->mLoopStartFrame = loopStartFrame;
        wave->mLoopEndFrame = loopEndFrame;
        wave->mSampleCount = info.sampleCount;

        wave->mUseOriginalData = false;

        return wave;
    }

    void WaveBuilder::attachChannel(WaveFile *wave, u8 *channelData, u32 sampleCount, u32 loopEndFrame)
    {
        WaveFile::Channel *channel = wave->mChannels.birthBack();
        SEAD_ASSERT(channel);

        channel->mOwnsData = true;
        channel->mData = channelData;
        channel->mDataSize = sampleCount * sizeof(s16);
        channel->mDataSizeMin = loopEndFrame * sizeof(s16);
        channel->mOriginalDataOffset = 0;
    }

    void WaveBuilder::attach(StreamWaveOwner &owner, Sound::StreamSoundInfo &stream, const sead::SafeString &streamName, const Info &info, const std::vector<s16 *> &channelBuffers, Item *popupItem)
    {
        Sound::StreamSoundInfo::Track::List &tracks = stream.getTrackList();

        bool isLoop = stream.getIsLoop();
        u32 loopStartFrame = isLoop ? stream.getLoopStartFrame() : 0;
        u32 loopEndFrame = isLoop ? stream.getLoopEndFrame() : info.sampleCount;

        if (loopEndFrame == 0 || loopEndFrame > info.sampleCount)
            loopEndFrame = info.sampleCount;

        if (loopStartFrame >= loopEndFrame)
        {
            isLoop = false;
            loopStartFrame = 0;
        }

        std::vector<bool> channelBufferUsed(channelBuffers.size(), false);

        for (u32 trackNo = 0; trackNo < tracks.size(); trackNo++)
        {
            Sound::StreamSoundInfo::Track &track = *static_cast<Sound::StreamSoundInfo::Track *>(tracks.nth(trackNo)->val());

            if (!track.isWaveUnloaded())
                continue;

            WaveFile *wave = createWave(owner, info, isLoop, loopStartFrame, loopEndFrame);
            owner.nameStreamWave(wave, streamName, trackNo, tracks.size());

            owner.addStreamWave(wave);
            track.getWaveFileRef().attach(wave);

            for (s32 ch = 0; ch < track.getChannels_().size(); ch++)
            {
                if (ch >= snd::cWaveChannelMax)
                    break;

                s8 globalChannelIndex = *track.getChannels_().nth(ch);

                if (globalChannelIndex < 0 || static_cast<u32>(globalChannelIndex) >= channelBuffers.size())
                {
                    sead::FormatFixedSafeString<1024> msg(messages::playback::cTrackNoChannelsFormat, trackNo);
                    MessageLog::instance()->addPopup({msg, popupItem});
                    continue;
                }

                u8 *channelData = reinterpret_cast<u8 *>(channelBuffers[globalChannelIndex]);

                if (channelBufferUsed[globalChannelIndex])
                {
                    u8 *copy = new u8[info.sampleCount * sizeof(s16)];
                    sead::MemUtil::copy(copy, channelData, info.sampleCount * sizeof(s16));
                    channelData = copy;
                }

                attachChannel(wave, channelData, info.sampleCount, loopEndFrame);
                channelBufferUsed[globalChannelIndex] = true;
            }

            wave->mIsLoopDirty = false;
            wave->mIsStreamExtended = true;
        }

        for (u32 i = 0; i < channelBuffers.size(); i++)
        {
            if (!channelBufferUsed[i])
                delete[] reinterpret_cast<u8 *>(channelBuffers[i]);
        }
    }

    bool AttachStreamWaves(Bfsar &archive, Sound *sound, const sead::SafeString *filePathOverride)
    {
        Sound::StreamSoundInfo &strmSoundInfo = sound->getStreamSoundInfo();
        Sound::StreamSoundInfo::Track::List &tracks = strmSoundInfo.getTrackList();

        if (tracks.isEmpty())
        {
            MessageLog::instance()->addPopup({messages::stream::cNeedsTrack, nullptr});
            return false;
        }

        if (!strmSoundInfo.hasUnloadedTracks())
            return true;

        const bool wasModified = sound->isModifiedSinceBaseline();

        sead::FileDevice *device = nullptr;

        if (sead::FileDeviceMgr::instance() != nullptr)
            device = sead::FileDeviceMgr::instance()->findDevice("native");

        if (!device)
            return false;

        sead::FileDevice::LoadArg loadArg;
        u8 *fileData = loadStreamFile(device, archive, strmSoundInfo.getPath(), filePathOverride, &loadArg);

        if (!fileData)
        {
            sead::FormatFixedSafeString<1024> msg(messages::stream::cFileMissingFormat, strmSoundInfo.getPath().cstr(), GetInnerFileExtension(archive.getFormat(), InnerFileKind::SoundArchive));
            MessageLog::instance()->addPopup({msg, sound});

            return false;
        }

        Info opusInfo;
        std::vector<s16 *> channelBuffers;
        const char *error = nullptr;

        const u32 fileSize = static_cast<u32>(loadArg.read_size);
        bool decoded = DecodeToPcm16(fileData, fileSize, opusInfo, channelBuffers, &error);

        if (decoded)
            strmSoundInfo.setOriginalOpusChannelBitrate(ReadChannelBitrate(fileData, fileSize));

        device->unload(fileData);

        if (!decoded)
        {
            sead::FormatFixedSafeString<1024> msg("'%s': %s", strmSoundInfo.getPath().cstr(), error ? error : messages::opus::cDecodeFailed);
            MessageLog::instance()->addPopup({msg, sound});
            return false;
        }

        WaveBuilder::attach(archive, strmSoundInfo, sound->getName(), opusInfo, channelBuffers, sound);

        sound->mStreamFileSignature = ComputeContentSignature(strmSoundInfo);
        sound->mHasStreamFileBaseline = true;

        if (!wasModified)
            sound->captureBaseline();

        return true;
    }

    bool ReadStreamFile(StreamWaveOwner &owner, Sound::StreamSoundInfo &stream, const sead::SafeString &streamName, const void *data, u32 size, const char **outError)
    {
        Info opusInfo;
        std::vector<s16 *> channelBuffers;

        if (!DecodeToPcm16(data, size, opusInfo, channelBuffers, outError))
            return false;

        ApplyStreamTrackLayout(stream, GetDefaultStreamTrackCount(opusInfo.channelCount), opusInfo.channelCount);
        stream.setOriginalOpusChannelBitrate(ReadChannelBitrate(data, size));

        WaveBuilder::attach(owner, stream, streamName, opusInfo, channelBuffers, nullptr);

        return true;
    }

    u64 ComputeContentSignature(const Sound::StreamSoundInfo &stream)
    {
        StateHash hash;

        const Sound::StreamSoundInfo::Track::List &trackList = stream.getTrackList();

        hash.mixValue<u32>(trackList.size());
        hash.mixValue<u32>(GetEncodeChannelBitrate(stream));

        for (u32 i = 0; i < trackList.size(); i++)
        {
            const Sound::StreamSoundInfo::Track *track = static_cast<const Sound::StreamSoundInfo::Track *>(trackList.nth(i)->val());

            hash.mixValue<u32>(track->getWaveFileRef().getItemId());

            if (!track->getWaveFileRef().isAttached())
                continue;

            const WaveFile &wave = *static_cast<const WaveFile *>(track->getWaveFileRef().getItem());

            hash.mixValue<u32>(static_cast<u32>(wave.getEncoding()));
            hash.mixValue<u32>(wave.getSampleRate());
            hash.mixValue<u32>(wave.getSampleCount());
            hash.mixValue<u32>(static_cast<u32>(wave.getDataEndian()));

            MixWaveChannelData(hash, wave);
        }

        return hash.value();
    }

    bool WriteStreamFile(sead::FileHandle &handle, const Sound::StreamSoundInfo &streamInfo, Item *popupItem)
    {
        const Sound::StreamSoundInfo::Track::List &tracks = streamInfo.getTrackList();

        auto fail = [&](const char *msg)
        {
            sead::FormatFixedSafeString<1024> popupMsg(messages::opus::cEncodeFailedFormat, msg);
            MessageLog::instance()->addPopup({popupMsg, popupItem});
            return false;
        };

        if (tracks.isEmpty())
            return fail(messages::stream::cTrackNone);

        const WaveFile *waves[8] = {};
        u32 waveCount = 0;
        u32 totalChannels = 0;

        for (u32 i = 0; i < tracks.size() && i < 8; i++)
        {
            const Sound::StreamSoundInfo::Track &track = *static_cast<const Sound::StreamSoundInfo::Track *>(tracks.nth(i)->val());

            if (!track.getWaveFileRef().isAttached())
                return fail(sead::FormatFixedSafeString<64>(messages::playback::cTrackNoWaveFileFormat, i).cstr());

            const WaveFile *wave = static_cast<const WaveFile *>(track.getWaveFileRef().getItem());
            waves[waveCount++] = wave;
            totalChannels += wave->getChannels().size();
        }

        if (totalChannels < 1 || totalChannels > nxopus::cMaxChannelCount)
            return fail(messages::opus::cEncodeChannelLimit);

        const WaveFile &mainWave = *waves[0];

        u32 sampleRate = mainWave.getSampleRate();

        if (!nxopus::IsSupportedSampleRate(sampleRate))
            return fail(messages::opus::cEncodeSampleRate);

        u32 sampleCount = 0;

        for (u32 i = 0; i < waveCount; i++)
        {
            if (waves[i]->getSampleRate() != sampleRate)
                return fail(messages::stream::cTracksSameSampleRate);

            if (waves[i]->getSampleCount() > sampleCount)
                sampleCount = waves[i]->getSampleCount();
        }

        if (sampleCount == 0)
            return fail(messages::wavefile::cNoData);

        std::vector<const std::vector<float> *> channels;
        std::vector<DecodedPcm> decoded(waveCount);

        for (u32 i = 0; i < waveCount; i++)
        {
            decoded[i] = decodeWaveFileForEditing(*waves[i]);
            if (!decoded[i].isValid())
                return fail(messages::opus::cEncodeDecodeFailed);

            for (const std::vector<float> &channel : decoded[i].channels)
                channels.push_back(&channel);
        }

        std::vector<u8> data;
        const nxopus::Error error = nxopus::Encode(channels, sampleCount, sampleRate, static_cast<s32>(GetEncodeChannelBitrate(streamInfo) * totalChannels), nxopus::PreSkip::Drop, &data);

        if (error == nxopus::Error::EncoderCreateFailed)
            return fail(messages::opus::cEncoderCreateFailed);

        if (error != nxopus::Error::None)
            return fail(messages::opus::cEncodeError);

        CheckedFileStreamSrc streamSrc(&handle);
        sead::WriteStream stream(&streamSrc, sead::Stream::Modes::eBinary);
        stream.writeMemBlock(data.data(), static_cast<u32>(data.size()));

        return !streamSrc.hasFailed();
    }
}
