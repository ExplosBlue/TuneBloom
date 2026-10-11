#include <bfsar/seq/SeqTimeline.h>

#include <bfsar/files/SequenceFile.h>

#include <bfsar/files/BankFile.h>
#include <bfsar/files/WaveFile.h>

#include <snd/MmlParser.h>

#include <algorithm>
#include <bit>
#include <cmath>

static u64 MixHash(u64 hash, u64 value)
{
    hash = (hash ^ value) * 0x9E3779B97F4A7C15ull;

    return hash ^ (hash >> 32);
}

void SeqVarUse::add(const SequenceSoundPlayer& seqPlayer)
{
    global |= seqPlayer.getUsedGlobalVariables();
    player |= seqPlayer.getUsedLocalVariables();

    for (s32 i = 0; i < SequenceSoundPlayer::cTrackNumPerPlayer; i++)
        track[i] |= seqPlayer.getTrack_(i).getUsedVariables();
}

u32 SeqVarUse::count() const
{
    u32 count = std::popcount(global) + std::popcount(player);

    for (u16 bits : track)
        count += std::popcount(bits);

    return count;
}

static bool HasTrackEvent(const SequenceSoundPlayer& player)
{
    for (s32 i = 0; i < SequenceSoundPlayer::cTrackNumPerPlayer; i++)
    {
        const SequenceTrack* track = player.getPlayerTrack(i);

        if (track && track->isOpened() && track->getParserTrackParam().wait <= 1)
            return true;
    }

    return false;
}

static void SkipWaitTick(SequenceSoundPlayer& player)
{
    for (s32 i = 0; i < SequenceSoundPlayer::cTrackNumPerPlayer; i++)
    {
        SequenceTrack* track = player.getPlayerTrack(i);

        if (track && track->isOpened())
            track->getParserTrackParam().wait--;
    }
}

static u64 HashParserState(SequenceSoundPlayer& player)
{
    const SequenceSoundPlayer::ParserPlayerParam& playerParam = player.getParserPlayerParam();

    u64 hash = MixHash(playerParam.tempo, playerParam.timebase);

    for (s32 i = 0; i < SequenceSoundPlayer::cPlayerVariableNum + SequenceSoundPlayer::cGlobalVariableNum; i++)
        hash = MixHash(hash, static_cast<u16>(*player.getVariablePtr(i)));

    for (s32 i = 0; i < SequenceSoundPlayer::cTrackNumPerPlayer; i++)
    {
        const SequenceTrack* track = player.getPlayerTrack(i);

        if (!track || !track->isOpened())
        {
            hash = MixHash(hash, 0);
            continue;
        }

        const SequenceTrack::ParserTrackParam& param = track->getParserTrackParam();

        hash = MixHash(hash, static_cast<u64>(param.currentAddr - param.baseAddr) + 1);
        hash = MixHash(hash, static_cast<u32>(param.wait));
        hash = MixHash(hash, (param.cmpFlag ? 1 : 0) | (param.noteWaitFlag ? 2 : 0) | (param.noteFinishWait ? 4 : 0) | (param.callStackDepth << 8));

        for (u32 depth = 0; depth < param.callStackDepth; depth++)
        {
            const SequenceTrack::ParserTrackParam::CallStack& callStack = param.callStack[depth];

            hash = MixHash(hash, static_cast<u64>(callStack.address - param.baseAddr));
            hash = MixHash(hash, callStack.loopFlag ? 0x100 | callStack.loopCount : 0);
        }

        for (s32 varNo = 0; varNo < SequenceTrack::cTrackVariableNum; varNo++)
            hash = MixHash(hash, static_cast<u16>(track->getTrackVariable(varNo)));
    }

    return hash;
}

namespace
{

constexpr f64 cNoteNoEndMsec = 1.0e12;

class ScanNoteOnCallback : public NoteOnCallback
{
public:
    snd::internal::driver::Channel* noteOn(SequenceSoundPlayer*, u8 bankIndex, const NoteOnInfo& noteOnInfo) override
    {
        SequenceTrack* track = static_cast<SequenceTrack*>(noteOnInfo.channelCallbackData);
        f64& endMsec = trackEndMsec[track->getPlayerTrackNo()];

        endMsec = std::max(endMsec, nowMsec + calcNoteMsec(*track, bankIndex, noteOnInfo));

        return nullptr;
    }

    f64 calcNoteMsec(const SequenceTrack& track, u8 bankIndex, const NoteOnInfo& noteOnInfo) const
    {
        if (bankIndex >= nw::snd::SoundArchive::SEQ_BANK_MAX || !bankFiles[bankIndex])
            return 0.0;

        const BankFile::Instrument* instrument = bankFiles[bankIndex]->getInstrument(noteOnInfo.prgNo);
        const BankFile::KeyRegion* keyRegion = instrument ? instrument->getKeyRegion(noteOnInfo.key) : nullptr;
        const BankFile::VelocityRegion* region = keyRegion ? keyRegion->getVelocityRegion(noteOnInfo.velocity) : nullptr;

        if (!region)
            return 0.0;

        const WaveFile* waveFile = static_cast<const WaveFile*>(region->getWaveFileRef().getItem());

        if (!waveFile || waveFile->getIsLoopDirty() || waveFile->getChannels().isEmpty() || waveFile->getSampleRate() == 0)
            return 0.0;

        const SequenceTrack::ParserTrackParam& param = track.getParserTrackParam();
        const f64 bend = param.pitchBend.getValue() / 128.0 * param.bendRange;
        const f64 pitch = std::pow(2.0, (noteOnInfo.key - region->getRootKey() + bend) / 12.0) * region->getPitch();

        const f64 noteMsec = noteOnInfo.length * msecPerTick;
        const bool isToEnd = noteOnInfo.length < 0 || region->getIsIgnoreNoteOff();

        if (waveFile->getIsLoop())
            return isToEnd ? cNoteNoEndMsec : noteMsec;

        if (pitch <= 0.0)
            return isToEnd ? cNoteNoEndMsec : noteMsec;

        const f64 waveMsec = waveFile->getSampleCount() * 1000.0 / (waveFile->getSampleRate() * pitch);

        return isToEnd ? waveMsec : std::min(noteMsec, waveMsec);
    }

    const BankFile* const* bankFiles = nullptr;
    f64 nowMsec = 0.0;
    f64 msecPerTick = 0.0;
    f64 trackEndMsec[SequenceSoundPlayer::cTrackNumPerPlayer] = {};
};

}

static bool ResolveNoteFinishWait(SequenceSoundPlayer& player, const ScanNoteOnCallback& callback, f64 msecPerTick)
{
    for (s32 i = 0; i < SequenceSoundPlayer::cTrackNumPerPlayer; i++)
    {
        SequenceTrack* track = player.getPlayerTrack(i);

        if (!track || !track->isOpened() || !track->getParserTrackParam().noteFinishWait)
            continue;

        const f64 restMsec = callback.trackEndMsec[i] - callback.nowMsec;

        if (restMsec >= cNoteNoEndMsec * 0.5)
            return false;

        SequenceTrack::ParserTrackParam& param = track->getParserTrackParam();
        param.noteFinishWait = false;
        param.wait = restMsec > 0.0 ? static_cast<s32>(std::min(std::ceil(restMsec / msecPerTick), static_cast<f64>(SeqTimeline::cMaxScanTicks))) : 0;
    }

    return true;
}

void SeqTimeline::scan(SequenceSoundPlayer& player, const SequenceFile& seqFile, const SeqScanSetup& setup)
{
    kind = Kind::None;
    endTick = 0;
    loopStartTick = 0;
    tempoPoints.clear();
    varUse = SeqVarUse();

    if (!seqFile.isValid() || !seqFile.getSeqBytes() || setup.startOffset >= seqFile.getSeqBytesSize())
        return;

    volatile s16 globalVars[SequenceSoundPlayer::cGlobalVariableNum];

    for (s32 i = 0; i < SequenceSoundPlayer::cGlobalVariableNum; i++)
        globalVars[i] = setup.globalVars[i];

    ScanNoteOnCallback callback;
    callback.bankFiles = setup.bankFiles;

    player.init();
    player.clearPlayingFile();
    player.setGlobalVariableStorage(globalVars);
    player.setup(setup.allocTracks, &callback);

    for (s32 i = 0; i < SequenceSoundPlayer::cPlayerVariableNum; i++)
        player.setLocalVariable(i, setup.playerVars[i]);

    for (s32 trackNo = 0; trackNo < SequenceSoundPlayer::cTrackNumPerPlayer; trackNo++)
    {
        for (s32 i = 0; i < SequenceTrack::cTrackVariableNum; i++)
            player.getTrack_(trackNo).setTrackVariable(i, setup.trackVars[trackNo][i]);
    }

    SequenceTrack* startTrack = player.getPlayerTrack(0);

    if (startTrack)
    {
        MmlParser::sSeqParamEndian = seqFile.getSeqParamEndian();

        startTrack->setSeqData(seqFile.getSeqBytes(), setup.startOffset, -1);
        startTrack->open();

        kind = Kind::NoEnd;
    }

    std::vector<u64> hashes;

    u64 eventHash = 0;
    u32 eventTick = 0;

    // Brent cycle detection
    u32 anchor = 0;
    u32 power = 1;
    u32 period = 0;

    f64 msec = 0.0;
    f64 msecPerTick = 0.0;

    for (u32 tick = 0; startTrack && tick < cMaxScanTicks; tick++)
    {
        if (tick == 0 || HasTrackEvent(player))
        {
            callback.nowMsec = msec;
            callback.msecPerTick = tick == 0 ? 1.0 / player.calcTickPerMsec() : msecPerTick;

            if (player.parseNextTick(true) != 0)
            {
                kind = Kind::Finite;
                endTick = tick;
                break;
            }

            const f32 tickPerMsec = player.calcTickPerMsec();

            if (tickPerMsec <= 0.0f)
                break;

            if (1.0 / tickPerMsec != msecPerTick)
            {
                msecPerTick = 1.0 / tickPerMsec;
                tempoPoints.push_back({ tick, msec, msecPerTick });
            }

            if (!ResolveNoteFinishWait(player, callback, msecPerTick))
                break;

            eventHash = HashParserState(player);
            eventTick = tick;
        }
        else
        {
            SkipWaitTick(player);
        }

        msec += msecPerTick;
        hashes.push_back(MixHash(eventHash, tick - eventTick));

        if (tick == 0)
            continue;

        if (period != 0)
        {
            if (hashes[tick] != hashes[tick - period])
                break;

            if (tick - anchor < period * 2)
                continue;

            u32 start = anchor;

            while (start > 0 && hashes[start - 1] == hashes[start - 1 + period])
                start--;

            kind = Kind::Loop;
            loopStartTick = start;
            endTick = start + period;
            break;
        }

        if (hashes[tick] == hashes[anchor])
        {
            period = tick - anchor;
            continue;
        }

        if (tick - anchor == power)
        {
            anchor = tick;
            power *= 2;
        }
    }

    varUse.tracks = setup.allocTracks;
    varUse.add(player);

    player.finishPlayer(true);
    player.setGlobalVariableStorage(nullptr);

    if (kind == Kind::Loop)
    {
        while (!tempoPoints.empty() && tempoPoints.back().tick >= endTick)
            tempoPoints.pop_back();
    }
}

u32 SeqTimeline::wrapTick(u32 tick) const
{
    if (kind == Kind::Loop && tick >= endTick)
        return loopStartTick + (tick - loopStartTick) % (endTick - loopStartTick);

    if (kind == Kind::Finite && tick > endTick)
        return endTick;

    return tick;
}

f64 SeqTimeline::calcMsec(u32 tick) const
{
    auto it = std::upper_bound(tempoPoints.begin(), tempoPoints.end(), tick, [](u32 value, const TempoPoint& point) { return value < point.tick; });

    if (it == tempoPoints.begin())
        return 0.0;

    --it;

    return it->msec + (tick - it->tick) * it->msecPerTick;
}

u32 SeqTimeline::calcTick(f64 msec) const
{
    auto it = std::upper_bound(tempoPoints.begin(), tempoPoints.end(), msec, [](f64 value, const TempoPoint& point) { return value < point.msec; });

    if (it == tempoPoints.begin())
        return 0;

    --it;

    return it->tick + static_cast<u32>((msec - it->msec) / it->msecPerTick);
}
