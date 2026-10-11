#include <ui/export/ExportInternal.h>

#include <ui/Messages.h>
#include <ui/app/PopupMgr.h>

#include <bfsar/model/Sound.h>
#include <bfsar/audio/WavWriter.h>
#include <bfsar/files/WaveFile.h>
#include <bfsar/audio/WaveFileEditDecode.h>

#include <snd/snd_Util.h>

#include <cstring>
#include <vector>

static void DecodeWaveFileChannels(const WaveFile* wave, std::vector<std::vector<s16>>& outChannels)
{
    u32 sampleCount = wave->getSampleCount();
    const auto& channels = wave->getChannels();
    u32 numChannels = channels.size();
    WaveFile::Encoding encoding = wave->getEncoding();

    outChannels.resize(numChannels);

    for (u32 ch = 0; ch < numChannels; ch++)
    {
        const WaveFile::Channel* channel = channels.nth(ch);
        outChannels[ch].resize(sampleCount);
        s16* dst = outChannels[ch].data();

        switch (encoding)
        {
            case WaveFile::Encoding::Pcm8:
            {
                const s8* src = static_cast<const s8*>(channel->getData());
                for (u32 i = 0; i < sampleCount; i++)
                    dst[i] = static_cast<s16>(src[i]) << 8;
                break;
            }
            case WaveFile::Encoding::Pcm16:
            {
                const s16* src = static_cast<const s16*>(channel->getData());
                for (u32 i = 0; i < sampleCount; i++)
                    dst[i] = sead::Endian::convertS16(wave->getDataEndian(), sead::Endian::eLittle, src[i]);
                break;
            }
            case WaveFile::Encoding::DspAdpcm:
            {
                ADPCMINFO adpcmInfo;
                sead::MemUtil::fillZero(&adpcmInfo, sizeof(adpcmInfo));
                FillAdpcmInfo(&adpcmInfo, channel->getAdpcmParam(false), channel->getAdpcmLoopParam(false));
                decode(const_cast<u8*>(static_cast<const u8*>(channel->getData())), dst, &adpcmInfo, sampleCount);
                break;
            }
        }
    }
}

const WaveFile* FindStreamExportWave(const Sound* sound)
{
    const auto& trackList = sound->getStreamSoundInfo().getTrackList();

    for (auto it = trackList.robustBegin(); it != trackList.robustEnd(); ++it)
    {
        const Sound::StreamSoundInfo::Track* track = static_cast<const Sound::StreamSoundInfo::Track*>(it->val());
        const Item* waveItem = track->getWaveFileRef().getItem();

        if (waveItem && static_cast<const WaveFile*>(waveItem)->getSampleRate() != 0)
            return static_cast<const WaveFile*>(waveItem);
    }

    return nullptr;
}

bool HasExportLoop(const WaveFile* wave)
{
    return wave->getIsLoop() && wave->getOriginalLoopEndFrame() > wave->getOriginalLoopStartFrame() && wave->getOriginalLoopEndFrame() <= wave->getSampleCount();
}

static constexpr u64 cExportSampleMax = 0x20000000;

static void ApplyLoopAndFade(std::vector<std::vector<s16>>& channels, u32 loopStart, u32 loopEnd, u32 loopCount, u32 fadeSamples)
{
    if (loopEnd <= loopStart)
        return;

    const u32 loopLength = loopEnd - loopStart;
    const u64 fullLength = static_cast<u64>(loopStart) + static_cast<u64>(loopLength) * loopCount + fadeSamples;

    if (fullLength > cExportSampleMax)
        return;

    const u32 totalLength = static_cast<u32>(fullLength);
    const u32 fadeStart = totalLength - fadeSamples;

    for (std::vector<s16>& channel : channels)
    {
        std::vector<s16> original = std::move(channel);

        if (original.size() < loopEnd)
            original.resize(loopEnd);

        channel.resize(totalLength);

        for (u32 i = 0; i < totalLength; i++)
        {
            const s16 sample = original[i < loopStart ? i : loopStart + (i - loopStart) % loopLength];

            if (i < fadeStart)
                channel[i] = sample;
            else
                channel[i] = static_cast<s16>(sample * (1.0f - static_cast<f32>(i - fadeStart) / fadeSamples));
        }
    }
}

static void ResampleChannels(std::vector<std::vector<s16>>& channels, u32 srcRate, u32 dstRate)
{
    if (srcRate == dstRate || channels.empty())
        return;

    u32 numChannels = channels.size();
    u32 srcLen = channels[0].size();
    u32 dstLen = static_cast<u32>((static_cast<u64>(srcLen) * dstRate) / srcRate);
    if (dstLen < 1) dstLen = 1;

    for (u32 ch = 0; ch < numChannels; ch++)
    {
        std::vector<s16> src = std::move(channels[ch]);
        channels[ch].resize(dstLen);

        for (u32 i = 0; i < dstLen; i++)
        {
            f64 pos = (static_cast<f64>(i) * srcLen) / dstLen;
            u32 idx = static_cast<u32>(pos);
            f32 frac = static_cast<f32>(pos - idx);

            s16 s0 = src[sead::Mathu::min(idx, srcLen - 1)];
            s16 s1 = src[sead::Mathu::min(idx + 1, srcLen - 1)];
            channels[ch][i] = static_cast<s16>(s0 + (s1 - s0) * frac);
        }
    }
}

static std::vector<StreamRegion> MakeExportRegions(const Sound* sound, u32 sampleRate, size_t sampleCount)
{
    std::vector<StreamRegion> regions;
    const WaveFile* mainWave = sound->getStreamSoundInfo().getMainWave();

    if (!mainWave || mainWave->getSampleRate() == 0)
        return regions;

    const f64 rateScale = static_cast<f64>(sampleRate) / mainWave->getSampleRate();

    for (StreamRegion region : mainWave->getStreamFileLayout().regions)
    {
        region.start = static_cast<u32>(region.start * rateScale);
        region.end = static_cast<u32>(region.end * rateScale);

        if (region.start < region.end && region.end <= sampleCount)
            regions.push_back(region);
    }

    return regions;
}

bool ExportStreamSoundToWav(Sound* sound, const char* path, bool multiChannel, u32 loopCount, f32 fadeSec, u32 targetRate, bool loopMarkers)
{
    const WaveFile* loopWave = FindStreamExportWave(sound);

    auto& trackList = sound->getStreamSoundInfo().getTrackList();

    std::vector<std::vector<std::vector<s16>>> trackChannels;
    std::vector<u32> trackSampleRates;

    for (auto it = trackList.robustBegin(); it != trackList.robustEnd(); ++it)
    {
        Sound::StreamSoundInfo::Track* track = static_cast<Sound::StreamSoundInfo::Track*>(it->val());
        Item* waveItem = track->getWaveFileRef().getItem();
        if (!waveItem)
            continue;

        const WaveFile* wave = static_cast<const WaveFile*>(waveItem);
        if (wave->getSampleRate() == 0)
            continue;

        trackChannels.emplace_back();
        DecodeWaveFileChannels(wave, trackChannels.back());
        trackSampleRates.push_back(wave->getSampleRate());
    }

    if (trackChannels.empty())
        return false;

    if (targetRate == 0)
        targetRate = trackSampleRates.front();

    for (u32 i = 0; i < trackChannels.size(); i++)
    {
        if (trackSampleRates[i] != targetRate)
            ResampleChannels(trackChannels[i], trackSampleRates[i], targetRate);
    }

    const u32 sampleRate = targetRate;

    std::vector<std::vector<s16>> allChannels;
    std::vector<u32> trackChannelOffset;

    for (auto& tc : trackChannels)
    {
        trackChannelOffset.push_back(allChannels.size());
        for (auto& ch : tc)
            allChannels.push_back(std::move(ch));
    }

    WavLoop loop;

    if (loopWave && HasExportLoop(loopWave))
    {
        const f64 rateScale = static_cast<f64>(sampleRate) / loopWave->getSampleRate();

        loop.enabled = loopMarkers;
        loop.start = static_cast<u32>(loopWave->getOriginalLoopStartFrame() * rateScale);
        loop.end = static_cast<u32>(loopWave->getOriginalLoopEndFrame() * rateScale);

        ApplyLoopAndFade(allChannels, loop.start, loop.end, loopCount, static_cast<u32>(fadeSec * sampleRate));
    }

    const std::vector<StreamRegion> regions = MakeExportRegions(sound, sampleRate, allChannels.empty() ? 0 : allChannels[0].size());

    if (multiChannel)
    {
        if (!WritePcm16Wav(path, { sampleRate, allChannels, loop, regions }))
        {
            PopupMgr::instance()->addPopup({ messages::exporting::cMultiChannelWriteFailed, nullptr });
            return false;
        }
    }
    else
    {
        bool anyFailed = false;
        u32 trackIdx = 0;
        for (auto& tc : trackChannels)
        {
            u32 offset = trackChannelOffset[trackIdx];
            std::vector<std::vector<s16>> trackChs;
            for (u32 i = 0; i < tc.size(); i++)
            {
                if (offset + i < allChannels.size())
                    trackChs.push_back(allChannels[offset + i]);
            }

            sead::FixedSafeString<512> trackPath;
            BuildTrackExportPath(&trackPath, path, trackIdx);

            if (!WritePcm16Wav(trackPath, { sampleRate, trackChs, loop, trackIdx == 0 ? regions : std::vector<StreamRegion>() }))
                anyFailed = true;
            trackIdx++;
        }
        if (anyFailed)
        {
            PopupMgr::instance()->addPopup({ messages::exporting::cTrackWavsWriteFailed, nullptr });
            return false;
        }
    }

    return true;
}
