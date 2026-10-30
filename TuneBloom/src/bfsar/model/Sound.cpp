#include <bfsar/model/Sound.h>

#include <bfsar/model/StateHash.h>
#include <bfsar/player/SoundPlayer.h>
#include <messages/Messages.h>

static void MixSequenceSoundInfo(StateHash& hash, const Sound::SequenceSoundInfo& seqInfo)
{
    hash.mixRef(seqInfo.getSequenceFileRef());

    for (u32 i = 0; i < 4; i++)
        hash.mixRef(seqInfo.getBankRef(i));

    hash.mixValue(seqInfo.isEnableStartOffset());
    hash.mixText(seqInfo.getStartLabel());
    hash.mixValue(seqInfo.isEnablePriority());
    hash.mixValue(seqInfo.getChannelPriority());
    hash.mixValue(seqInfo.getIsReleasePriorityFix());
}

static void MixStreamTrack(StateHash& hash, const Sound::StreamSoundInfo::Track& track)
{
    hash.mixValue(track.isEnableName());
    hash.mixText(track.getName());
    hash.mixRef(track.getWaveFileRef());

    hash.mixValue(track.getVolume());
    hash.mixValue(track.getPan());
    hash.mixValue(track.getSPan());
    hash.mixValue(track.getFlags());
    hash.mixValue(track.getMainSend());

    for (u32 i = 0; i < 3; i++)
        hash.mixValue(track.getFxSend(i));

    hash.mixValue(track.getLpfFreq());
    hash.mixValue(track.getBiquadType());
    hash.mixValue(track.getBiquadValue());

    const sead::ObjList<u8>& channels = track.getChannels_();

    for (s32 i = 0; i < channels.size(); i++)
        hash.mixValue(*channels.nth(i));
}

static void MixStreamSoundInfo(StateHash& hash, const Sound::StreamSoundInfo& strmInfo)
{
    hash.mixText(strmInfo.getPath());
    hash.mixValue(strmInfo.getAllocateTrackFlags());
    hash.mixValue(strmInfo.getAllocateChannelCount());
    hash.mixValue(strmInfo.getPitch());
    hash.mixValue(strmInfo.getMainSend());

    for (u32 i = 0; i < 3; i++)
        hash.mixValue(strmInfo.getFxSend(i));

    hash.mixValue(strmInfo.isEnableStreamSoundExtension());
    hash.mixValue(strmInfo.getStreamType());
    hash.mixValue(strmInfo.getIsLoop());
    hash.mixValue(strmInfo.getLoopStartFrame());
    hash.mixValue(strmInfo.getLoopEndFrame());
    hash.mixValue(strmInfo.getStreamTypeInfoUpper());
    hash.mixValue(strmInfo.hasPrefetch());
    hash.mixValue(strmInfo.isOpusChannelBitrateChanged() ? strmInfo.getOpusChannelBitrate() : 0);

    const Sound::StreamSoundInfo::Track::List& tracks = strmInfo.getTrackList();

    for (s32 i = 0; i < tracks.size(); i++)
        MixStreamTrack(hash, *static_cast<const Sound::StreamSoundInfo::Track*>(tracks.nth(i)->val()));
}

static void MixWaveSoundInfo(StateHash& hash, const Sound::WaveSoundInfo& waveInfo)
{
    hash.mixRef(waveInfo.getWaveFileRef());
    hash.mixValue(waveInfo.getAllocateTrackCount());

    hash.mixValue(waveInfo.isEnablePriority());
    hash.mixValue(waveInfo.getChannelPriority());
    hash.mixValue(waveInfo.getIsReleasePriorityFix());

    hash.mixValue(waveInfo.isEnablePan());
    hash.mixValue(waveInfo.getPan());
    hash.mixValue(waveInfo.getSurroundPan());

    hash.mixValue(waveInfo.isEnablePitch());
    hash.mixValue(waveInfo.getPitch());

    hash.mixValue(waveInfo.isEnableSend());
    hash.mixValue(waveInfo.getMainSend());
    hash.mixValue(waveInfo.getFxSendCount());

    for (u32 i = 0; i < 3; i++)
        hash.mixValue(waveInfo.getFxSend(i));

    hash.mixValue(waveInfo.isEnableEnvelope());

    const snd::AdshrCurve& adshrCurve = waveInfo.getAdshrCurve();

    hash.mixValue(adshrCurve.attack);
    hash.mixValue(adshrCurve.decay);
    hash.mixValue(adshrCurve.sustain);
    hash.mixValue(adshrCurve.hold);
    hash.mixValue(adshrCurve.release);

    hash.mixValue(waveInfo.isEnableFilter());
    hash.mixValue(waveInfo.getLpfFreq());
    hash.mixValue(waveInfo.getBiquadType());
    hash.mixValue(waveInfo.getBiquadValue());

    for (const std::pair<u32, u32>& option : waveInfo.getExtraOptions())
    {
        hash.mixValue(option.first);
        hash.mixValue(option.second);
    }
}

u64 Sound::computeStateSignature() const
{
    StateHash hash;

    hash.mixName(*this);
    hash.mixRef(getPlayerRef());

    hash.mixValue(getVolume());
    hash.mixValue(getRemoteFilter());
    hash.mixValue(getSoundType());

    hash.mixValue(isEnablePanParam());
    hash.mixValue(getPanMode());
    hash.mixValue(getPanCurve());

    hash.mixValue(isEnablePlayerParam());
    hash.mixValue(getPlayerPriority());
    hash.mixValue(getActorPlayerId());

    for (u32 i = 0; i < 4; i++)
    {
        hash.mixValue(isEnableUserParam(i));
        hash.mixValue(getUserParam(i));
    }

    hash.mixValue(isEnableIsFrontBypass());
    hash.mixValue(getIsFrontBypass());

    hash.mixValue(isEnableSound3DInfo());

    const Sound3DInfo& info3D = getSound3DInfo();

    hash.mixValue(info3D.getFlags());
    hash.mixValue(info3D.getDecayRatio());
    hash.mixValue(info3D.getDecayCurve());
    hash.mixValue(info3D.getDopplerFactor());

    hash.mixBytes(mV3NameField.data(), mV3NameField.size());

    for (const std::pair<u32, u32>& option : mExtraSoundInfoOptions)
    {
        hash.mixValue(option.first);
        hash.mixValue(option.second);
    }

    MixSequenceSoundInfo(hash, getSequenceSoundInfo());
    MixStreamSoundInfo(hash, getStreamSoundInfo());
    MixWaveSoundInfo(hash, getWaveSoundInfo());

    return hash.value();
}

sead::FixedSafeString<256> Sound::StreamSoundInfo::Track::getFormattedName() const
{
    sead::FixedSafeString<256> name;
    name.appendWithFormat(messages::stream::cTrackNameFormat, getId());

    return name;
}

Sound::~Sound()
{
    if (this == sSoundPlayer.getPlayingSound())
        sSoundPlayer.resetPlayingSound();

    if (this == sSoundPlayer.getLastPlayedSound())
        sSoundPlayer.resetLastPlayedSound();
}
