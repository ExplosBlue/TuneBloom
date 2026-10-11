#include <bfsar/player/SoundPlayer.h>

void SoundPlayer::setVolume(f32 volume)
{
    mVolume = volume;

    if (isCurrentPlayer())
    {
        snd::internal::driver::SoundThreadLock lock;
        mCurrentPlayer->setVolume(volume);
    }
}

void SoundPlayer::setPitch(f32 pitch)
{
    mPitch = pitch;

    if (!isCurrentPlayer())
        return;

    snd::internal::driver::SoundThreadLock lock;
    mCurrentPlayer->setPitch(pitch);
}

void SoundPlayer::setWaveFileScale(const WaveFileScale& scale)
{
    if (!isCurrentPlayerWave() || mPlayingSound)
        return;

    snd::internal::driver::SoundThreadLock lock;
    mWavePlayer.setFileScale(scale.volume, scale.pitch);
}

void SoundPlayer::setPan(f32 pan)
{
    mPan = pan;

    if (!isCurrentPlayer())
        return;

    snd::internal::driver::SoundThreadLock lock;
    mCurrentPlayer->setPan(pan);
}

void SoundPlayer::setLpfFreq(f32 lpfFreq)
{
    mLPF = lpfFreq;

    if (!isCurrentPlayer())
        return;

    snd::internal::driver::SoundThreadLock lock;
    mCurrentPlayer->setLpfFreq(lpfFreq);
}

void SoundPlayer::setBiquadFilter(s32 type, f32 value)
{
    mBiquadType = type;
    mBiquadValue = value;

    if (!isCurrentPlayer())
        return;

    snd::internal::driver::SoundThreadLock lock;
    mCurrentPlayer->setBiquadFilter(type, value);
}

void SoundPlayer::setSeqTempoRatio(f32 tempoRatio)
{
    mSeqTempoRatio = tempoRatio;
    mSequencePlayer.setTempoRatio(tempoRatio);
}

void SoundPlayer::setLoopEnabled(bool enable)
{
    mLoopEnabled = enable;

    if (!isActive())
        return;

    snd::internal::driver::SoundThreadLock lock;
    initPlayerLoop_();
}

void SoundPlayer::setTrackVolume(u32 trackNo, f32 volume)
{
    SEAD_ASSERT(trackNo < cMaxTracks);

    mTrackVolume[trackNo] = volume;

    snd::internal::driver::SoundThreadLock lock;
    initPlayerTrack_();
}

void SoundPlayer::setTrackSwitches(u32 muteTracks, u32 soloTracks)
{
    mMuteTracks = muteTracks;
    mSoloTracks = soloTracks;

    snd::internal::driver::SoundThreadLock lock;
    initPlayerTrack_();
}

bool SoundPlayer::hasParameterOverride() const
{
    return mVolume != cDefaultVolume
        || mPitch != cDefaultPitch
        || mPan != cDefaultPan
        || mLPF != cDefaultLpfFreq
        || mBiquadType != cDefaultBiquadType
        || mBiquadValue != cDefaultBiquadValue
        || mSeqTempoRatio != cDefaultSeqTempoRatio;
}

bool SoundPlayer::hasTrackOverride() const
{
    if (mMuteTracks != 0 || mSoloTracks != 0)
        return true;

    for (u32 i = 0; i < cMaxTracks; i++)
    {
        if (mTrackVolume[i] != cDefaultTrackVolume)
            return true;
    }

    return false;
}

bool SoundPlayer::hasSeqVarOverride() const
{
    for (const SeqVarInfo& varInfo : mGlobalVars)
    {
        if (varInfo.enable)
            return true;
    }

    for (const SeqVarInfo& varInfo : mPlayerVars)
    {
        if (varInfo.enable)
            return true;
    }

    for (const auto& trackVars : mTrackVars)
    {
        for (const SeqVarInfo& varInfo : trackVars)
        {
            if (varInfo.enable)
                return true;
        }
    }

    return false;
}

void SoundPlayer::resetParameters()
{
    mVolume = cDefaultVolume;
    mPitch = cDefaultPitch;
    mPan = cDefaultPan;
    mLPF = cDefaultLpfFreq;
    mBiquadType = cDefaultBiquadType;
    mBiquadValue = cDefaultBiquadValue;
    mSeqTempoRatio = cDefaultSeqTempoRatio;

    snd::internal::driver::SoundThreadLock lock;
    initPlayerParam_();
}

void SoundPlayer::resetTracks()
{
    for (u32 i = 0; i < cMaxTracks; i++)
        mTrackVolume[i] = cDefaultTrackVolume;

    mMuteTracks = 0;
    mSoloTracks = 0;

    snd::internal::driver::SoundThreadLock lock;
    initPlayerTrack_();
}

const SoundPlayer::SeqVarInfo& SoundPlayer::getSeqVarInfo_(SeqVarScope scope, u32 trackNo, u32 varNo) const
{
    switch (scope)
    {
    case SeqVarScope::Global:
        return mGlobalVars[varNo];
    case SeqVarScope::Player:
        return mPlayerVars[varNo];
    default:
        return mTrackVars[trackNo][varNo];
    }
}

volatile s16* SoundPlayer::getSeqVarPtr_(SeqVarScope scope, u32 trackNo, u32 varNo)
{
    switch (scope)
    {
    case SeqVarScope::Global:
        return &SequenceSoundPlayer::sGlobalVariable[varNo];
    case SeqVarScope::Player:
        return mSequencePlayer.getVariablePtr(varNo);
    default:
        return mSequencePlayer.getTrack_(trackNo).getVariablePtr(varNo);
    }
}

s16 SoundPlayer::getSeqVar(SeqVarScope scope, u32 trackNo, u32 varNo) const
{
    if (scope == SeqVarScope::Global)
        return SequenceSoundPlayer::sGlobalVariable[varNo];

    if (!isCurrentPlayerSequence() || !mSequencePlayer.isActive())
    {
        const SeqVarInfo& varInfo = getSeqVarInfo_(scope, trackNo, varNo);

        return varInfo.enable ? varInfo.value : static_cast<s16>(SequenceSoundPlayer::cVariableDefaultValue);
    }

    if (scope == SeqVarScope::Player)
        return mSequencePlayer.getLocalVariable(varNo);

    return mSequencePlayer.getTrack_(trackNo).getTrackVariable(varNo);
}

void SoundPlayer::setSeqVar(SeqVarScope scope, u32 trackNo, u32 varNo, s16 value)
{
    SeqVarInfo& varInfo = const_cast<SeqVarInfo&>(getSeqVarInfo_(scope, trackNo, varNo));
    varInfo.enable = true;
    varInfo.value = value;

    *getSeqVarPtr_(scope, trackNo, varNo) = value;
}

void SoundPlayer::resetSeqVar(SeqVarScope scope, u32 trackNo, u32 varNo)
{
    SeqVarInfo& varInfo = const_cast<SeqVarInfo&>(getSeqVarInfo_(scope, trackNo, varNo));

    if (!varInfo.enable)
        return;

    varInfo = SeqVarInfo();
    *getSeqVarPtr_(scope, trackNo, varNo) = SequenceSoundPlayer::cVariableDefaultValue;
}

void SoundPlayer::resetSeqVars()
{
    for (u32 i = 0; i < SequenceSoundPlayer::cGlobalVariableNum; i++)
        resetSeqVar(SeqVarScope::Global, 0, i);

    for (u32 i = 0; i < SequenceSoundPlayer::cPlayerVariableNum; i++)
        resetSeqVar(SeqVarScope::Player, 0, i);

    for (u32 trackNo = 0; trackNo < SequenceSoundPlayer::cTrackNumPerPlayer; trackNo++)
    {
        for (u32 i = 0; i < SequenceTrack::cTrackVariableNum; i++)
            resetSeqVar(SeqVarScope::Track, trackNo, i);
    }
}

SeqVarUse SoundPlayer::getSeqVarUse(const SeqTimeline& timeline) const
{
    SeqVarUse use = timeline.varUse;

    if (&timeline == &mSeqTimeline && isCurrentPlayerSequence())
        use.add(mSequencePlayer);

    return use;
}

void SoundPlayer::initPlayerParam_()
{
    if (isCurrentPlayer())
    {
        mCurrentPlayer->setVolume(mVolume);
        mCurrentPlayer->setPitch(mPitch);
        mCurrentPlayer->setPan(mPan);
        mCurrentPlayer->setLpfFreq(mLPF);
        mCurrentPlayer->setBiquadFilter(mBiquadType, mBiquadValue);
    }

    if (isCurrentPlayerSequence())
    {
        mSequencePlayer.setTempoRatio(mSeqTempoRatio);
    }
}

void SoundPlayer::initPlayerLoop_()
{
    if (isCurrentPlayerWave())
    {
        mWavePlayer.setLoopEnabled(mLoopEnabled);
    }
    else if (isCurrentPlayerStream())
    {
        mStreamPlayer->setLoopEnabled(mLoopEnabled);
    }
    else if (isCurrentPlayerSequence())
    {
        const bool isStop = !mLoopEnabled && mSeqTimeline.kind == SeqTimeline::Kind::Loop;
        mSequencePlayer.setLoopStop(isStop ? mSeqTimeline.loopStartTick : 0, isStop ? mSeqTimeline.endTick : 0);
    }
}

void SoundPlayer::initPlayerTrack_()
{
    if (isCurrentPlayerStream())
    {
        for (u32 i = 0; i < cStrmTrackNum; i++)
        {
            if (i < mStreamPlayer->getTrackCount())
            {
                mStreamPlayer->setTrackVolume(1 << i, calcTrackVolume_(i));
            }
        }
    }
    else if (isCurrentPlayerSequence())
    {
        for (u32 i = 0; i < SequenceSoundPlayer::cTrackNumPerPlayer; i++)
        {
            SequenceTrack* track = mSequencePlayer.getPlayerTrack(i);
            if (track)
            {
                track->setVolume(calcTrackVolume_(i));
            }
        }
    }
}

void SoundPlayer::initSeqVars_()
{
    if (!isCurrentPlayerSequence())
    {
        return;
    }

    for (s32 i = 0; i < SequenceSoundPlayer::cGlobalVariableNum; i++)
    {
        const SeqVarInfo& varInfo = mGlobalVars[i];
        if (varInfo.enable)
        {
            mSequencePlayer.setGlobalVariable(i, varInfo.value);
        }
    }

    for (s32 i = 0; i < SequenceSoundPlayer::cPlayerVariableNum; i++)
    {
        const SeqVarInfo& varInfo = mPlayerVars[i];
        if (varInfo.enable)
        {
            mSequencePlayer.setLocalVariable(i, varInfo.value);
        }
    }

    for (s32 trackNo = 0; trackNo < SequenceSoundPlayer::cTrackNumPerPlayer; trackNo++)
    {
        for (s32 i = 0; i < SequenceTrack::cTrackVariableNum; i++)
        {
            const SeqVarInfo& varInfo = mTrackVars[trackNo][i];
            if (varInfo.enable)
            {
                mSequencePlayer.getTrack_(trackNo).setTrackVariable(i, varInfo.value);
            }
        }
    }
}
