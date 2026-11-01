#include <bfsar/player/SoundPlayer.h>

#include <bfsar/archive/Bfsar.h>
#include <bfsar/files/BfwsdFile.h>
#include <bfsar/files/OpusStream.h>

#include <messages/Messages.h>
#include <bfsar/model/MessageLog.h>
#include <ui/items/Selection.h>
#include <bfsar/model/ModelEvents.h>

#include <snd/ChannelMgr.h>
#include <snd/MultiVoiceMgr.h>

#include <math/seadMathCalcCommon.h>

#include <algorithm>
#include <chrono>
#include <cstring>

static constexpr std::chrono::milliseconds cSeqRescanInterval(250);
static constexpr s32 cSeqRescanCostFactor = 10;

SoundPlayer sSoundPlayer;

static const Bfsar& GetSoundArchive(const Sound& sound)
{
    const Bfsar* archive = sound.getArchive();
    SEAD_ASSERT(archive);

    return *archive;
}

static StreamSource GetSoundStreamSource(const Sound& sound)
{
    Sound& playable = const_cast<Sound&>(sound);
    Bfsar* archive = playable.getArchive();
    SEAD_ASSERT(archive);

    return archive->getStreamSource(&playable);
}

static bool IsWaveSoundFilterSupported(const Bfsar& archive)
{
    return BfwsdFile::isFilterSupportedVersion(archive.getVersionForBfwsd(), archive.getFormat());
}

void SoundPlayer::update()
{
    if (!isActive())
        resetPlayingSound();

    refreshPlayingSound_();
    refreshBankRegions_();
    freeIdleStreamPlayer_();
}

void SoundPlayer::fadeOutPlayers()
{
    snd::internal::driver::SoundThreadLock lock;
    fadeOutPlayers_();
}

void SoundPlayer::fadeOutAllVoices()
{
    {
        snd::internal::driver::SoundThreadLock lock;

        snd::internal::driver::ChannelMgr::instance()->fadeOutAllChannel();
        fadeOutPlayers_();
    }

    stopAllBankNotes(false);
}

void SoundPlayer::stopAllPlayers()
{
    snd::internal::driver::SoundThreadLock lock;
    stopAllPlayers_();
}

void SoundPlayer::stopAllVoices()
{
    snd::internal::driver::SoundThreadLock lock;
    snd::internal::driver::MultiVoiceMgr::instance()->stopAllVoices();
}

void SoundPlayer::fadeOutPlayers_()
{
    mSequencePlayer.fadeOut();
    mStreamPlayer->fadeOut();
    mWavePlayer.fadeOut();
}

void SoundPlayer::stopAllPlayers_()
{
    mSequencePlayer.deinit(true);

    for (StreamSoundPlayer& streamPlayer : mStreamPlayers)
        streamPlayer.deinit();

    mWavePlayer.deinit(true);
}

StreamSoundPlayer& SoundPlayer::getIdleStreamPlayer_()
{
    return mStreamPlayer == &mStreamPlayers[0] ? mStreamPlayers[1] : mStreamPlayers[0];
}

void SoundPlayer::switchStreamPlayer_()
{
    if (mStreamPlayer->isActive())
        mStreamPlayer = &getIdleStreamPlayer_();

    mStreamPlayer->deinit();
}

void SoundPlayer::freeIdleStreamPlayer_()
{
    StreamSoundPlayer& idlePlayer = getIdleStreamPlayer_();
    if (idlePlayer.isActive() || !idlePlayer.hasBuffers())
        return;

    snd::internal::driver::SoundThreadLock lock;

    if (!idlePlayer.isActive())
        idlePlayer.deinit();
}

static bool GetSequenceSoundPlaybackError(const Sound& sound, sead::BufferedSafeString* outError)
{
    if (!sound.getSequenceSoundInfo().getSequenceFileRef().isAttached())
    {
        outError->copy(messages::reference::cNoSequenceFile);
        return true;
    }

    return false;
}

static bool GetStreamInfoPlaybackError(const Sound::StreamSoundInfo& streamSoundInfo, ArchiveFormat format, sead::BufferedSafeString* outError, bool* outFileNotLoaded = nullptr)
{
    const Sound::StreamSoundInfo::StreamType streamType = streamSoundInfo.getStreamType();

    if (streamType != Sound::StreamSoundInfo::StreamType::NwStreamBinary && streamType != Sound::StreamSoundInfo::StreamType::Opus)
    {
        outError->format(messages::stream::cTypeUnsupportedFormat, GetInnerFileDisplayName(format, InnerFileKind::Stream));
        return true;
    }

    const Sound::StreamSoundInfo::Track::List& trackList = streamSoundInfo.getTrackList();

    if (trackList.isEmpty())
    {
        const bool fileNotLoaded = streamSoundInfo.getAllocateChannelCount() != 0;

        if (outFileNotLoaded)
            *outFileNotLoaded = fileNotLoaded;

        outError->copy(fileNotLoaded ? messages::stream::cFileNotLoaded : messages::stream::cNeedsTrack);
        return true;
    }

    if (trackList.size() > cStrmTrackNum)
    {
        outError->format(messages::stream::cTrackLimitFormat, cStrmTrackNum);
        return true;
    }

    if (streamType == Sound::StreamSoundInfo::StreamType::Opus)
        return false;

    for (u32 i = 0; i < trackList.size(); i++)
    {
        const Sound::StreamSoundInfo::Track& track = *static_cast<const Sound::StreamSoundInfo::Track*>(trackList.nth(i)->val());

        if (track.isWaveUnloaded())
        {
            if (outFileNotLoaded)
                *outFileNotLoaded = true;

            outError->copy(messages::stream::cFileNotLoaded);
            return true;
        }

        if (!track.getWaveFileRef().isAttached())
        {
            outError->format(messages::playback::cTrackNoWaveFileFormat, i);
            return true;
        }

        const WaveFile& waveFile = *static_cast<const WaveFile*>(track.getWaveFileRef().getItem());

        if (waveFile.getChannels().isEmpty())
        {
            outError->format(messages::playback::cTrackNoChannelsFormat, i);
            return true;
        }
    }

    return false;
}

static bool GetWaveSoundPlaybackError(const Sound& sound, sead::BufferedSafeString* outError)
{
    const Item* waveFileItem = sound.getWaveSoundInfo().getWaveFileRef().getItem();

    if (!waveFileItem)
    {
        outError->copy(messages::reference::cNoWaveFile);
        return true;
    }

    return SoundPlayer::GetWaveFilePlaybackError(*static_cast<const WaveFile*>(waveFileItem), outError);
}

bool SoundPlayer::GetWaveFilePlaybackError(const WaveFile& wave, sead::BufferedSafeString* outError)
{
    if (wave.getChannels().isEmpty())
    {
        outError->copy(messages::playback::cWaveFileNoChannels);
        return true;
    }

    return false;
}

bool SoundPlayer::IsStreamFileNotLoaded(const Sound& sound)
{
    if (sound.getSoundType() != Sound::SoundType::Strm)
        return false;

    sead::FixedSafeString<cPlaybackErrorLength> error;
    bool fileNotLoaded = false;

    return GetStreamInfoPlaybackError(sound.getStreamSoundInfo(), GetSoundArchive(sound).getFormat(), &error, &fileNotLoaded) && fileNotLoaded;
}

bool SoundPlayer::GetStreamPlaybackError(const StreamSource& source, sead::BufferedSafeString* outError)
{
    return GetStreamInfoPlaybackError(*source.stream, source.target.format, outError);
}

bool SoundPlayer::GetSoundPlaybackError(const Sound& sound, sead::BufferedSafeString* outError)
{
    switch (sound.getSoundType())
    {
        case Sound::SoundType::Seq:
            return GetSequenceSoundPlaybackError(sound, outError);

        case Sound::SoundType::Strm:
            return GetStreamInfoPlaybackError(sound.getStreamSoundInfo(), GetSoundArchive(sound).getFormat(), outError);

        case Sound::SoundType::Wave:
            return GetWaveSoundPlaybackError(sound, outError);

        default:
            break;
    }

    outError->copy(messages::playback::cUnsupportedSoundType);
    return true;
}

bool SoundPlayer::playSound(const Sound* sound, u32 startOffsetSample)
{
    SEAD_ASSERT(sound);

    if (mSeqExport.isActive)
        return false;

    mLastPlayedSound = sound;
    mLastPlayedStream = {};

    switch (sound->getSoundType())
    {
        case Sound::SoundType::Seq:
            return playSeqSound_(sound);

        case Sound::SoundType::Strm:
            return playStream_(GetSoundStreamSource(*sound));

        case Sound::SoundType::Wave:
            return playWaveSound_(sound, startOffsetSample);

        default:
            break;
    }

    return false;
}

bool SoundPlayer::playStream(const StreamSource& source)
{
    SEAD_ASSERT(source.isValid());

    if (source.sound)
        return playSound(source.sound);

    if (mSeqExport.isActive)
        return false;

    mLastPlayedSound = nullptr;
    mLastPlayedStream = source;

    return playStream_(source);
}

const Item* SoundPlayer::getLastPlayedItem() const
{
    if (mLastPlayedSound)
        return mLastPlayedSound;

    return mLastPlayedStream.isValid() ? mLastPlayedStream.stream->getMainWave() : nullptr;
}

void SoundPlayer::forgetStream(const Sound::StreamSoundInfo* stream)
{
    if (mPlayingStream.stream == stream)
        reset();

    if (mLastPlayedStream.stream == stream)
        mLastPlayedStream = {};

    if (mPlayingTracksStream == stream)
        forgetStreamTracks_();
}

static s32 FindStreamTrackSlot(const Sound::StreamSoundInfo::Track* const* slots, const Sound::StreamSoundInfo::Track* track)
{
    for (u32 slot = 0; slot < cStrmTrackNum; slot++)
    {
        if (slots[slot] == track)
            return static_cast<s32>(slot);
    }

    return -1;
}

bool SoundPlayer::isStreamTrackAudible(const Sound::StreamSoundInfo& stream, const Sound::StreamSoundInfo::Track* track, u32 trackNo) const
{
    if (mPlayingTracksStream != &stream)
        return isTrackAudible(trackNo);

    const s32 slot = FindStreamTrackSlot(mPlayingStreamTracks, track);

    if (slot < 0)
        return (mSoloTracks & ((1u << cStrmTrackNum) - 1)) == 0;

    return isTrackAudible(static_cast<u32>(slot));
}

void SoundPlayer::forgetStreamTrack(const Sound::StreamSoundInfo::Track* track)
{
    u32 muteTracks = mMuteTracks;
    u32 soloTracks = mSoloTracks;

    for (u32 slot = 0; slot < cStrmTrackNum; slot++)
    {
        if (mPlayingStreamTracks[slot] != track)
            continue;

        mPlayingStreamTracks[slot] = nullptr;
        muteTracks &= ~(1u << slot);
        soloTracks &= ~(1u << slot);
    }

    if (muteTracks != mMuteTracks || soloTracks != mSoloTracks)
        setTrackSwitches(muteTracks, soloTracks);
}

void SoundPlayer::forgetStreamTracks_()
{
    mPlayingTracksStream = nullptr;

    for (u32 slot = 0; slot < cStrmTrackNum; slot++)
        mPlayingStreamTracks[slot] = nullptr;
}

void SoundPlayer::remapStreamTrackSwitches_(const Sound::StreamSoundInfo& stream, const Sound::StreamSoundInfo::Track* const* tracks)
{
    if (mPlayingTracksStream != &stream)
        return;

    u32 muteTracks = 0;
    u32 soloTracks = 0;
    f32 trackVolume[cStrmTrackNum];

    for (u32 trackNo = 0; trackNo < cStrmTrackNum; trackNo++)
    {
        const s32 slot = tracks[trackNo] ? FindStreamTrackSlot(mPlayingStreamTracks, tracks[trackNo]) : static_cast<s32>(trackNo);

        trackVolume[trackNo] = slot < 0 ? cDefaultTrackVolume : mTrackVolume[slot];

        if (slot < 0)
            continue;

        muteTracks |= ((mMuteTracks >> slot) & 1u) << trackNo;
        soloTracks |= ((mSoloTracks >> slot) & 1u) << trackNo;
    }

    for (u32 trackNo = 0; trackNo < cStrmTrackNum; trackNo++)
        mTrackVolume[trackNo] = trackVolume[trackNo];

    mMuteTracks = (mMuteTracks & ~((1u << cStrmTrackNum) - 1)) | muteTracks;
    mSoloTracks = (mSoloTracks & ~((1u << cStrmTrackNum) - 1)) | soloTracks;
}

f32 SoundPlayer::calcTrackVolume_(u32 trackNo) const
{
    if (!isTrackAudible(trackNo))
        return 0.0f;

    return mTrackVolume[trackNo];
}

bool SoundPlayer::playLastSound()
{
    if (mLastPlayedStream.isValid())
        return playStream(mLastPlayedStream);

    if (mLastPlayedSound)
    {
        Item* prev = sSelectedItem;
        bool ret = playSound(mLastPlayedSound);
        sSelectedItem = prev;

        return ret;
    }

    return false;
}

bool SoundPlayer::playSeqSound_(const Sound* sound)
{
    const Sound::SequenceSoundInfo& seqSoundInfo = sound->getSequenceSoundInfo();

    sead::FixedSafeString<cPlaybackErrorLength> playbackError;

    if (GetSequenceSoundPlaybackError(*sound, &playbackError))
    {
        MessageLog::instance()->addPopup({ playbackError, nullptr });
        return false;
    }

    const Item* seqFileItem = seqSoundInfo.getSequenceFileRef().getItem();
    SEAD_ASSERT(seqFileItem->getItemType() == Item::ItemType::SequenceFile);

    const SequenceFile& seqFile = *static_cast<const SequenceFile*>(seqFileItem);

    const Bank* banks[nw::snd::SoundArchive::SEQ_BANK_MAX] = {};

    for (u32 i = 0; i < nw::snd::SoundArchive::SEQ_BANK_MAX; i++)
    {
        const Item* item = seqSoundInfo.getBankRef(i).getItem();
        if (!item)
            continue;

        SEAD_ASSERT(item->getItemType() == Item::ItemType::Bank);
        banks[i] = static_cast<const Bank*>(item);
    }

    if (!playSeqFile(seqFile, seqSoundInfo.getStartLabel(), banks, sound))
        return false;

    sSelectedItem = const_cast<Sound*>(sound);
    mPlayingSound = sound;

    return true;
}

static void SetSoundParam(BasicSoundPlayer* player, const Sound& sound)
{
    player->setInitialVolume(static_cast<f32>(sound.getVolume()) / 127.0f);
    player->setPanMode(sound.getPanMode());
    player->setPanCurve(sound.getPanCurve());
}

static void SetStreamItemParam(StreamSoundPlayer::SetupArg* setupArg, const StreamSource& source)
{
    const Sound::StreamSoundInfo& strmSoundInfo = *source.stream;

    if (source.enableSend)
    {
        setupArg->pitch = strmSoundInfo.getPitch();
        setupArg->mainSend = strmSoundInfo.getMainSend();
        for (u32 i = 0; i < nw::snd::AUX_BUS_NUM; i++)
        {
            setupArg->fxSend[i] = strmSoundInfo.getFxSend(i);
        }
    }
    else
    {
        setupArg->pitch = 1.0f;
        setupArg->mainSend = 127;
        for (u32 i = 0; i < nw::snd::AUX_BUS_NUM; i++)
        {
            setupArg->fxSend[i] = 0;
        }
    }
}

static void SetStreamTrackParam(nw::snd::SoundArchive::StreamTrackInfo* tmp, const Sound::StreamSoundInfo::Track& trackInfo, const StreamSource& source)
{
    tmp->volume = trackInfo.getVolume();
    tmp->pan = trackInfo.getPan();
    tmp->span = trackInfo.getSPan();
    tmp->flags = trackInfo.getFlags();

    if (source.enableSend)
    {
        tmp->mainSend = trackInfo.getMainSend();
        for (u32 j = 0; j < nw::snd::AUX_BUS_NUM; j++)
        {
            tmp->fxSend[j] = trackInfo.getFxSend(j);
        }
    }
    else
    {
        tmp->mainSend = 127;
        for (u32 j = 0; j < nw::snd::AUX_BUS_NUM; j++)
        {
            tmp->fxSend[j] = 0;
        }
    }

    if (source.enableFilter)
    {
        tmp->lpfFreq = trackInfo.getLpfFreq();
        tmp->biquadType = trackInfo.getBiquadType();
        tmp->biquadValue = trackInfo.getBiquadValue();
    }
    else
    {
        tmp->lpfFreq = 64;
        tmp->biquadType = 0;
        tmp->biquadValue = 0;
    }
}

bool SoundPlayer::playStream_(const StreamSource& source)
{
    const Sound::StreamSoundInfo& strmSoundInfo = *source.stream;

    sead::FixedSafeString<cPlaybackErrorLength> playbackError;

    if (GetStreamPlaybackError(source, &playbackError))
    {
        MessageLog::instance()->addPopup({ playbackError, nullptr });
        return false;
    }

    if (strmSoundInfo.getStreamType() == Sound::StreamSoundInfo::StreamType::Opus && source.sound)
    {
        Bfsar* archive = source.sound->getArchive();

        if (!archive || !opusstream::AttachStreamWaves(*archive, source.sound))
            return false;
    }

    StreamSoundPlayer::SetupArg setupArg;
    setupArg.allocChannelCount = strmSoundInfo.getAllocateChannelCount();
    setupArg.allocTrackFlag = strmSoundInfo.getAllocateTrackFlags();

    SetStreamItemParam(&setupArg, source);

    const WaveFile* mainWave = nullptr;
    WaveFile::Encoding mainEncoding = WaveFile::Encoding::DspAdpcm;
    u32 mainSampleRate = 0;

    const Sound::StreamSoundInfo::Track* playingTracks[cStrmTrackNum] = {};

    u32 channelIdxStart = 0;
    for (u32 i = 0; i < strmSoundInfo.getTrackList().size(); i++)
    {
        nw::snd::SoundArchive::StreamTrackInfo& tmp = setupArg.tracks[i];

        const Item* trackItem = strmSoundInfo.getTrackList().nth(i)->val();
        const Sound::StreamSoundInfo::Track& trackInfo = *static_cast<const Sound::StreamSoundInfo::Track*>(trackItem);

        if (!trackInfo.getWaveFileRef().isAttached())
        {
            MessageLog::instance()->addPopup({ sead::FormatFixedSafeString<64>(messages::playback::cTrackNoWaveFileFormat, i).cstr(), nullptr });
            return false;
        }

        const WaveFile& waveFile = *static_cast<const WaveFile*>(trackInfo.getWaveFileRef().getItem());

        if (waveFile.getChannels().isEmpty())
        {
            MessageLog::instance()->addPopup({ sead::FormatFixedSafeString<64>(messages::playback::cTrackNoChannelsFormat, i).cstr(), nullptr });
            return false;
        }

        const_cast<WaveFile&>(waveFile).updateLoop();

        if (i == 0)
        {
            mainWave = &waveFile;
            mainEncoding = waveFile.getEncoding();
            mainSampleRate = waveFile.getSampleRate();
        }
        else
        {
            if (mainEncoding != waveFile.getEncoding())
            {
                MessageLog::instance()->addPopup({ messages::stream::cTracksSameEncoding, nullptr });
                return false;
            }

            if (mainSampleRate != waveFile.getSampleRate())
            {
                MessageLog::instance()->addPopup({ messages::stream::cTracksSameSampleRate, nullptr });
                return false;
            }
        }

        SetStreamTrackParam(&tmp, trackInfo, source);
        tmp.channelCount = static_cast<u8>(trackInfo.getChannelCount());

        playingTracks[i] = &trackInfo;

        u32 count = sead::Mathu::min(static_cast<u32>(tmp.channelCount), nw::snd::WAVE_CHANNEL_MAX);
        for (u32 ch = 0; ch < count; ch++)
        {
            tmp.globalChannelIndex[ch] = channelIdxStart + ch;
        }

        channelIdxStart += count;
    }

    {
        snd::internal::driver::SoundThreadLock lock;
        fadeOutPlayers_();
        switchStreamPlayer_();

        mStreamPlayer->init();
        mCurrentPlayer = mStreamPlayer;
        mOwner = mOwnerContext;

        if (source.sound)
            SetSoundParam(mStreamPlayer, *source.sound);

        mStreamPlayer->setup(setupArg);
        mStreamPlayer->prepare(strmSoundInfo);
        remapStreamTrackSwitches_(strmSoundInfo, playingTracks);
        initPlayerParam_();
        initPlayerTrack_();
        initPlayerLoop_();

        mSampleCount = mStreamPlayer->getSampleCount();
        mSampleRate = mStreamPlayer->getSampleRate();

        mPlayingWaveFile = mainWave;

        for (u32 i = 0; i < cStrmTrackNum; i++)
            mPlayingStreamTracks[i] = playingTracks[i];

        mPlayingTracksStream = &strmSoundInfo;
    }

    mPlayingStream = source;
    mPlayingSound = source.sound;

    if (source.sound)
        sSelectedItem = source.sound;

    return true;
}

bool SoundPlayer::playWaveSound_(const Sound* sound, u32 startOffsetSample)
{
    sead::FixedSafeString<cPlaybackErrorLength> playbackError;

    if (GetWaveSoundPlaybackError(*sound, &playbackError))
    {
        MessageLog::instance()->addPopup({ playbackError, nullptr });
        return false;
    }

    const Item* waveFile = sound->getWaveSoundInfo().getWaveFileRef().getItem();

    if (!playWaveFile(*static_cast<const WaveFile*>(waveFile), -1, sound, startOffsetSample))
        return false;

    sSelectedItem = const_cast<Sound*>(sound);
    mPlayingSound = sound;

    return true;
}

bool SoundPlayer::playSeqFile(const SequenceFile& seqFile, const sead::SafeString& startLabel, const Bank** bankArray, const Sound* sound)
{
    if (mSeqExport.isActive)
        return false;

    if (!seqFile.isValid())
    {
        MessageLog::instance()->addPopup({ messages::sequence::cNotCompiled, nullptr });
        return false;
    }

    u32 startOffset = seqFile.getLabelOffset(startLabel);
    if (startOffset == SequenceFile::cInvaldOffset)
    {
        MessageLog::instance()->addPopup({ sead::FormatFixedSafeString<128>(messages::sound::cStartLabelNotFoundFormat,startLabel.cstr()).cstr(), nullptr });
        return false;
    }

    u32 allocTracks = seqFile.getLabelAllocTracks(startLabel);

    const BankFile* banks[nw::snd::SoundArchive::SEQ_BANK_MAX] = {};

    for (u32 i = 0; i < nw::snd::SoundArchive::SEQ_BANK_MAX; i++)
    {
        const Bank* bank = bankArray[i];
        if (!bank)
            continue;

        SEAD_ASSERT(bank->getItemType() == Item::ItemType::Bank);

        const Item* bankFile = bank->getFileRef().getItem();
        if (!bankFile)
            continue;

        SEAD_ASSERT(bankFile->getItemType() == Item::ItemType::BankFile);
        banks[i] = static_cast<const BankFile*>(bankFile);
    }

    mSeqTimeline = findSeqTimeline_(seqFile, startLabel, banks, true);

    {
        snd::internal::driver::SoundThreadLock lock;
        fadeOutPlayers_();

        mSequencePlayer.init();
        mCurrentPlayer = &mSequencePlayer;
        mOwner = mOwnerContext;

        if (sound)
            SetSoundParam(&mSequencePlayer, *sound);

        s32 origSeqOffset = seqFile.getLabelOffset(startLabel, false);
        if (origSeqOffset == startOffset)
            origSeqOffset = -1;

        mSequencePlayer.setup(allocTracks, &mSequenceNoteOnCallback2);
        mSequencePlayer.prepare(seqFile, startOffset, banks, origSeqOffset);
        initPlayerParam_();
        initPlayerTrack_();
        initPlayerLoop_();
        initSeqVars_();

        mSampleCount = 0;
        mSampleRate = 0;

        resetPlayingSound();
        mPlayingWaveFile = nullptr;
    }

    return true;
}

u32 SoundPlayer::getSeqCurrentTick() const
{
    const u32 tickCounter = mSequencePlayer.getTickCounter();

    return mSeqTimeline.wrapTick(tickCounter > 0 ? tickCounter - 1 : 0);
}

const SeqTimeline& SoundPlayer::getSeqTimeline(const Sound& sound)
{
    static const SeqTimeline sNoTimeline;

    if (sound.getSoundType() != Sound::SoundType::Seq)
        return sNoTimeline;

    const Sound::SequenceSoundInfo& seqSoundInfo = sound.getSequenceSoundInfo();
    const Item* seqFileItem = seqSoundInfo.getSequenceFileRef().getItem();

    if (!seqFileItem)
        return sNoTimeline;

    const BankFile* banks[nw::snd::SoundArchive::SEQ_BANK_MAX] = {};

    for (u32 i = 0; i < nw::snd::SoundArchive::SEQ_BANK_MAX; i++)
    {
        const Item* bank = seqSoundInfo.getBankRef(i).getItem();

        if (bank)
            banks[i] = static_cast<const BankFile*>(static_cast<const Bank*>(bank)->getFileRef().getItem());
    }

    return findSeqTimeline_(*static_cast<const SequenceFile*>(seqFileItem), seqSoundInfo.getStartLabel(), banks, false);
}

const SeqTimeline& SoundPlayer::findSeqTimeline_(const SequenceFile& seqFile, const sead::SafeString& startLabel, const BankFile* const* banks, bool isPlay)
{
    SeqScanSetup setup = {};
    setup.revision = seqFile.getSeqBytesRevision();
    setup.startOffset = seqFile.getLabelOffset(startLabel);
    setup.allocTracks = seqFile.getLabelAllocTracks(startLabel);
    setup.seqBytesSize = seqFile.getSeqBytesSize();

    for (u32 i = 0; i < nw::snd::SoundArchive::SEQ_BANK_MAX; i++)
        setup.bankFiles[i] = banks[i];

    for (s32 i = 0; i < SequenceSoundPlayer::cGlobalVariableNum; i++)
        setup.globalVars[i] = SequenceSoundPlayer::sGlobalVariable[i];

    for (s32 i = 0; i < SequenceSoundPlayer::cPlayerVariableNum; i++)
        setup.playerVars[i] = mPlayerVars[i].enable ? mPlayerVars[i].value : static_cast<s16>(SequenceSoundPlayer::cVariableDefaultValue);

    for (s32 trackNo = 0; trackNo < SequenceSoundPlayer::cTrackNumPerPlayer; trackNo++)
    {
        for (s32 i = 0; i < SequenceTrack::cTrackVariableNum; i++)
            setup.trackVars[trackNo][i] = mTrackVars[trackNo][i].enable ? mTrackVars[trackNo][i].value : static_cast<s16>(SequenceSoundPlayer::cVariableDefaultValue);
    }

    const u64 editGeneration = modelevents::GetLatestEditGeneration();
    const std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();

    SeqTimelineEntry* entry = nullptr;

    for (SeqTimelineEntry& cached : mSeqTimelineCache)
    {
        if (cached.isUsed && memcmp(&cached.setup, &setup, sizeof(setup)) == 0)
            entry = &cached;
    }

    if (entry && (entry->editGeneration == editGeneration || (!isPlay && begin < entry->nextScanTime)))
        return entry->timeline;

    if (!entry)
    {
        entry = &mSeqTimelineCache[mSeqTimelineCacheNext];
        mSeqTimelineCacheNext = (mSeqTimelineCacheNext + 1) % cSeqTimelineCacheNum;

        entry->setup = setup;
        entry->isUsed = true;
    }

    entry->editGeneration = editGeneration;
    entry->timeline.scan(mSeqScanPlayer, seqFile, setup);

    const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    entry->nextScanTime = end + std::max<std::chrono::steady_clock::duration>(cSeqRescanInterval, (end - begin) * cSeqRescanCostFactor);

    return entry->timeline;
}

bool SoundPlayer::playWaveFile(const WaveFile& wave, s32 channel, const Sound* sound, u32 startOffsetSample, bool updateSelection, const WaveFileScale& scale)
{
    SEAD_ASSERT(wave.getItemType() == Item::ItemType::WaveFile);

    if (mSeqExport.isActive)
        return false;

    sead::FixedSafeString<cPlaybackErrorLength> playbackError;

    if (GetWaveFilePlaybackError(wave, &playbackError))
    {
        MessageLog::instance()->addPopup({ playbackError, nullptr });
        return false;
    }

    const_cast<WaveFile&>(wave).updateLoop();

    {
        snd::internal::driver::SoundThreadLock lock;
        fadeOutPlayers_();

        mWavePlayer.init();
        mCurrentPlayer = &mWavePlayer;
        mOwner = mOwnerContext;

        if (sound)
            SetSoundParam(&mWavePlayer, *sound);

        mWavePlayer.prepare(wave, channel, sound, sound && IsWaveSoundFilterSupported(GetSoundArchive(*sound)), startOffsetSample);

        if (!sound)
            mWavePlayer.setFileScale(scale.volume, scale.pitch);

        initPlayerParam_();
        initPlayerLoop_();

        mSampleCount = mWavePlayer.getSampleCount();
        mSampleRate = mWavePlayer.getSampleRate();

        resetPlayingSound();
        mPlayingWaveFile = &wave;
    }

    if (updateSelection)
        sSelectedItem = const_cast<WaveFile*>(&wave);

    return true;
}

void SoundPlayer::pause(bool isPause)
{
    if (isCurrentPlayer())
    {
        snd::internal::driver::SoundThreadLock lock;
        mCurrentPlayer->pause(isPause);
    }
}

bool SoundPlayer::seek(f32 progress)
{
    progress = sead::Mathf::clamp2(0.0f, progress, 1.0f);

    Item* selectedItem = sSelectedItem;

    if (mWavePlayer.isActive())
    {
        snd::internal::driver::SoundThreadLock lock;
        mWavePlayer.seek(mSampleCount * progress);
    }

    if (mStreamPlayer->isActive())
    {
        snd::internal::driver::SoundThreadLock lock;
        mStreamPlayer->seek(mSampleCount * progress);
    }

    if (mCurrentPlayer == &mSequencePlayer && mSequencePlayer.isActive() && mSeqTimeline.hasLength() && mSeqTimeline.endTick > 0)
    {
        const u32 endTick = mSeqTimeline.endTick;
        const u32 loopStartTick = mSeqTimeline.loopStartTick;
        const bool isLoop = mSeqTimeline.kind == SeqTimeline::Kind::Loop;

        u32 target = mSeqTimeline.calcTick(mSeqTimeline.calcMsec(endTick) * progress);
        if (target >= endTick)
            target = endTick - 1;

        const u32 cur = getSeqCurrentTick();
        u32 skip = 0;

        if (target >= cur)
        {
            skip = target - cur;
        }
        else if (isLoop && mLoopEnabled && target >= loopStartTick)
        {
            skip = endTick - cur + target - loopStartTick;
        }
        else if (mPlayingSound)
        {
            const Sound* sound = mPlayingSound;

            if (playSound(sound))
                skip = target;
        }

        if (skip > 0)
        {
            snd::internal::driver::SoundThreadLock lock;
            mSequencePlayer.requestSkip(skip);
        }
    }

    sSelectedItem = selectedItem;

    return true;
}

void SoundPlayer::reset()
{
    resetPlayingSound();
    resetLastPlayedSound();
    resetPlayingWaveFile();
    forgetStreamTracks_();

    mSampleRate = 0;
    mSampleCount = 0;

    stopAllBankNotes(true);
    stopAllPlayers();
    stopAllVoices();
}

void SoundPlayer::refreshPlayingSound_()
{
    if (!isCurrentPlayer() || (!mPlayingSound && !mPlayingStream.isValid()))
        return;

    snd::internal::driver::SoundThreadLock lock;

    if (mPlayingSound)
        SetSoundParam(mCurrentPlayer, *mPlayingSound);

    if (isCurrentPlayerWave() && mPlayingSound && mPlayingSound->getSoundType() == Sound::SoundType::Wave)
    {
        mWavePlayer.setWaveSoundInfo(*mPlayingSound, IsWaveSoundFilterSupported(GetSoundArchive(*mPlayingSound)));
        return;
    }

    if (!isCurrentPlayerStream() || !mPlayingStream.isValid())
        return;

    const Sound::StreamSoundInfo& strmSoundInfo = *mPlayingStream.stream;

    StreamSoundPlayer::SetupArg setupArg;
    SetStreamItemParam(&setupArg, mPlayingStream);
    mStreamPlayer->setItemData(setupArg);

    const Sound::StreamSoundInfo::Track::List& trackList = strmSoundInfo.getTrackList();

    for (s32 i = 0; i < trackList.size(); i++)
    {
        const Sound::StreamSoundInfo::Track* track = static_cast<const Sound::StreamSoundInfo::Track*>(trackList.nth(i)->val());

        for (u32 trackNo = 0; trackNo < cStrmTrackNum; trackNo++)
        {
            if (mPlayingStreamTracks[trackNo] != track)
                continue;

            nw::snd::SoundArchive::StreamTrackInfo trackInfo;
            SetStreamTrackParam(&trackInfo, *track, mPlayingStream);
            mStreamPlayer->setTrackDataInfo(trackNo, trackInfo);
            break;
        }
    }
}

static bool GetStreamRegion(StreamSoundPlayer::Region* region, const Sound::StreamSoundInfo& stream, s32 regionNo)
{
    const StreamFileLayout* layout = stream.getFileLayout();

    if (!layout || regionNo < 0 || regionNo >= static_cast<s32>(layout->regions.size()))
        return false;

    region->no = regionNo;
    region->start = layout->regions[regionNo].start;
    region->end = layout->regions[regionNo].end;

    return true;
}

bool SoundPlayer::playStreamRegion(const StreamSource& source, s32 regionNo)
{
    if (mSeqExport.isActive)
        return false;

    StreamSoundPlayer::Region region;

    if (!GetStreamRegion(&region, *source.stream, regionNo))
        return false;

    snd::internal::driver::SoundThreadLock lock;

    const bool isPlaying = mPlayingStream.stream == source.stream && isCurrentPlayerStream() && mStreamPlayer->isActive();

    if (!isPlaying && !playStream(source))
        return false;

    return mStreamPlayer->playRegion(region);
}

void SoundPlayer::setNextStreamRegion(s32 regionNo)
{
    if (!mPlayingStream.isValid() || !isCurrentPlayerStream() || !mStreamPlayer->isActive())
        return;

    snd::internal::driver::SoundThreadLock lock;

    StreamSoundPlayer::Region region;

    if (GetStreamRegion(&region, *mPlayingStream.stream, regionNo))
        mStreamPlayer->setNextRegion(region);
    else
        mStreamPlayer->clearNextRegion();
}

s32 SoundPlayer::getPlayingStreamRegion() const
{
    if (!isCurrentPlayerStream() || !mStreamPlayer->isActive())
        return -1;

    snd::internal::driver::SoundThreadLock lock;
    return mStreamPlayer->getPlayingRegionNo();
}

s32 SoundPlayer::getNextStreamRegion() const
{
    if (!isCurrentPlayerStream() || !mStreamPlayer->isActive())
        return -1;

    snd::internal::driver::SoundThreadLock lock;
    return mStreamPlayer->getNextRegionNo();
}

void SoundPlayer::renumberStreamRegions(const s32* newNos, s32 count)
{
    if (!isCurrentPlayerStream() || !mStreamPlayer->isActive())
        return;

    snd::internal::driver::SoundThreadLock lock;
    mStreamPlayer->renumberRegions(newNos, count);
}

s32 SoundPlayer::getPlaySamplePosition(bool isOriginalSamplePosition) const
{
    s32 currentSample = 0;

    if (mWavePlayer.isActive())
    {
        currentSample = mWavePlayer.getPlaySamplePosition(isOriginalSamplePosition);
    }

    if (mStreamPlayer->isActive())
    {
        currentSample = mStreamPlayer->getPlaySamplePosition(isOriginalSamplePosition);
    }

    return currentSample;
}
