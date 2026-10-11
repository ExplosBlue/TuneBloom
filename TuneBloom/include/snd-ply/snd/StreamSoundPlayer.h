#pragma once

#include "BasicSoundPlayer.h"
#include "Channel.h"
#include "SoundThread.h"

#include "snd/snd_SoundArchive.h"

#include <bfsar/model/Sound.h>

#include <atomic>

static const u32 cStrmChannelNum = 16;
static const u32 cStrmTrackNum = 8;

struct StreamChannel
{
    void* mBufferAddress;
    snd::internal::driver::Channel* mVoice;
    snd::UpdateType mUpdateType;

    WaveFile::Channel::SeekInfo* mSeekInfo;
    u32 mSeekInfoCount;
};

struct StreamTrack
{
public:
    bool mActiveFlag;
    StreamChannel* mChannels[snd::cWaveChannelMax];

    u8 channelCount;
    u8 volume;
    u8 pan;
    u8 span;
    u8 mainSend;
    u8 fxSend[nw::snd::AUX_BUS_NUM];
    u8 lpfFreq;
    s8 biquadType;
    u8 biquadValue;
    u8 flags;

    f32 mVolume;
    s32 mOutputLine;

    std::atomic<f32> mPeak[snd::cWaveChannelMax] = {};

    snd::internal::OutputParam mParam;
};

class StreamSoundPlayer : public BasicSoundPlayer, public snd::internal::driver::SoundThread::PlayerCallback
{
public:
    static void channelCallbackFunc(snd::internal::driver::Channel* dropChannel, snd::internal::driver::Channel::ChannelCallbackStatus, void* arg);

    struct SetupArg
    {
        u32 allocChannelCount;
        u16 allocTrackFlag;

        nw::snd::SoundArchive::StreamTrackInfo tracks[cStrmTrackNum];

        f32 pitch;
        u8 mainSend;
        u8 fxSend[nw::snd::AUX_BUS_NUM];
    };

    struct ItemData
    {
        f32 pitch;
        f32 mainSend;
        f32 fxSend[nw::snd::AUX_BUS_NUM];

        void set(const SetupArg& arg)
        {
            // pitch
            pitch = arg.pitch;

            // send
            mainSend = static_cast<f32>(arg.mainSend) / 127.0f - 1.0f;
            for (u32 i = 0; i < nw::snd::AUX_BUS_NUM; i++)
            {
                fxSend[i] = static_cast<f32>(arg.fxSend[i]) / 127.0f;
            }
        }
    };

    struct TrackData
    {
        f32 volume;
        f32 lpfFreq;
        s32 biquadType;
        f32 biquadValue;
        f32 pan;
        f32 span;
        f32 mainSend;
        f32 fxSend[nw::snd::AUX_BUS_NUM];

        void set(const StreamTrack* track)
        {
            // volume
            volume = static_cast<f32>(track->volume) / 127.0f;

            // lpf freq
            lpfFreq = static_cast<f32>(track->lpfFreq - 64) / 64.0f;

            // biquad filter;
            biquadType = static_cast<s32>(track->biquadType);
            biquadValue = static_cast<f32>(track->biquadValue) / 127.0f;

            // Pan
            if (track->pan <= 1)
                pan = static_cast<f32>(static_cast<s32>(track->pan) - 63) / 63.0f;
            else
                pan = static_cast<f32>(static_cast<s32>(track->pan) - 64) / 63.0f;

            // span
            if (track->span <= 63)
                span = static_cast<f32>(track->span) / 63.0f;
            else
                span = static_cast<f32>(track->span + 1) / 64.0f;

            // send
            mainSend = static_cast<f32>(track->mainSend) / 127.0f - 1.0f;
            for (u32 i = 0; i < nw::snd::AUX_BUS_NUM; i++)
            {
                fxSend[i] = static_cast<f32>(track->fxSend[i]) / 127.0f;
            }
        }
    };

public:
    StreamSoundPlayer();

    void onUpdateFrameSoundThread() override
    {
        update();
    }

    void onUpdateFrameSoundThreadWithAudioFrameFrequency() override
    {
        if (mUpdateType == snd::UpdateType::AudioFrame)
            update();
    }

    void init();
    void deinit(bool freeBuffers = true);
    void fadeOut();

    bool hasBuffers() const
    {
        for (u32 ch = 0; ch < cStrmChannelNum; ch++)
        {
            if (mChannels[ch].mBufferAddress)
                return true;
        }

        return false;
    }
    void setup(const SetupArg& arg);
    void prepare(const void* strmFile, snd::UpdateType updateType = snd::UpdateType::AudioFrame);
    void prepare(const Sound::StreamSoundInfo& streamSoundInfo, snd::UpdateType updateType = snd::UpdateType::AudioFrame);

    void pause(bool flag) override;
    bool seek(u32 offsetSample);
    void setLoopEnabled(bool enable);

    struct Region
    {
        s32 no = -1;
        u32 start = 0;
        u32 end = 0;
    };

    bool playRegion(const Region& region);
    void setNextRegion(const Region& region);
    void clearNextRegion();
    s32 getPlayingRegionNo() const;
    s32 getNextRegionNo() const;
    void renumberRegions(const s32* newNos, s32 count);

    void applyTrackDataInfo();
    bool allocVoices();

    void startPlayer(const nw::snd::internal::WaveInfo* waveInfos, u32 startOffsetSample, const Region* region = nullptr);

    void update();
    void updateVoiceParams(StreamTrack* track);
    void updateTrackPeak(StreamTrack* track);
    void setOutputParam(snd::internal::OutputParam* pOutputParam, const snd::internal::OutputParam& trackParam, const TrackData& trackData);
    void mixSettingForOutputParam(snd::internal::OutputParam* pOutputParam, s32 channelIndex, snd::MixMode mixMode);
    void applyTvOutputParamForMultiChannel(snd::internal::OutputParam* pOutputParam, snd::internal::driver::Channel* pVoice, s32 channelIndex, snd::MixMode mixMode);

    void setTrackVolume(u32 trackBitFlag, f32 volume);
    void setItemData(const SetupArg& arg) { mItemData.set(arg); }
    void setTrackDataInfo(u32 trackIndex, const nw::snd::SoundArchive::StreamTrackInfo& trackInfo);

    s32 getPlaySamplePosition(bool isOriginalSamplePosition) const;

    StreamTrack* getPlayerTrack(s32 trackNo);
    const StreamTrack* getPlayerTrack(s32 trackNo) const;

    bool isPrepared() const
    {
        return mPrepared;
    }

    u32 getTrackCount() const
    {
        return mTrackCount;
    }

    f32 fetchTrackPeak(u32 trackNo, u32 channel)
    {
        return mTracks[trackNo].mPeak[channel].exchange(0.0f, std::memory_order_relaxed);
    }

    u32 getSampleRate() const
    {
        return mSampleRate;
    }

    u32 getSampleCount() const
    {
        return mSampleCount;
    }

private:
    bool restartVoices_();
    void setSeekInfo_(u32 channelIndex, const WaveFile::Channel::SeekData& seekData);
    void freeSeekInfo_(u32 channelIndex);
    void calcAdpcmContext_(snd::AdpcmContext* outContext, const snd::internal::WaveInfo& waveInfo, u32 channelIndex, u32 startSample) const;
    bool makeChannelRegion_(snd::internal::driver::Channel::Region* channelRegion, const snd::internal::WaveInfo& waveInfo, u32 channelIndex, const Region& region) const;

protected:
    bool mSetup;
    bool mPrepared;
    bool mIsRegisterPlayerCallback;
    bool mLoopEnabled = true;

    ItemData mItemData;

    u32 mChannelCount;
    u32 mTrackCount;
    StreamChannel mChannels[cStrmChannelNum];
    StreamTrack mTracks[cStrmTrackNum];

    nw::snd::internal::WaveInfo mWaveInfos[cStrmChannelNum];

    nw::snd::SoundArchive::StreamTrackInfo mStreamDataInfoTracks[cStrmTrackNum];

    snd::UpdateType mUpdateType;

    u32 mSampleRate;
    u32 mSampleCount;
};
