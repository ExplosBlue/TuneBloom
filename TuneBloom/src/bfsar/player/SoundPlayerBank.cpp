#include <bfsar/player/SoundPlayer.h>

#include <ui/Messages.h>
#include <ui/app/PopupMgr.h>

#include <snd/BankR.h>
#include <snd/ChannelMgr.h>

#include <math/seadMathCalcCommon.h>

static void NoteOffBankVoice(WaveSoundPlayer& voice)
{
    snd::internal::driver::Channel* channel = voice.getChannel();

    if (channel)
        channel->noteOff();
}

s32 SoundPlayer::allocBankVoice_()
{
    for (u32 i = 0; i < cMaxBankVoices; i++)
    {
        if (!mBankVoices[i].isActive())
            return static_cast<s32>(i);
    }

    s32 oldest = 0;
    for (u32 i = 1; i < cMaxBankVoices; i++)
    {
        if (mBankVoiceAge[i] < mBankVoiceAge[oldest])
            oldest = static_cast<s32>(i);
    }

    return oldest;
}

void SoundPlayer::applyBankExpression_(snd::internal::driver::Channel* channel)
{
    if (!channel)
        return;

    channel->setUserPitch(mBankPitchBendSemis);

    snd::internal::CurveLfoParam lfo;
    lfo.curve = static_cast<u8>(snd::internal::CurveLfoParam::CurveType::Sine);
    lfo.speed = 5.0f;
    lfo.depth = mBankModulation * 0.5f;
    lfo.range = 1;
    lfo.delay = 0;
    lfo.phase = 0;
    channel->setLfoParam(lfo, 0);
    channel->setLfoTarget(snd::internal::driver::Channel::LfoTarget::Pitch, 0);
}

bool SoundPlayer::playBankNotePoly(u8 key, u8 velocity, const BankFile::VelocityRegion& velocityRegion)
{
    const WaveFile* waveFile = static_cast<const WaveFile*>(velocityRegion.getWaveFileRef().getItem());
    if (!waveFile)
        return false;

    if (waveFile->getChannels().isEmpty())
    {
        PopupMgr::instance()->addPopup({messages::playback::cWaveFileNoChannels, nullptr});
        return false;
    }

    const_cast<WaveFile*>(waveFile)->updateLoop();

    snd::internal::driver::SoundThreadLock lock;

    const s32 slot = allocBankVoice_();
    WaveSoundPlayer& voice = mBankVoices[slot];

    voice.deinit(true);
    voice.init();
    voice.prepare(*waveFile);
    voice.setBankNoteInfo(key, velocity, velocityRegion);

    applyBankExpression_(voice.getChannel());

    mBankVoiceKey[slot] = static_cast<s32>(key);
    mBankVoiceHeld[slot] = false;
    mBankVoiceAge[slot] = ++mBankVoiceCounter;

    return true;
}

void SoundPlayer::stopBankNote(u8 key)
{
    snd::internal::driver::SoundThreadLock lock;

    for (u32 i = 0; i < cMaxBankVoices; i++)
    {
        if (mBankVoiceKey[i] != static_cast<s32>(key))
            continue;

        if (mBankSustainOn)
        {
            mBankVoiceHeld[i] = true;
            continue;
        }

        NoteOffBankVoice(mBankVoices[i]);
        mBankVoiceKey[i] = -1;
    }
}

void SoundPlayer::stopAllBankNotes(bool immediate)
{
    snd::internal::driver::SoundThreadLock lock;

    for (u32 i = 0; i < cMaxBankVoices; i++)
    {
        if (immediate)
            mBankVoices[i].deinit(true);
        else
            NoteOffBankVoice(mBankVoices[i]);

        mBankVoiceKey[i] = -1;
    }
}

void SoundPlayer::setBankPitchBend(f32 normalized)
{
    mBankPitchBendSemis = sead::Mathf::clamp2(-1.0f, normalized, 1.0f) * cBankPitchBendRange;

    snd::internal::driver::SoundThreadLock lock;
    for (u32 i = 0; i < cMaxBankVoices; i++)
    {
        if (!mBankVoices[i].isActive())
            continue;

        snd::internal::driver::Channel* channel = mBankVoices[i].getChannel();

        if (channel)
            channel->setUserPitch(mBankPitchBendSemis);
    }
}

void SoundPlayer::setBankModulation(f32 amount01)
{
    mBankModulation = sead::Mathf::clamp2(0.0f, amount01, 1.0f);

    snd::internal::driver::SoundThreadLock lock;
    for (u32 i = 0; i < cMaxBankVoices; i++)
    {
        if (mBankVoices[i].isActive())
            applyBankExpression_(mBankVoices[i].getChannel());
    }
}

void SoundPlayer::setBankExpression(f32 amount01)
{
    mBankExpression = sead::Mathf::clamp2(0.0f, amount01, 1.0f);

    snd::internal::driver::SoundThreadLock lock;
    for (u32 i = 0; i < cMaxBankVoices; i++)
    {
        if (mBankVoices[i].isActive())
            applyBankExpression_(mBankVoices[i].getChannel());
    }
}

void SoundPlayer::setBankSustain(bool on)
{
    mBankSustainOn = on;
    if (on)
        return;

    snd::internal::driver::SoundThreadLock lock;
    for (u32 i = 0; i < cMaxBankVoices; i++)
    {
        if (!mBankVoiceHeld[i])
            continue;

        NoteOffBankVoice(mBankVoices[i]);

        mBankVoiceHeld[i] = false;
        mBankVoiceKey[i] = -1;
    }
}

void SoundPlayer::refreshBankRegions_()
{
    snd::internal::driver::SoundThreadLock lock;
    BankR::refreshVelocityRegions();
}

void SoundPlayer::forgetBankRegion(const BankFile::VelocityRegion& velocityRegion)
{
    if (!snd::internal::driver::SoundThread::instance() || !snd::internal::driver::ChannelMgr::instance())
        return;

    snd::internal::driver::SoundThreadLock lock;
    BankR::forgetVelocityRegion(velocityRegion);
}

void SoundPlayer::invalidateBankFile(const BankFile& bankFile)
{
    stopAllBankNotes(true);

    snd::internal::driver::SoundThreadLock lock;
    mSequencePlayer.invalidateBankFile(bankFile);
}
