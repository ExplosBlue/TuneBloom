#pragma once

#include "HardwareMgr.h"
#include "MultiVoice.h"
#include "WaveFileReader.h"
#include "DisposeCallback.h"
#include "CurveAdshr.h"
#include "CurveLfo.h"
#include "MoveValue.h"

#include <math/seadMathCalcCommon.h>

class Item;

namespace snd { namespace internal { namespace driver {

class Channel
{
public:
    enum class LfoTarget
    {
        Pitch,
        Volume,
        Pan
    };

    enum class ChannelCallbackStatus
    {
        Stopped,
        Drop,
        Finish,
        Cancel
    };

    enum class EnvelopeParam
    {
        Attack,
        Hold,
        Decay,
        Sustain,
        Release,

        Num
    };

    typedef void (*ChannelCallback)(Channel* channel, ChannelCallbackStatus status, void* userData);

    static const s32 cChannelNum = HardwareMgr::cMaxVoiceCount + 1; // Number of channels.
    static const s32 cChannelMin = 0;
    static const s32 cChannelMax = cChannelMin + cChannelNum - 1;

    static const u32 cPriorityRelease = 1;
    static const s32 cFadeOutRelease = 126;

    static const s32 cWaveBufferMax = 2;

    static const s32 cModCount = 4;

private:
    static const s32 cKeyInit = 60;
    static const s32 cRootKeyInit = 60;
    static const u8 cSilenceVolumeMax = 255;
    static const u8 cSilenceVolumeMin = 0;

    // Inverse of cSilenceVolumeMax. Trying to speed up process making division into multiplication.
    static constexpr f32 cSilenceVolumeMaxR = 1.0f / static_cast<f32>(cSilenceVolumeMax);

public:
    static Channel* allocChannel(s32 voiceChannelCount, u32 priority, ChannelCallback callback, void* callbackData);
    static void freeChannel(Channel* channel);
    static void detachChannel(Channel* channel);

private:
    static void VoiceCallbackFunc(MultiVoice* voice, MultiVoice::VoiceCallbackStatus status, void* arg);

public:
    Channel();
    ~Channel();

    void update(bool doPeriodicProc);
    void callChannelCallback(ChannelCallbackStatus status);

    void setOffsetContext(const WaveFileReader::OffsetContextInfo& contextInfo)
    {
        mContextInfo = contextInfo;
        mUseContextInfo = true;
    }

    void start(const WaveInfo& waveParam, s32 length, u32 startOffsetSamples);

    // Custom
    struct Region
    {
        s32 no = -1;
        u32 start = 0;
        u32 end = 0;
        bool loop = false;
        AdpcmContext context = {};
    };

    void startRegion(const WaveInfo& waveInfo, const Region& region);
    void setNextRegion(const WaveInfo& waveInfo, const Region& region);
    void clearNextRegion(bool loopPlayingRegion);
    void setRegionLoopEnabled(bool enable);
    s32 getPlayingRegionNo() const;
    s32 getNextRegionNo() const;
    void renumberRegions(const s32* newNos, s32 count);

    void stop();
    void pause(bool flag);

    void noteOff();
    void release();
    void fadeOut();

    bool isActive() const
    {
        return mActiveFlag != 0;
    }

    bool isPause() const
    {
        return mPauseFlag != 0;
    }

    void setKey(u8 key)
    {
        mKey = key;
    }

    void setKey(u8 key, u8 rootKey)
    {
        mKey = key;
        mRootKey = rootKey;
    }

    void setRootKey(u8 rootKey)
    {
        mRootKey = rootKey;
    }

    void setBankRegion(const Item* region, s32 notePan)
    {
        mBankRegion = region;
        mBankNotePan = notePan;
        mBankEnvelopeUnlinkMask = 0;
    }

    void setEnvelopeParam(EnvelopeParam param, u8 value)
    {
        switch (param)
        {
            case EnvelopeParam::Attack:
                setAttack(value);
                break;

            case EnvelopeParam::Hold:
                setHold(value);
                break;

            case EnvelopeParam::Decay:
                setDecay(value);
                break;

            case EnvelopeParam::Sustain:
                setSustain(value);
                break;

            case EnvelopeParam::Release:
                setRelease(value);
                break;

            default:
                break;
        }
    }

    u8 getBankEnvelope(EnvelopeParam param) const
    {
        return mBankEnvelope[static_cast<s32>(param)];
    }

    void setBankEnvelope(EnvelopeParam param, u8 value)
    {
        mBankEnvelope[static_cast<s32>(param)] = value;
    }

    bool isBankEnvelopeLinked(EnvelopeParam param) const
    {
        return (mBankEnvelopeUnlinkMask & (1 << static_cast<s32>(param))) == 0;
    }

    void unlinkBankEnvelope(EnvelopeParam param)
    {
        mBankEnvelopeUnlinkMask |= 1 << static_cast<s32>(param);
    }

    void resetBankRegion()
    {
        mBankRegion = nullptr;
    }

    const Item* getBankRegion() const
    {
        return mBankRegion;
    }

    s32 getBankNotePan() const
    {
        return mBankNotePan;
    }

    void setInitPan(f32 pan)
    {
        mInitPan = pan;
    }

    //void setInitSurroundPan(f32 surroundPan)
    //{
    //    mInitSurroundPan = surroundPan;
    //}

    void setTune(f32 tune)
    {
        mTune = tune;
    }

    void setAttack(s32 attack)
    {
        mCurveAdshr.setAttack(attack);
    }

    void setHold(s32 hold)
    {
        mCurveAdshr.setHold(hold);
    }

    void setDecay(s32 decay)
    {
        mCurveAdshr.setDecay(decay);
    }

    void setSustain(s32 sustain)
    {
        mCurveAdshr.setSustain(sustain);
    }

    void setRelease(s32 release)
    {
        mCurveAdshr.setRelease(release);
    }

    void setSilence(bool silenceFlag, s32 fadeTimes)
    {
        SEAD_ASSERT(fadeTimes >= 0 && fadeTimes <= sead::MathCalcCommon<u16>::maxNumber());

        mSilenceVolume.setTarget(silenceFlag ? cSilenceVolumeMin : cSilenceVolumeMax, static_cast<u16>(fadeTimes));
    }

    s32 getLength() const
    {
        return mLength;
    }

    void setLength(s32 length)
    {
        mLength = length;
    }

    bool isRelease() const
    {
        return mCurveAdshr.getStatus() == CurveAdshr::Status::Release;
    }

    void setUserVolume(f32 volume)
    {
        mUserVolume = volume;
    }

    void setUserPitch(f32 pitch)
    {
        mUserPitch = pitch;
    }

    void setUserPitchRatio(f32 pitchRatio)
    {
        mUserPitchRatio = pitchRatio;
    }

    void setUserLpfFreq(f32 lpfFreq)
    {
        mUserLpfFreq = lpfFreq;
    }

    void setBiquadFilter(s32 type, f32 value)
    {
        if (type > (s32)BiquadFilterType::Max)
            type = 0;

        // To reach here, the INHERIT process should be done, so BiquadFilterType::DataMin should be minimized.
        type = sead::Mathi::clamp2((s32)BiquadFilterType::DataMin, type, (s32)BiquadFilterType::Max);

        mBiquadType = static_cast<u8>(type);
        mBiquadValue = value;
    }

    void setLfoParam(const CurveLfoParam& param, u32 i = 0)
    {
        mLfo[i].setParam(param);
    }

    void setLfoTarget(LfoTarget type, u32 i = 0)
    {
        mLfoTarget[i] = static_cast<u8>(type);
    }

    void setPriority(u32 priority);

    void setReleasePriorityFix(bool fix)
    {
        mReleasePriorityFixFlag = fix;
    }

    void setIsIgnoreNoteOff(bool flag)
    {
        mIsIgnoreNoteOff = flag;
    }

    f32 fetchVoicePeak()
    {
        return mVoice ? mVoice->fetchPeak() : 0.0f;
    }

    //void setFrontBypass(bool flag)
    //{
    //    mVoice->setFrontBypass(flag);
    //}

    void setSweepParam(f32 sweepPitch, s32 sweepTime, bool autoUpdate)
    {
        mSweepPitch = sweepPitch;
        mSweepLength = sweepTime;
        mAutoSweep = autoUpdate;
        mSweepCounter = 0;
    }

    bool isAutoUpdateSweep() const
    {
        return mAutoSweep != 0;
    }

    void updateSweep(s32 count);

    void setPanMode(PanMode panMode)
    {
        mPanMode = panMode;
     }

    void setPanCurve(PanCurve panCurve)
    {
        mPanCurve = panCurve;
    }

    void setOutputLine(u32 lineFlag)
    {
        mOutputLineFlag = lineFlag;
    }

    u32 getOutputLine() const
    {
        return mOutputLineFlag;
    }

    void setParam(const OutputParam& param)
    {
        mParam = param;
    }

    const OutputParam& getParam() const
    {
        return mParam;
    }

    Channel* getNextTrackChannel() const
    {
        return mNextLink;
    }

    void setNextTrackChannel(Channel* channel)
    {
        mNextLink = channel;
    }

    u32 getCurrentPlayingSample(bool isOriginalSamplePosition) const;
    void setLoopEnabled(bool enable);

    void setKeyGroupId(u8 id)
    {
        mKeyGroupId = id;
    }

    u8 getKeyGroupId() const
    {
        return mKeyGroupId;
    }

    void setInterpolationType(u8 type)
    {
        mInterpolationType = type;

        if (mVoice)
            mVoice->setInterpolationType(type);
    }

    u8 getInterpolationType() const
    {
        return mInterpolationType;
    }

    void setInstrumentVolume(f32 instrumentVolume)
    {
        mInstrumentVolume = instrumentVolume;
    }

    void setVelocity(f32 velocity)
    {
        mVelocity = velocity;
    }

    void setUpdateType(UpdateType updateType);
    UpdateType getUpdateType() const;

private:
    f32 getSweepValue() const;
    void initParam(ChannelCallback callback, void* callbackData);
    void appendWaveBuffer(const WaveInfo& waveInfo, u32 startOffsetSamples);

private:
    class Disposer : public DisposeCallback
    {
    public:
        Disposer()
            : mChannel(nullptr)
        {
        }

        ~Disposer() override
        {
        }

        void initialize(Channel* channel)
        {
            mChannel = channel;
        }

        void invalidateData(const void* start, const void* end) override;

    private:
        Channel* mChannel;
    };

    friend class ChannelMgr;

    Disposer mDisposer;
    CurveAdshr mCurveAdshr;

    CurveLfo mLfo[cModCount];
    u8 mLfoTarget[cModCount]; // enum LfoTarget

    u8 mPauseFlag;
    u8 mActiveFlag;
    u8 mAllocFlag;
    u8 mAutoSweep;
    u8 mReleasePriorityFixFlag;
    u8 mIsIgnoreNoteOff;
    u8 mBiquadType;
    //u8 padding;

    f32 mUserVolume;
    f32 mUserPitchRatio;
    f32 mUserLpfFreq;
    f32 mBiquadValue;
    u32 mOutputLineFlag;

    OutputParam mParam;

    f32 mUserPitch;
    f32 mSweepPitch;
    s32 mSweepCounter;
    s32 mSweepLength;

    f32 mInitPan;
    //f32 mInitSurroundPan;
    f32 mTune;
    MoveValue<u8, u16> mSilenceVolume;

    f32 mCent;
    f32 mCentPitch;

    s32 mLength;

    PanMode mPanMode;
    PanCurve mPanCurve;

    u8 mKey;
    u8 mRootKey;
    u8 mKeyGroupId;
    u8 mInterpolationType;

    f32 mInstrumentVolume;
    f32 mVelocity;

    const Item* mBankRegion;
    s32 mBankNotePan;
    u8 mBankEnvelope[static_cast<s32>(EnvelopeParam::Num)];
    u8 mBankEnvelopeUnlinkMask;

    ChannelCallback mCallback;
    void* mCallbackData;

    MultiVoice* mVoice;

    Channel* mNextLink;

    WaveBuffer mWaveBuffer[cWaveChannelMax][cWaveBufferMax];
    AdpcmContext mAdpcmContext[cWaveChannelMax];
    AdpcmContext mAdpcmLoopContext[cWaveChannelMax];
    u32 mStartOffsetSamples;
    u32 mLoopStartFrame;
    u32 mOriginalLoopStartFrame;
    WaveFileReader::OffsetContextInfo mContextInfo;
    bool mUseContextInfo;
    bool mLoopFlag;

    // Custom
    static const s32 cRegionBufferMax = 2;

    void setupStart(const WaveInfo& waveInfo, s32 length);
    void setupRegionBuffer(s32 index, const WaveInfo& waveInfo, const Region& region);
    s32 getPlayingRegionIndex() const;

    WaveBuffer mRegionBuffer[cRegionBufferMax];
    AdpcmContext mRegionContext[cRegionBufferMax];
    Region mRegion[cRegionBufferMax];
    u32 mRegionBufferStart[cRegionBufferMax];
    s32 mRegionIndex;
    bool mRegionMode;

    //u8 padding2;

    sead::ListNode mListNode;
};

} } } // namespace snd::internal::driver
