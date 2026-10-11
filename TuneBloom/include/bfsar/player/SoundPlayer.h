#pragma once

#include <snd/FinalMixCallback.h>
#include <snd/SequenceNoteOnCallback.h>
#include <snd/SequenceSoundPlayer.h>
#include <snd/StreamSoundPlayer.h>
#include <snd/WaveSoundPlayer.h>

#include <bfsar/seq/SeqTimeline.h>
#include <bfsar/player/TrackSwitch.h>

#include <chrono>
#include <vector>

struct Pcm16Wav;

struct WaveFileScale
{
    f32 volume = 1.0f;
    f32 pitch = 1.0f;
};

class SoundPlayer
{
public:
    static const u32 cMaxTracks = 16;
    static constexpr s32 cPlaybackErrorLength = 128;

    static constexpr f32 cDefaultVolume        = 1.0f;
    static constexpr f32 cDefaultPitch         = 1.0f;
    static constexpr f32 cDefaultPan           = 0.0f;
    static constexpr f32 cDefaultLpfFreq       = 0.0f;
    static constexpr s32 cDefaultBiquadType    = static_cast<s32>(snd::BiquadFilterType::Inherit);
    static constexpr f32 cDefaultBiquadValue   = 0.0f;
    static constexpr f32 cDefaultSeqTempoRatio = 1.0f;
    static constexpr f32 cDefaultTrackVolume   = 1.0f;

    struct SeqVarInfo
    {
        SeqVarInfo()
            : enable(false), value(-1)
        {
        }

        bool enable;
        s16 value;
    };

    enum class SeqVarScope
    {
        Global,
        Player,
        Track
    };

    struct SeqExportSettings
    {
        u32 loopCount = 2;
        f32 fadeSec = 10.0f;
        u32 noEndSecs = 120;
        u32 sampleRate = 0;
        s32 soloTrack = -1;
    };

public:
    SoundPlayer()
        : mVolume(cDefaultVolume)
        , mPitch(cDefaultPitch)
        , mPan(cDefaultPan)
        , mLPF(cDefaultLpfFreq)
        , mBiquadType(cDefaultBiquadType)
        , mBiquadValue(cDefaultBiquadValue)
        , mSeqTempoRatio(cDefaultSeqTempoRatio)

        , mPlayingSound(nullptr)
        , mLastPlayedSound(nullptr)
        , mPlayingWaveFile(nullptr)
        , mCurrentPlayer(nullptr)
        , mSampleRate(0)
        , mSampleCount(0)
    {
        for (u32 i = 0; i < cMaxTracks; i++)
        {
            mTrackVolume[i] = cDefaultTrackVolume;
        }

        for (u32 i = 0; i < cMaxBankVoices; i++)
        {
            mBankVoiceKey[i] = -1;
            mBankVoiceAge[i] = 0;
            mBankVoiceHeld[i] = false;
        }
    }

    void update();

    void fadeOutPlayers();
    void fadeOutAllVoices();
    void stopAllPlayers();
    void stopAllVoices();

    static bool GetSoundPlaybackError(const Sound& sound, sead::BufferedSafeString* outError);
    static bool GetWaveFilePlaybackError(const WaveFile& wave, sead::BufferedSafeString* outError);
    static bool IsStreamFileNotLoaded(const Sound& sound);

    static bool CanPlaySound(const Sound& sound)
    {
        sead::FixedSafeString<cPlaybackErrorLength> error;
        return !GetSoundPlaybackError(sound, &error);
    }

    bool playSound(const Sound* sound, u32 startOffsetSample = 0);
    bool playLastSound();

    bool playSeqFile(const SequenceFile& seqFile, const sead::SafeString& startLabel, const Bank** bankArray, const Sound* sound = nullptr);
    bool playWaveFile(const WaveFile& wave, s32 channel = -1, const Sound* sound = nullptr, u32 startOffsetSample = 0, bool updateSelection = true, const WaveFileScale& scale = {});
    void setWaveFileScale(const WaveFileScale& scale);

    bool beginSeqExport(const Sound* sound, const SeqExportSettings& settings);
    bool stepSeqExport(f64 budgetSec);
    void endSeqExport(Pcm16Wav* out);
    f32 getSeqExportProgress() const;

    bool playBankNotePoly(u8 key, u8 velocity, const BankFile::VelocityRegion& velocityRegion);
    void stopBankNote(u8 key);
    void stopAllBankNotes(bool immediate);
    void setBankPitchBend(f32 normalized);
    void setBankModulation(f32 amount01);
    void setBankExpression(f32 amount01);
    void setBankSustain(bool on);

    void pause(bool isPause);
    bool seek(f32 progress);
    void reset();

    f32 getVolume() const { return mVolume; }
    void setVolume(f32 volume);

    f32 getPitch() const { return mPitch; }
    void setPitch(f32 pitch);

    f32 getPan() const { return mPan; }
    void setPan(f32 pan);

    f32 getLpfFreq() const { return mLPF; }
    void setLpfFreq(f32 lpfFreq);

    s32 getBiquadType() const { return mBiquadType; }
    f32 getBiquadValue() const { return mBiquadValue; }
    void setBiquadFilter(s32 type, f32 value);

    f32 getSeqTempoRatio() const { return mSeqTempoRatio; }
    void setSeqTempoRatio(f32 tempoRatio);

    f32 getTrackVolume(u32 trackNo) const { return mTrackVolume[trackNo]; }
    void setTrackVolume(u32 trackNo, f32 volume);

    u32 getMuteTracks() const { return mMuteTracks; }
    u32 getSoloTracks() const { return mSoloTracks; }
    void setTrackSwitches(u32 muteTracks, u32 soloTracks);
    bool isTrackAudible(u32 trackNo) const { return IsTrackAudible(mMuteTracks, mSoloTracks, trackNo); }

    bool isLoopEnabled() const { return mLoopEnabled; }
    void setLoopEnabled(bool enable);

    bool playStreamRegion(const Sound* sound, s32 regionNo);
    void setNextStreamRegion(s32 regionNo);
    s32 getPlayingStreamRegion() const;
    s32 getNextStreamRegion() const;
    void renumberStreamRegions(const s32* newNos, s32 count);

    void updateStreamTrackMute(const Sound* sound);

    bool hasSeqVarOverride(SeqVarScope scope, u32 trackNo, u32 varNo) const { return getSeqVarInfo_(scope, trackNo, varNo).enable; }
    s16 getSeqVar(SeqVarScope scope, u32 trackNo, u32 varNo) const;
    void setSeqVar(SeqVarScope scope, u32 trackNo, u32 varNo, s16 value);
    void resetSeqVar(SeqVarScope scope, u32 trackNo, u32 varNo);
    SeqVarUse getSeqVarUse(const SeqTimeline& timeline) const;

    bool hasParameterOverride() const;
    bool hasTrackOverride() const;
    bool hasSeqVarOverride() const;

    void resetParameters();
    void resetTracks();
    void resetSeqVars();

    const Sound* getPlayingSound() const { return mPlayingSound; }
    void resetPlayingSound() { mPlayingSound = nullptr; }
    const Sound* getLastPlayedSound() const { return mLastPlayedSound; }
    void resetLastPlayedSound() { mLastPlayedSound = nullptr; }
    const WaveFile* getPlayingWaveFile() const { return mPlayingWaveFile; }
    void resetPlayingWaveFile() { mPlayingWaveFile = nullptr; }

    void invalidateBankFile(const BankFile& bankFile);
    void forgetBankRegion(const BankFile::VelocityRegion& velocityRegion);

    SequenceSoundPlayer& getSequencePlayer() { return mSequencePlayer; }
    StreamSoundPlayer& getStreamPlayer() { return *mStreamPlayer; }
    WaveSoundPlayer& getWavePlayer() { return mWavePlayer; }

    u32 getSampleRate() const { return mSampleRate; }
    u32 getSampleCount() const { return mSampleCount; }
    s32 getPlaySamplePosition(bool isOriginalSamplePosition) const;

    u32 getSeqCurrentTick() const;
    const SeqTimeline& getPlayingSeqTimeline() const { return mSeqTimeline; }
    const SeqTimeline& getSeqTimeline(const Sound& sound);

    bool isCurrentPlayer() const { return mCurrentPlayer != nullptr; }
    bool isCurrentPlayerSequence() const { return mCurrentPlayer == &mSequencePlayer; }
    bool isCurrentPlayerStream() const { return mCurrentPlayer == mStreamPlayer; }
    bool isCurrentPlayerWave() const { return mCurrentPlayer == &mWavePlayer; }

    bool isPause() const
    {
        bool isPause = true;
        if (isActive())
        {
            isPause = mCurrentPlayer->isPause();
        }

        return isPause;
    }

    bool isActive() const
    {
        return isCurrentPlayer() && mCurrentPlayer->isActive();
    }

public:
    static const u32 cMaxBankVoices = 16;
    static constexpr f32 cBankPitchBendRange = 2.0f;

private:
    bool playSeqSound_(const Sound* sound);
    bool playStrmSound_(const Sound* sound);
    bool playWaveSound_(const Sound* sound, u32 startOffsetSample);

    void fadeOutPlayers_();
    void stopAllPlayers_();

    StreamSoundPlayer& getIdleStreamPlayer_();
    void switchStreamPlayer_();
    void freeIdleStreamPlayer_();

    void initPlayerParam_();
    void initPlayerTrack_();
    void initPlayerLoop_();
    f32 calcTrackVolume_(u32 trackNo) const;
    void readStreamTrackMute_(const Sound* sound);
    void initSeqVars_();
    const SeqVarInfo& getSeqVarInfo_(SeqVarScope scope, u32 trackNo, u32 varNo) const;
    volatile s16* getSeqVarPtr_(SeqVarScope scope, u32 trackNo, u32 varNo);

    void refreshPlayingSound_();
    void refreshBankRegions_();

    const SeqTimeline& findSeqTimeline_(const SequenceFile& seqFile, const sead::SafeString& startLabel, const BankFile* const* banks, bool isPlay);

    void applyBankExpression_(snd::internal::driver::Channel* channel);
    s32 allocBankVoice_();

    static const u32 cSeqTimelineCacheNum = 4;

    struct SeqTimelineEntry
    {
        SeqScanSetup setup = {};
        SeqTimeline timeline;
        u64 editGeneration = 0;
        std::chrono::steady_clock::time_point nextScanTime;
        bool isUsed = false;
    };

    struct SeqCapture : public snd::FinalMixCallback
    {
        SeqCapture()
            : mActive(false)
        {
        }

        void prepare()
        {
            mActive = true;
            mSamplesLeft.clear();
            mSamplesRight.clear();
        }

        void finish()
        {
            mActive = false;
        }

        std::vector<f32> mSamplesLeft;
        std::vector<f32> mSamplesRight;

        virtual void onFinalMix(const snd::FinalMixData* data) override
        {
            if (!mActive)
                return;

            for (u32 i = 0; i < data->sampleCount; i++)
            {
                mSamplesLeft.push_back(data->left[i]);
                mSamplesRight.push_back(data->right[i]);
            }
        }

    private:
        bool mActive;
    };

private:
    f32 mVolume;
    f32 mPitch;
    f32 mPan;
    f32 mLPF;
    s32 mBiquadType;
    f32 mBiquadValue;
    f32 mTrackVolume[cMaxTracks];
    u32 mMuteTracks = 0;
    u32 mSoloTracks = 0;
    f32 mSeqTempoRatio;
    bool mLoopEnabled = true;

    SeqVarInfo mGlobalVars[SequenceSoundPlayer::cGlobalVariableNum];
    SeqVarInfo mPlayerVars[SequenceSoundPlayer::cPlayerVariableNum];
    SeqVarInfo mTrackVars[SequenceSoundPlayer::cTrackNumPerPlayer][SequenceTrack::cTrackVariableNum];

    const Sound* mPlayingSound;
    const Sound* mLastPlayedSound;
    const WaveFile* mPlayingWaveFile;
    const Sound::StreamSoundInfo::Track* mPlayingStreamTracks[cStrmTrackNum] = {};
    u32 mSilentTracks = 0;
    BasicSoundPlayer* mCurrentPlayer;
    u32 mSampleRate;
    u32 mSampleCount;

    SequenceSoundPlayer mSequencePlayer;
    StreamSoundPlayer mStreamPlayers[2];
    StreamSoundPlayer* mStreamPlayer = &mStreamPlayers[0];
    WaveSoundPlayer mWavePlayer;
    
    SequenceSoundPlayer mSeqScanPlayer;
    SeqTimeline mSeqTimeline;
    SeqTimelineEntry mSeqTimelineCache[cSeqTimelineCacheNum];
    u32 mSeqTimelineCacheNext = 0;

    WaveSoundPlayer mBankVoices[cMaxBankVoices];
    s32 mBankVoiceKey[cMaxBankVoices];
    u32 mBankVoiceAge[cMaxBankVoices];
    u32 mBankVoiceCounter = 0;
    f32 mBankPitchBendSemis = 0.0f;
    f32 mBankModulation = 0.0f;
    f32 mBankExpression = 1.0f;
    bool mBankSustainOn = false;
    bool mBankVoiceHeld[cMaxBankVoices] = {};

    SequenceNoteOnCallback2 mSequenceNoteOnCallback2;

    struct SeqExportState
    {
        bool isActive = false;
        bool isTail = false;
        SeqExportSettings settings;
        f64 playSec = 0.0;
        f64 fadeSec = 0.0;
        f64 loopStartSec = 0.0;
        f64 loopEndSec = 0.0;
        u32 frameCount = 0;
        u32 frameMax = 0;
        u32 playFrameNum = 0;
        u32 tailFrameCount = 0;
        u32 quietFrameCount = 0;
        u32 muteTracks = 0;
        u32 soloTracks = 0;
    };

    void restoreSeqExportTracks_();

    SeqCapture mSeqCapture;
    bool mSeqCaptureRegistered = false;
    SeqExportState mSeqExport;
};
