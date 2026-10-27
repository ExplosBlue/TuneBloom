#include <bfsar/files/OpusStream.h>

#include <bfsar/archive/Bfsar.h>
#include <bfsar/files/BfstmFile.h>
#include <bfsar/audio/NxOpus.h>
#include <bfsar/model/Sound.h>
#include <bfsar/files/WaveFile.h>
#include <bfsar/audio/WaveFileEditDecode.h>

#include <ui/Messages.h>
#include <ui/app/PopupMgr.h>
#include <ui/app/UI.h>

#include <prim/seadMemUtil.h>
#include <filedevice/seadFileDeviceMgr.h>
#include <stream/seadFileDeviceStream.h>

#include <opus.h>

#include <algorithm>
#include <vector>

namespace opusstream
{

    namespace
    {

        constexpr u32 cMaxChannelCount = 2;
        constexpr u8 cInfoVersion = 0;
        constexpr u16 cVariableFrameSize = 0;

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

        bool encodePackets(OpusEncoder *encoder, const std::vector<const std::vector<float> *> &channels, u32 channelCount, u32 frameSize, u64 samplesToFeed, std::vector<u8> *outData)
        {
            std::vector<float> frame(static_cast<size_t>(frameSize) * channelCount);
            std::vector<u8> packet(4000);

            for (u64 pos = 0; pos < samplesToFeed; pos += frameSize)
            {
                for (u32 s = 0; s < frameSize; s++)
                {
                    u64 srcIdx = pos + s;

                    for (u32 c = 0; c < channelCount; c++)
                    {
                        const std::vector<float> &src = *channels[c];
                        frame[static_cast<size_t>(s) * channelCount + c] = srcIdx < src.size() ? src[srcIdx] : 0.0f;
                    }
                }

                opus_int32 packetLength = opus_encode_float(encoder, frame.data(), frameSize, packet.data(), static_cast<opus_int32>(packet.size()));

                if (packetLength < 0)
                    return false;

                opus_uint32 finalRange = 0;
                opus_encoder_ctl(encoder, OPUS_GET_FINAL_RANGE(&finalRange));

                u8 header[8] = {
                    static_cast<u8>(packetLength >> 24),
                    static_cast<u8>(packetLength >> 16),
                    static_cast<u8>(packetLength >> 8),
                    static_cast<u8>(packetLength),
                    static_cast<u8>(finalRange >> 24),
                    static_cast<u8>(finalRange >> 16),
                    static_cast<u8>(finalRange >> 8),
                    static_cast<u8>(finalRange),
                };

                outData->insert(outData->end(), header, header + 8);
                outData->insert(outData->end(), packet.data(), packet.data() + packetLength);
            }

            return true;
        }

        bool areAllTracksAttached(const Sound::StreamSoundInfo::Track::List &tracks)
        {
            for (u32 i = 0; i < tracks.size(); i++)
            {
                const Sound::StreamSoundInfo::Track &track = *static_cast<const Sound::StreamSoundInfo::Track *>(tracks.nth(i)->val());

                if (!track.getWaveFileRef().isAttached())
                    return false;
            }

            return true;
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

        return info.channelCount >= 1 && info.channelCount <= cMaxChannelCount;
    }

    bool IsOpusStream(const void *data, u32 size)
    {
        return nxopus::HasInfoChunk(data, size);
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

    bool AttachStreamWaves(Bfsar &archive, Sound *sound, const sead::SafeString *filePathOverride)
    {
        Sound::StreamSoundInfo &strmSoundInfo = sound->getStreamSoundInfo();
        Sound::StreamSoundInfo::Track::List &tracks = strmSoundInfo.getTrackList();

        if (tracks.isEmpty())
        {
            PopupMgr::instance()->addPopup({messages::stream::cNeedsTrack, nullptr});
            return false;
        }

        if (areAllTracksAttached(tracks))
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
            PopupMgr::instance()->addPopup({msg, sound});

            return false;
        }

        Info opusInfo;
        std::vector<s16 *> channelBuffers;
        const char *error = nullptr;

        bool decoded = DecodeToPcm16(fileData, static_cast<u32>(loadArg.read_size), opusInfo, channelBuffers, &error);
        device->unload(fileData);

        if (!decoded)
        {
            sead::FormatFixedSafeString<1024> msg("'%s': %s", strmSoundInfo.getPath().cstr(), error ? error : messages::opus::cDecodeFailed);
            PopupMgr::instance()->addPopup({msg, sound});
            return false;
        }

        bool isLoop = strmSoundInfo.getIsLoop();
        u32 loopStartFrame = isLoop ? strmSoundInfo.getLoopStartFrame() : 0;
        u32 loopEndFrame = isLoop ? strmSoundInfo.getLoopEndFrame() : opusInfo.sampleCount;

        if (loopEndFrame == 0 || loopEndFrame > opusInfo.sampleCount)
            loopEndFrame = opusInfo.sampleCount;

        if (loopStartFrame >= loopEndFrame)
        {
            isLoop = false;
            loopStartFrame = 0;
        }

        std::vector<bool> channelBufferUsed(channelBuffers.size(), false);

        for (u32 trackNo = 0; trackNo < tracks.size(); trackNo++)
        {
            Sound::StreamSoundInfo::Track &track = *static_cast<Sound::StreamSoundInfo::Track *>(tracks.nth(trackNo)->val());

            if (track.getWaveFileRef().isAttached())
                continue;

            WaveFile *wave = new WaveFile();
            wave->setArchive(&archive);
            wave->mId = archive.getWaveFileList().size();
            wave->mIsFromStreamFile = true;

            wave->mEnableName = true;
            if (tracks.size() == 1)
            {
                wave->mName.format("GUESS_%s", sound->getName().cstr());
            }
            else
            {
                wave->mName.format("GUESS_%s_TRACK_%u", sound->getName().cstr(), trackNo);
            }

            wave->mVersion = archive.getVersionForBfwav();
            wave->mDataEndian = sead::Endian::Types::eLittle;
            wave->mEncoding = WaveFile::Encoding::Pcm16;
            wave->mIsLoop = isLoop;
            wave->mSampleRate = opusInfo.sampleRate;
            wave->mLoopStartFrame = loopStartFrame;
            wave->mLoopEndFrame = loopEndFrame;
            wave->mSampleCount = opusInfo.sampleCount;

            wave->mUseOriginalData = false;

            archive.getWaveFileList().pushBack(wave);
            track.getWaveFileRef().attach(wave);

            for (s32 ch = 0; ch < track.getChannels_().size(); ch++)
            {
                if (ch >= snd::cWaveChannelMax)
                    break;

                s8 globalChannelIndex = *track.getChannels_().nth(ch);

                if (globalChannelIndex < 0 || static_cast<u32>(globalChannelIndex) >= channelBuffers.size())
                {
                    sead::FormatFixedSafeString<1024> msg(messages::playback::cTrackNoChannelsFormat, trackNo);
                    PopupMgr::instance()->addPopup({msg, sound});
                    continue;
                }

                WaveFile::Channel *channel = wave->mChannels.birthBack();
                SEAD_ASSERT(channel);

                u8 *channelData = reinterpret_cast<u8 *>(channelBuffers[globalChannelIndex]);

                if (channelBufferUsed[globalChannelIndex])
                {
                    u8 *copy = new u8[opusInfo.sampleCount * sizeof(s16)];
                    sead::MemUtil::copy(copy, channelData, opusInfo.sampleCount * sizeof(s16));
                    channelData = copy;
                }

                channel->mOwnsData = true;
                channel->mData = channelData;
                channel->mDataSize = opusInfo.sampleCount * sizeof(s16);
                channel->mDataSizeMin = loopEndFrame * sizeof(s16);
                channel->mOriginalDataOffset = 0;

                channelBufferUsed[globalChannelIndex] = true;
            }

            wave->mIsLoopDirty = false;
            wave->mIsStreamExtended = true;
        }

        for (u32 i = 0; i < channelBuffers.size(); i++)
        {
            if (!channelBufferUsed[i])
            {
                delete[] reinterpret_cast<u8 *>(channelBuffers[i]);
            }
        }

        sound->mStreamFileSignature = ComputeContentSignature(*sound);
        sound->mHasStreamFileBaseline = true;

        if (!wasModified)
            sound->captureBaseline();

        return true;
    }

    u64 ComputeContentSignature(const Sound &sound)
    {
        StateHash hash;

        const Sound::StreamSoundInfo::Track::List &trackList = sound.getStreamSoundInfo().getTrackList();

        hash.mixValue<u32>(trackList.size());

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

    bool WriteStreamFile(sead::FileHandle &handle, const Sound *sound)
    {
        const Sound::StreamSoundInfo &strmSoundInfo = sound->getStreamSoundInfo();
        const Sound::StreamSoundInfo::Track::List &tracks = strmSoundInfo.getTrackList();

        auto fail = [&](const char *msg)
        {
            sead::FormatFixedSafeString<1024> popupMsg(messages::opus::cEncodeFailedFormat, msg);
            PopupMgr::instance()->addPopup({popupMsg, const_cast<Sound *>(sound)});
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

        if (totalChannels < 1 || totalChannels > 2)
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

        int opusErr = OPUS_OK;

        OpusEncoder *encoder = opus_encoder_create(sampleRate, totalChannels, OPUS_APPLICATION_AUDIO, &opusErr);

        if (!encoder || opusErr != OPUS_OK)
            return fail(messages::opus::cEncoderCreateFailed);

        opus_encoder_ctl(encoder, OPUS_SET_BITRATE(256000));

        opus_int32 lookahead = 0;
        opus_encoder_ctl(encoder, OPUS_GET_LOOKAHEAD(&lookahead));
        u32 preSkip = static_cast<u32>(lookahead);

        std::vector<u8> data;
        data.reserve(static_cast<size_t>(sampleCount) / 2);

        const bool encoded = encodePackets(encoder, channels, totalChannels, sampleRate / 50, static_cast<u64>(sampleCount) + preSkip, &data);

        opus_encoder_destroy(encoder);

        if (!encoded)
            return fail(messages::opus::cEncodeError);

        CheckedFileStreamSrc streamSrc(&handle);
        sead::WriteStream stream(&streamSrc, sead::Stream::Modes::eBinary);
        stream.setBinaryEndian(sead::Endian::Types::eLittle);

        stream.writeU32(nxopus::cInfoChunkId);
        stream.writeU32(nxopus::cInfoChunkSize);
        stream.writeU8(cInfoVersion);
        stream.writeU8(static_cast<u8>(totalChannels));
        stream.writeU16(cVariableFrameSize);
        stream.writeU32(sampleRate);
        stream.writeU32(nxopus::cChunkHeaderSize + nxopus::cInfoChunkSize);
        stream.writeU32(0);
        stream.writeU32(0);
        stream.writeU32(preSkip);

        stream.writeU32(nxopus::cDataChunkId);
        stream.writeU32(static_cast<u32>(data.size()));
        stream.writeMemBlock(data.data(), static_cast<u32>(data.size()));

        return !streamSrc.hasFailed();
    }
}
