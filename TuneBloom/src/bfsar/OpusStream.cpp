#include <bfsar/OpusStream.h>

#include <bfsar/Bfsar.h>
#include <bfsar/BfstmFile.h>
#include <bfsar/Sound.h>
#include <bfsar/WaveFile.h>
#include <bfsar/WaveFileEditDecode.h>

#include <ui/PopupMgr.h>
#include <ui/UI.h>

#include <prim/seadMemUtil.h>
#include <filedevice/seadFileDeviceMgr.h>
#include <filedevice/seadPath.h>
#include <stream/seadFileDeviceStream.h>

#include <opus.h>

#include <vector>

namespace opusstream
{

    namespace
    {

        constexpr u32 cInfoChunkId = 0x80000001;
        constexpr u32 cDataChunkId = 0x80000004;

        constexpr u32 cMaxFrameCount = 5760;

        u32 readLE32(const u8 *p)
        {
            return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
        }

        u32 readBE32(const u8 *p)
        {
            return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
        }

    }

    bool IsOpusStream(const void *data, u32 size)
    {
        if (!data || size < 8)
            return false;

        return readLE32(static_cast<const u8 *>(data)) == cInfoChunkId;
    }

    bool DecodeToPcm16(const void *data, u32 size, Info &outInfo, std::vector<s16 *> &outChannelData, const char **outError)
    {
        auto fail = [&](const char *msg)
        {
            if (outError)
                *outError = msg;
            return false;
        };

        const u8 *base = static_cast<const u8 *>(data);

        u32 infoOffset = 0;
        u32 dataOffset = 0;
        u32 dataSize = 0;
        bool hasInfo = false;
        bool hasData = false;

        for (u32 o = 0; o + 8 <= size;)
        {
            u32 id = readLE32(base + o);
            u32 chunkSize = readLE32(base + o + 4);

            if (chunkSize > size - o - 8)
                chunkSize = size - o - 8;

            if (id == cInfoChunkId && chunkSize >= 0x18)
            {
                infoOffset = o + 8;
                hasInfo = true;
            }
            else if (id == cDataChunkId)
            {
                dataOffset = o + 8;
                dataSize = chunkSize;
                hasData = true;
                break;
            }

            o += 8 + chunkSize;
        }

        if (!hasInfo || !hasData)
            return fail("Not a valid OpusStream file (missing info/data chunk)");

        u8 channelCount = base[infoOffset + 0x01];
        u32 sampleRate = readLE32(base + infoOffset + 0x04);
        u32 preSkip = readLE32(base + infoOffset + 0x14);

        if (channelCount < 1 || channelCount > 2)
            return fail("Unsupported OpusStream channel count (only mono/stereo)");

        if (sampleRate != 48000 && sampleRate != 24000 && sampleRate != 16000 && sampleRate != 12000 && sampleRate != 8000)
            return fail("Unsupported OpusStream sample rate");

        u64 totalFrames = 0;
        {
            u32 p = dataOffset;
            u32 end = dataOffset + dataSize;

            while (p + 8 <= end)
            {
                u32 packetLength = readBE32(base + p);
                p += 8;

                if (packetLength == 0 || packetLength > end - p)
                    return fail("Corrupt OpusStream packet framing");

                int frames = opus_packet_get_nb_samples(base + p, packetLength, sampleRate);

                if (frames < 0)
                    return fail("Invalid Opus packet");

                totalFrames += frames;
                p += packetLength;
            }
        }

        if (totalFrames <= preSkip)
            return fail("OpusStream has no audible samples");

        u32 sampleCount = static_cast<u32>(totalFrames - preSkip);

        int opusErr = OPUS_OK;
        OpusDecoder *decoder = opus_decoder_create(sampleRate, channelCount, &opusErr);

        if (!decoder || opusErr != OPUS_OK)
            return fail("Couldn't create the Opus decoder");

        std::vector<s16 *> channels(channelCount, nullptr);

        for (u32 c = 0; c < channelCount; c++)
        {
            channels[c] = reinterpret_cast<s16 *>(new u8[sampleCount * sizeof(s16)]);
        }

        auto cleanup = [&]()
        {
            opus_decoder_destroy(decoder);

            for (s16 *buf : channels)
            {
                delete[] reinterpret_cast<u8 *>(buf);
            }
        };

        std::vector<opus_int16> tmp(cMaxFrameCount * channelCount);

        u32 written = 0;
        u32 skipLeft = preSkip;
        {
            u32 p = dataOffset;
            u32 end = dataOffset + dataSize;

            while (p + 8 <= end)
            {
                u32 packetLength = readBE32(base + p);
                p += 8;

                int frames = opus_decode(decoder, base + p, packetLength, tmp.data(), cMaxFrameCount, 0);

                if (frames < 0)
                {
                    cleanup();
                    return fail("Opus decode error");
                }

                p += packetLength;

                const opus_int16 *src = tmp.data();
                u32 usable = static_cast<u32>(frames);

                if (skipLeft > 0)
                {
                    u32 drop = skipLeft < usable ? skipLeft : usable;
                    src += static_cast<size_t>(drop) * channelCount;
                    usable -= drop;
                    skipLeft -= drop;
                }

                if (written + usable > sampleCount)
                    usable = sampleCount - written;

                for (u32 c = 0; c < channelCount; c++)
                {
                    s16 *dst = channels[c] + written;
                    for (u32 i = 0; i < usable; i++)
                    {
                        dst[i] = src[static_cast<size_t>(i) * channelCount + c];
                    }
                }

                written += usable;
            }
        }

        opus_decoder_destroy(decoder);

        outInfo.channelCount = channelCount;
        outInfo.sampleRate = sampleRate;
        outInfo.preSkip = preSkip;
        outInfo.sampleCount = written;

        outChannelData = std::move(channels);

        return true;
    }

    bool AttachStreamWaves(Sound *sound)
    {
        Sound::StreamSoundInfo &strmSoundInfo = sound->getStreamSoundInfo();
        Sound::StreamSoundInfo::Track::List &tracks = strmSoundInfo.getTrackList();

        if (tracks.isEmpty())
        {
            PopupMgr::instance()->addPopup({"Streams must have at least 1 Track", nullptr});
            return false;
        }

        bool allAttached = true;

        for (u32 i = 0; i < tracks.size(); i++)
        {
            const Sound::StreamSoundInfo::Track &track = *static_cast<const Sound::StreamSoundInfo::Track *>(tracks.nth(i)->val());

            if (!track.getWaveFileRef().isAttached())
            {
                allAttached = false;
                break;
            }
        }

        if (allAttached)
            return true;

        sead::FileDevice *device = nullptr;

        if (sead::FileDeviceMgr::instance() != nullptr)
            device = sead::FileDeviceMgr::instance()->findDevice("native");

        if (!device)
            return false;

        u8 *fileData = nullptr;
        sead::FileDevice::LoadArg loadArg;

        const sead::SafeString *archivePaths[2] = {&sBfsar.getFilePath(), &sBfsar.getLoadedArchivePath()};
        for (const sead::SafeString *archivePath : archivePaths)
        {
            sead::FixedSafeString<512> dir;

            if (!sead::Path::getDirectoryName(&dir, *archivePath))
                continue;

            sead::FixedSafeString<512> path;
            path.format("%s/%s", dir.cstr(), strmSoundInfo.getPath().cstr());

            loadArg = sead::FileDevice::LoadArg();
            loadArg.path = path;

            fileData = device->tryLoad(loadArg);

            if (fileData)
                break;
        }

        if (!fileData)
        {
            sead::FormatFixedSafeString<1024> msg("Couldn't load '%s'\nThis should be relative to your .bfsar file", strmSoundInfo.getPath().cstr());
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
            sead::FormatFixedSafeString<1024> msg("'%s': %s", strmSoundInfo.getPath().cstr(), error ? error : "Opus decode failed");
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
            wave->mId = sBfsar.getWaveFileList().size();

            wave->mEnableName = true;
            if (tracks.size() == 1)
            {
                wave->mName.format("GUESS_%s", sound->getName().cstr());
            }
            else
            {
                wave->mName.format("GUESS_%s_TRACK_%u", sound->getName().cstr(), trackNo);
            }

            wave->mVersion = sBfsar.getVersionForBfwav();
            wave->mDataEndian = sead::Endian::Types::eLittle;
            wave->mEncoding = WaveFile::Encoding::Pcm16;
            wave->mIsLoop = isLoop;
            wave->mSampleRate = opusInfo.sampleRate;
            wave->mLoopStartFrame = loopStartFrame;
            wave->mLoopEndFrame = loopEndFrame;
            wave->mSampleCount = opusInfo.sampleCount;

            wave->mUseOriginalData = false;

            sBfsar.getWaveFileList().pushBack(wave);
            track.getWaveFileRef().attach(wave);

            for (s32 ch = 0; ch < track.getChannels_().size(); ch++)
            {
                if (ch >= snd::cWaveChannelMax)
                    break;

                s8 globalChannelIndex = *track.getChannels_().nth(ch);

                if (globalChannelIndex < 0 || static_cast<u32>(globalChannelIndex) >= channelBuffers.size())
                {
                    sead::FormatFixedSafeString<1024> msg("Track %u has no associated channels", trackNo);
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

        return true;
    }

    u64 ComputeContentSignature(const Sound &sound)
    {
        u64 h = 0xCBF29CE484222325ULL; // FNV-1a 64

        auto mix = [&](const void *p, size_t n)
        {
            const u8 *b = static_cast<const u8 *>(p);
            for (size_t i = 0; i < n; i++)
            {
                h ^= b[i];
                h *= 0x100000001B3ULL;
            }
        };

        auto mixU32 = [&](u32 v)
        {
            mix(&v, sizeof(v));
        };

        const Sound::StreamSoundInfo::Track::List &trackList = sound.getStreamSoundInfo().getTrackList();

        mixU32(trackList.size());

        for (u32 i = 0; i < trackList.size(); i++)
        {
            const Sound::StreamSoundInfo::Track *track = static_cast<const Sound::StreamSoundInfo::Track *>(trackList.nth(i)->val());

            mixU32(track->getWaveFileRef().getItemId());

            if (!track->getWaveFileRef().isAttached())
                continue;

            const WaveFile &wave = *static_cast<const WaveFile *>(track->getWaveFileRef().getItem());

            mixU32(static_cast<u32>(wave.getEncoding()));
            mixU32(wave.getSampleRate());
            mixU32(wave.getSampleCount());
            mixU32(static_cast<u32>(wave.getDataEndian()));

            const sead::ObjList<WaveFile::Channel> &channels = wave.getChannels();

            mixU32(channels.size());

            for (s32 ch = 0; ch < channels.size(); ch++)
            {
                const WaveFile::Channel &channel = *channels.nth(ch);
                u32 dataSize = channel.getDataSize();

                mixU32(dataSize);

                const u8 *data = static_cast<const u8 *>(channel.getData());

                if (data && dataSize)
                {
                    u32 sample = dataSize < 128 ? dataSize : 128;
                    mix(data, sample);
                    mix(data + dataSize - sample, sample);
                }
            }
        }

        return h;
    }

    bool WriteStreamFile(sead::FileHandle &handle, const Sound *sound)
    {
        const Sound::StreamSoundInfo &strmSoundInfo = sound->getStreamSoundInfo();
        const Sound::StreamSoundInfo::Track::List &tracks = strmSoundInfo.getTrackList();

        auto fail = [&](const char *msg)
        {
            sead::FormatFixedSafeString<1024> popupMsg("Can't encode Opus stream: %s", msg);
            PopupMgr::instance()->addPopup({popupMsg, const_cast<Sound *>(sound)});
            return false;
        };

        if (tracks.isEmpty())
            return fail("no Tracks");

        const WaveFile *waves[8] = {};
        u32 waveCount = 0;
        u32 totalChannels = 0;

        for (u32 i = 0; i < tracks.size() && i < 8; i++)
        {
            const Sound::StreamSoundInfo::Track &track = *static_cast<const Sound::StreamSoundInfo::Track *>(tracks.nth(i)->val());

            if (!track.getWaveFileRef().isAttached())
                return fail("a Track has no Wave File attached");

            const WaveFile *wave = static_cast<const WaveFile *>(track.getWaveFileRef().getItem());
            waves[waveCount++] = wave;
            totalChannels += wave->getChannels().size();
        }

        if (totalChannels < 1 || totalChannels > 2)
            return fail("Opus streams support 1 or 2 channels total");

        const WaveFile &mainWave = *waves[0];

        u32 sampleRate = mainWave.getSampleRate();

        if (sampleRate != 48000 && sampleRate != 24000 && sampleRate != 16000 && sampleRate != 12000 && sampleRate != 8000)
            return fail("Sample rate must be 8000, 12000, 16000, 24000 or 48000 Hz");

        u32 sampleCount = 0;

        for (u32 i = 0; i < waveCount; i++)
        {
            if (waves[i]->getSampleRate() != sampleRate)
                return fail("all Tracks must have the same sample rate");

            if (waves[i]->getSampleCount() > sampleCount)
                sampleCount = waves[i]->getSampleCount();
        }

        if (sampleCount == 0)
            return fail("no sample data");

        std::vector<const std::vector<float> *> channels;
        std::vector<DecodedPcm> decoded(waveCount);

        for (u32 i = 0; i < waveCount; i++)
        {
            decoded[i] = decodeWaveFileForEditing(*waves[i]);
            if (!decoded[i].isValid())
                return fail("couldn't decode a Track's Wave File");

            for (const std::vector<float> &channel : decoded[i].channels)
                channels.push_back(&channel);
        }

        int opusErr = OPUS_OK;

        OpusEncoder *encoder = opus_encoder_create(sampleRate, totalChannels, OPUS_APPLICATION_AUDIO, &opusErr);

        if (!encoder || opusErr != OPUS_OK)
            return fail("couldn't create the Opus encoder");

        opus_encoder_ctl(encoder, OPUS_SET_BITRATE(256000));

        opus_int32 lookahead = 0;
        opus_encoder_ctl(encoder, OPUS_GET_LOOKAHEAD(&lookahead));
        u32 preSkip = static_cast<u32>(lookahead);

        const u32 frameSize = sampleRate / 50;

        std::vector<float> frame(static_cast<size_t>(frameSize) * totalChannels);
        std::vector<u8> packet(4000);
        std::vector<u8> data;

        data.reserve(static_cast<size_t>(sampleCount) / 2);

        u64 samplesToFeed = static_cast<u64>(sampleCount) + preSkip;

        for (u64 pos = 0; pos < samplesToFeed; pos += frameSize)
        {
            for (u32 s = 0; s < frameSize; s++)
            {
                u64 srcIdx = pos + s;

                for (u32 c = 0; c < totalChannels; c++)
                {
                    const std::vector<float> &src = *channels[c];
                    frame[static_cast<size_t>(s) * totalChannels + c] = srcIdx < src.size() ? src[srcIdx] : 0.0f;
                }
            }

            opus_int32 packetLength = opus_encode_float(encoder, frame.data(), frameSize, packet.data(), static_cast<opus_int32>(packet.size()));

            if (packetLength < 0)
            {
                opus_encoder_destroy(encoder);
                return fail("Opus encode error");
            }

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

            data.insert(data.end(), header, header + 8);
            data.insert(data.end(), packet.data(), packet.data() + packetLength);
        }

        opus_encoder_destroy(encoder);

        sead::FileDeviceWriteStream stream(&handle, sead::Stream::Modes::eBinary);
        stream.setBinaryEndian(sead::Endian::Types::eLittle);

        // Info chunk
        stream.writeU32(cInfoChunkId);
        stream.writeU32(0x18);
        stream.writeU8(0); // version
        stream.writeU8(static_cast<u8>(totalChannels));
        stream.writeU16(0); // frame size (0 = variable)
        stream.writeU32(sampleRate);
        stream.writeU32(0x20); // data chunk offset
        stream.writeU32(0);
        stream.writeU32(0);
        stream.writeU32(preSkip);

        // Data chunk
        stream.writeU32(cDataChunkId);
        stream.writeU32(static_cast<u32>(data.size()));
        stream.writeMemBlock(data.data(), static_cast<u32>(data.size()));

        return true;
    }
}