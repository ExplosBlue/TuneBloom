#pragma once

#include <snd/SequenceSoundPlayer.h>

#include <vector>

class SequenceFile;

struct SeqScanSetup
{
    u32 revision;
    u32 startOffset;
    u32 allocTracks;
    u32 seqBytesSize;

    const BankFile* bankFiles[nw::snd::SoundArchive::SEQ_BANK_MAX];

    s16 globalVars[SequenceSoundPlayer::cGlobalVariableNum];
    s16 playerVars[SequenceSoundPlayer::cPlayerVariableNum];
    s16 trackVars[SequenceSoundPlayer::cTrackNumPerPlayer][SequenceTrack::cTrackVariableNum];
};

struct SeqVarUse
{
    u16 global = 0;
    u16 player = 0;
    u16 track[SequenceSoundPlayer::cTrackNumPerPlayer] = {};
    u32 tracks = 0;

    void add(const SequenceSoundPlayer& player);
    u32 count() const;
};

struct SeqTimeline
{
    enum class Kind
    {
        None,
        Finite,
        Loop,
        NoEnd
    };

    struct TempoPoint
    {
        u32 tick;
        f64 msec;
        f64 msecPerTick;
    };

    static constexpr u32 cMaxScanTicks = 400000;

    Kind kind = Kind::None;
    u32 endTick = 0;
    u32 loopStartTick = 0;
    std::vector<TempoPoint> tempoPoints;
    SeqVarUse varUse;

    void scan(SequenceSoundPlayer& player, const SequenceFile& seqFile, const SeqScanSetup& setup);

    bool hasLength() const { return kind == Kind::Finite || kind == Kind::Loop; }

    u32 wrapTick(u32 tick) const;
    f64 calcMsec(u32 tick) const;
    u32 calcTick(f64 msec) const;

    f32 calcSeconds(u32 tick) const { return static_cast<f32>(calcMsec(tick) / 1000.0); }
};
