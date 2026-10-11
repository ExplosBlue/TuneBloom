#include <ui/items/ItemListInternal.h>

#include <ui/file/File.h>

#include <bfsar/model/Sound.h>

Sound* CloneSound(Sound* src)
{
    Sound* dup = new Sound();

    dup->setEnableName(src->isEnableName());
    {
        u32 copyNum = 1;
        sead::FixedSafeString<256> newName;
        do {
            newName = src->getName();
            newName.appendWithFormat("_%u", copyNum);
            copyNum++;
        } while (!sBfsar.validateName(newName));
        dup->getName() = newName;
    }

    if (src->getPlayerRef().isAttached())
        dup->getPlayerRef().attach(src->getPlayerRef().getItem());

    dup->setVolume(src->getVolume());
    dup->setRemoteFilter(src->getRemoteFilter());
    dup->setSoundType(src->getSoundType());

    dup->setEnablePanParam(src->isEnablePanParam());
    dup->setPanMode(src->getPanMode());
    dup->setPanCurve(src->getPanCurve());

    dup->setEnablePlayerParam(src->isEnablePlayerParam());
    dup->setPlayerPriority(src->getPlayerPriority());
    dup->setActorPlayerId(src->getActorPlayerId());

    for (u32 i = 0; i < 4; i++)
    {
        dup->setEnableUserParam(i, src->isEnableUserParam(i));
        dup->setUserParam(i, src->getUserParam(i));
    }

    dup->setEnableIsFrontBypass(src->isEnableIsFrontBypass());
    dup->setIsFrontBypass(src->getIsFrontBypass());

    dup->setEnableSound3DInfo(src->isEnableSound3DInfo());
    {
        Sound::Sound3DInfo& dst3D = dup->getSound3DInfo();
        const Sound::Sound3DInfo& src3D = src->getSound3DInfo();
        dst3D.setFlags(src3D.getFlags());
        dst3D.setDecayRatio(src3D.getDecayRatio());
        dst3D.setDecayCurve(src3D.getDecayCurve());
        dst3D.setDopplerFactor(src3D.getDopplerFactor());
    }

    {
        Sound::SequenceSoundInfo& dstSeq = dup->getSequenceSoundInfo();
        Sound::SequenceSoundInfo& srcSeq = src->getSequenceSoundInfo();

        if (srcSeq.getSequenceFileRef().isAttached())
            dstSeq.getSequenceFileRef().attach(srcSeq.getSequenceFileRef().getItem());

        for (u32 i = 0; i < 4; i++)
        {
            if (srcSeq.getBankRef(i).isAttached())
                dstSeq.getBankRef(i).attach(srcSeq.getBankRef(i).getItem());
        }

        dstSeq.setEnableStartOffset(srcSeq.isEnableStartOffset());
        dstSeq.getStartLabel() = srcSeq.getStartLabel();
        dstSeq.setEnablePriority(srcSeq.isEnablePriority());
        dstSeq.setIsReleasePriorityFix(srcSeq.getIsReleasePriorityFix());
    }

    {
        Sound::StreamSoundInfo& dstStrm = dup->getStreamSoundInfo();
        Sound::StreamSoundInfo& srcStrm = src->getStreamSoundInfo();

        dstStrm.getPath() = srcStrm.getPath();
        dstStrm.copyAllocationFrom(srcStrm);
        dstStrm.setPitch(srcStrm.getPitch());
        dstStrm.setMainSend(srcStrm.getMainSend());
        for (u32 i = 0; i < 3; i++)
            dstStrm.setFxSend(i, srcStrm.getFxSend(i));
        dstStrm.setEnableStreamSoundExtension(srcStrm.isEnableStreamSoundExtension());
        dstStrm.setStreamType(srcStrm.getStreamType());
        dstStrm.setIsLoop(srcStrm.getIsLoop());
        dstStrm.setLoopStartFrame(srcStrm.getLoopStartFrame());
        dstStrm.setLoopEndFrame(srcStrm.getLoopEndFrame());

        dstStrm.setHasPrefetch(srcStrm.hasPrefetch());

        u32 trackIdx = 0;
        for (auto it = srcStrm.getTrackList().begin(); it != srcStrm.getTrackList().end(); ++it, ++trackIdx)
        {
            Sound::StreamSoundInfo::Track* srcTrack = static_cast<Sound::StreamSoundInfo::Track*>(*it);
            Sound::StreamSoundInfo::Track* dstTrack = new Sound::StreamSoundInfo::Track();

            if (srcTrack->getWaveFileRef().isAttached())
                dstTrack->getWaveFileRef().attach(srcTrack->getWaveFileRef().getItem());

            dstTrack->setId(trackIdx);
            dstTrack->setEnableName(srcTrack->isEnableName());
            dstTrack->getName() = srcTrack->getName();

            dstTrack->setVolume(srcTrack->getVolume());
            dstTrack->setPan(srcTrack->getPan());
            dstTrack->setSPan(srcTrack->getSPan());
            dstTrack->setFlags(srcTrack->getFlags());
            dstTrack->setMainSend(srcTrack->getMainSend());
            dstTrack->setLpfFreq(srcTrack->getLpfFreq());
            dstTrack->setBiquadType(srcTrack->getBiquadType());
            dstTrack->setBiquadValue(srcTrack->getBiquadValue());
            for (u32 i = 0; i < 3; i++)
                dstTrack->setFxSend(i, srcTrack->getFxSend(i));

            dstTrack->copyChannelsFrom(*srcTrack);

            dstStrm.getTrackList().pushBack(dstTrack);
        }
    }

    {
        Sound::WaveSoundInfo& dstWave = dup->getWaveSoundInfo();
        Sound::WaveSoundInfo& srcWave = src->getWaveSoundInfo();

        if (srcWave.getWaveFileRef().isAttached())
            dstWave.getWaveFileRef().attach(srcWave.getWaveFileRef().getItem());

        dstWave.setAllocateTrackCount(srcWave.getAllocateTrackCount());
        dstWave.setEnablePriority(srcWave.isEnablePriority());
        dstWave.setChannelPriority(srcWave.getChannelPriority());
        dstWave.setIsReleasePriorityFix(srcWave.getIsReleasePriorityFix());
        dstWave.setEnablePan(srcWave.isEnablePan());
        dstWave.setPan(srcWave.getPan());
        dstWave.setSurroundPan(srcWave.getSurroundPan());
        dstWave.setEnablePitch(srcWave.isEnablePitch());
        dstWave.setPitch(srcWave.getPitch());
        dstWave.setEnableSend(srcWave.isEnableSend());
        dstWave.setMainSend(srcWave.getMainSend());
        for (u32 i = 0; i < 3; i++)
            dstWave.setFxSend(i, srcWave.getFxSend(i));
        dstWave.setEnableEnvelope(srcWave.isEnableEnvelope());
        dstWave.setAdshrCurve(srcWave.getAdshrCurve());
        dstWave.setEnableFilter(srcWave.isEnableFilter());
        dstWave.setLpfFreq(srcWave.getLpfFreq());
        dstWave.setBiquadType(srcWave.getBiquadType());
        dstWave.setBiquadValue(srcWave.getBiquadValue());
    }

    dup->copyStreamFileBaselineFrom(*src);

    return dup;
}
