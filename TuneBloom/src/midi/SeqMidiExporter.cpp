#include <midi/SeqMidiExporter.h>

#include <bfsar/files/SequenceFile.h>
#include <bfsar/model/Sound.h>

#include <midi/ExportCommon.h>

#include <snd/MmlCommand.h>

#include <basis/seadTypes.h>
#include <math/seadMathCalcCommon.h>
#include <prim/seadEndian.h>
#include <prim/seadSafeString.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <unordered_set>
#include <vector>

// MIDI file
static constexpr u32 cMidiPPQN          = 480;
static constexpr u32 cMicrosecPerMinute = 60000000;
static constexpr u32 cTempoMetaMax      = 0xFFFFFF;
static constexpr s32 cPitchBendCenter   = 8192;
static constexpr s32 cPitchBendMax      = 16383;
static constexpr s32 cPitchBendStep     = 64;

static constexpr u8 cStatusNoteOff   = 0x80;
static constexpr u8 cStatusNoteOn    = 0x90;
static constexpr u8 cStatusControl   = 0xB0;
static constexpr u8 cStatusProgram   = 0xC0;
static constexpr u8 cStatusPitchBend = 0xE0;
static constexpr u8 cStatusSysEx     = 0xF0;
static constexpr u8 cStatusMeta      = 0xFF;

static constexpr u8 cMetaTrackName  = 0x03;
static constexpr u8 cMetaEndOfTrack = 0x2F;
static constexpr u8 cMetaTempo      = 0x51;

// Sequence player
static constexpr u32 cTrackNum        = cSeqMidiTrackNum;
static constexpr u32 cBankNum         = 4;
static constexpr u32 cProgramNum      = 128;
static constexpr u32 cMidiBankMax     = 127;
static constexpr u32 cMidiBanksPerSeqBank = (cMidiBankMax + 1) / cBankNum;
static constexpr u32 cVarNum          = 16;
static constexpr u32 cCallStackDepth  = 10;
static constexpr u32 cAllocTrackSize  = 3;
static constexpr u32 cDefaultTimebase = 48;
static constexpr u32 cDefaultTempo    = 120;
static constexpr s32 cTempoMax        = 1023;
static constexpr s16 cVarDefault      = -1;
static constexpr s32 cCommandMax      = 10000;
static constexpr u32 cTickMax         = cDefaultTimebase * 4 * 2000;

static constexpr u64 cHashSeed  = 14695981039346656037ull;
static constexpr u64 cHashPrime = 1099511628211ull;

static constexpr const char* cDefaultSoundName = "Sequence";

enum class MidiCC : u8
{
    BankSelect  = 0,
    ModDepth    = 1,
    InitPan     = 3,
    PortaTime   = 5,
    DataEntry   = 6,
    Volume      = 7,
    SurroundPan = 9,
    Pan         = 10,
    Volume2     = 11,
    MainVolume  = 12,
    Prio        = 14,
    ModSpeed    = 21,
    ModType     = 22,
    ModRange    = 23,
    ModDelay    = 26,
    ModDelay10  = 27,
    Sweep       = 28,
    Sweep24     = 29,
    BiquadType  = 30,
    BiquadValue = 31,
    Damper      = 64,
    PortaSw     = 65,
    Monophonic  = 68,
    LpfCutoff   = 74,
    EnvHold     = 79,
    Porta       = 84,
    Attack      = 85,
    Decay       = 86,
    Sustain     = 87,
    Release     = 88,
    FxSendA     = 91,
    FxSendB     = 92,
    FxSendC     = 93,
    MainSend    = 95,
    RpnLsb      = 100,
    RpnMsb      = 101,
    FrontBypass = 119,
    None        = 0xFF
};

static MidiCC GetController(u32 cmd)
{
    switch (cmd)
    {
        case MmlCommand::MML_INIT_PAN:     return MidiCC::InitPan;
        case MmlCommand::MML_PRIO:         return MidiCC::Prio;
        case MmlCommand::MML_PORTA_TIME:   return MidiCC::PortaTime;
        case MmlCommand::MML_MOD_DEPTH:    return MidiCC::ModDepth;
        case MmlCommand::MML_MOD_SPEED:    return MidiCC::ModSpeed;
        case MmlCommand::MML_MOD_TYPE:     return MidiCC::ModType;
        case MmlCommand::MML_MOD_RANGE:    return MidiCC::ModRange;
        case MmlCommand::MML_ATTACK:       return MidiCC::Attack;
        case MmlCommand::MML_DECAY:        return MidiCC::Decay;
        case MmlCommand::MML_SUSTAIN:      return MidiCC::Sustain;
        case MmlCommand::MML_RELEASE:      return MidiCC::Release;
        case MmlCommand::MML_ENV_HOLD:     return MidiCC::EnvHold;
        case MmlCommand::MML_LPF_CUTOFF:   return MidiCC::LpfCutoff;
        case MmlCommand::MML_BIQUAD_TYPE:  return MidiCC::BiquadType;
        case MmlCommand::MML_BIQUAD_VALUE: return MidiCC::BiquadValue;
        case MmlCommand::MML_FXSEND_A:     return MidiCC::FxSendA;
        case MmlCommand::MML_FXSEND_B:     return MidiCC::FxSendB;
        case MmlCommand::MML_FXSEND_C:     return MidiCC::FxSendC;
        case MmlCommand::MML_MAINSEND:     return MidiCC::MainSend;
        default:                           return MidiCC::None;
    }
}

// MIDI writing helpers

static void WriteVarLen(std::vector<u8>& out, u32 value)
{
    u8 buf[5];
    s32 n = 0;

    buf[n++] = value & 0x7F;
    value >>= 7;

    while (value > 0)
    {
        buf[n++] = (value & 0x7F) | 0x80;
        value >>= 7;
    }

    for (s32 i = n - 1; i >= 0; i--)
        out.push_back(buf[i]);
}

static void WriteBE16(std::vector<u8>& out, u16 value)
{
    out.push_back(static_cast<u8>(value >> 8));
    out.push_back(static_cast<u8>(value));
}

static void WriteBE32(std::vector<u8>& out, u32 value)
{
    WriteBE16(out, static_cast<u16>(value >> 16));
    WriteBE16(out, static_cast<u16>(value));
}

static void WriteTag(std::vector<u8>& out, const char* tag)
{
    out.insert(out.end(), tag, tag + 4);
}

// MIDI track

static bool IsVolumeController(MidiCC controller)
{
    return controller == MidiCC::Volume || controller == MidiCC::Volume2 || controller == MidiCC::MainVolume;
}

enum class MidiEventPrio : u8
{
    Other,
    NoteOff,
    NoteOn
};

class MidiTrack
{
public:
    void addNoteOn(u32 tick, u8 channel, u8 key, u8 velocity)
    {
        const u8 bytes[] = { static_cast<u8>(cStatusNoteOn | channel), key, velocity };
        addEvent_(tick, MidiEventPrio::NoteOn, bytes, sizeof(bytes));
    }

    void addNoteOff(u32 tick, u8 channel, u8 key)
    {
        const u8 bytes[] = { static_cast<u8>(cStatusNoteOff | channel), key, 0 };
        addEvent_(tick, MidiEventPrio::NoteOff, bytes, sizeof(bytes));
    }

    void addController(u32 tick, u8 channel, MidiCC controller, s32 value)
    {
        if (IsVolumeController(controller))
            mVolumeEvents.push_back({ static_cast<u32>(mData.size()) + 2, controller, value });

        u8 data = static_cast<u8>(sead::Mathi::clamp2(0, value, 127));
        const u8 bytes[] = { static_cast<u8>(cStatusControl | channel), static_cast<u8>(controller), data };
        addEvent_(tick, MidiEventPrio::Other, bytes, sizeof(bytes));
    }

    s32 calcVolumeMax(MidiCC controller) const
    {
        s32 valueMax = 0;

        for (const VolumeEvent& event : mVolumeEvents)
        {
            if (event.controller == controller)
                valueMax = sead::Mathi::max(valueMax, event.value);
        }

        return valueMax;
    }

    void scaleVolume(MidiCC controller, s32 valueMax)
    {
        for (const VolumeEvent& event : mVolumeEvents)
        {
            if (event.controller == controller)
                mData[event.dataOffset] = static_cast<u8>(sead::Mathi::clamp2(0, (event.value * 127 + valueMax / 2) / valueMax, 127));
        }
    }

    void setChannel(u8 channel)
    {
        for (const Event& event : mEvents)
        {
            u8& status = mData[event.offset];
            if (status >= cStatusNoteOff && status < cStatusSysEx)
                status = static_cast<u8>((status & 0xF0) | channel);
        }
    }

    void addProgram(u32 tick, u8 channel, u8 program)
    {
        const u8 bytes[] = { static_cast<u8>(cStatusProgram | channel), program };
        addEvent_(tick, MidiEventPrio::Other, bytes, sizeof(bytes));
    }

    void addPitchBend(u32 tick, u8 channel, s32 value)
    {
        s32 bend = sead::Mathi::clamp2(0, value, cPitchBendMax);
        const u8 bytes[] = { static_cast<u8>(cStatusPitchBend | channel), static_cast<u8>(bend & 0x7F), static_cast<u8>(bend >> 7) };
        addEvent_(tick, MidiEventPrio::Other, bytes, sizeof(bytes));
    }

    void addTempo(u32 tick, u32 bpm)
    {
        u32 microsec = sead::Mathu::min(cMicrosecPerMinute / bpm, cTempoMetaMax);
        const u8 data[] = { static_cast<u8>(microsec >> 16), static_cast<u8>(microsec >> 8), static_cast<u8>(microsec) };
        addMeta_(tick, cMetaTempo, data, sizeof(data));
    }

    void addTrackName(const char* name)
    {
        if (name && *name)
            addMeta_(0, cMetaTrackName, reinterpret_cast<const u8*>(name), static_cast<u32>(std::strlen(name)));
    }

    void writeChunk(std::vector<u8>& out, u32 endTick)
    {
        std::sort(mEvents.begin(), mEvents.end(), [](const Event& a, const Event& b) { return a.key < b.key; });

        WriteTag(out, "MTrk");
        size_t sizePos = out.size();
        WriteBE32(out, 0);

        u32 lastTick = 0;
        for (const Event& event : mEvents)
        {
            u32 tick = static_cast<u32>(event.key >> cTickShift);
            WriteVarLen(out, tick - lastTick);
            lastTick = tick;

            out.insert(out.end(), mData.begin() + event.offset, mData.begin() + event.offset + event.size);
        }

        // End of track
        WriteVarLen(out, sead::Mathu::max(endTick, lastTick) - lastTick);
        out.push_back(cStatusMeta);
        out.push_back(cMetaEndOfTrack);
        out.push_back(0);

        u32 chunkSize = static_cast<u32>(out.size() - sizePos - 4);
        for (u32 i = 0; i < 4; i++)
            out[sizePos + i] = static_cast<u8>(chunkSize >> (24 - 8 * i));
    }

private:
    static constexpr u32 cTickShift = 32;
    static constexpr u32 cPrioShift = 30;

    struct Event
    {
        u64 key;
        u32 offset;
        u32 size;
    };

    // Volume event
    struct VolumeEvent
    {
        u32    dataOffset;
        MidiCC controller;
        s32    value;
    };

    void pushEvent_(u32 tick, MidiEventPrio prio, u32 offset)
    {
        u64 key = (static_cast<u64>(tick) << cTickShift) | (static_cast<u64>(prio) << cPrioShift) | mEvents.size();
        mEvents.push_back({ key, offset, static_cast<u32>(mData.size()) - offset });
    }

    void addEvent_(u32 tick, MidiEventPrio prio, const u8* bytes, u32 size)
    {
        u32 offset = static_cast<u32>(mData.size());
        mData.insert(mData.end(), bytes, bytes + size);
        pushEvent_(tick, prio, offset);
    }

    void addMeta_(u32 tick, u8 type, const u8* data, u32 size)
    {
        u32 offset = static_cast<u32>(mData.size());
        mData.push_back(cStatusMeta);
        mData.push_back(type);
        WriteVarLen(mData, size);
        mData.insert(mData.end(), data, data + size);
        pushEvent_(tick, MidiEventPrio::Other, offset);
    }

    std::vector<Event>       mEvents;
    std::vector<u8>          mData;
    std::vector<VolumeEvent> mVolumeEvents;
};

// Bytecode reader

struct SeqReader
{
    const u8* cur;
    const u8* end;
    bool      littleEndian;

    u8 readU8()
    {
        if (cur >= end)
            return MmlCommand::MML_FIN;

        return *cur++;
    }

    u16 readU16()
    {
        u8 b0 = readU8();
        u8 b1 = readU8();

        if (littleEndian)
            return static_cast<u16>(b0 | (b1 << 8));

        return static_cast<u16>((b0 << 8) | b1);
    }

    u32 readU24()
    {
        u8 b0 = readU8();
        u8 b1 = readU8();
        u8 b2 = readU8();

        if (littleEndian)
            return b0 | (b1 << 8) | (b2 << 16);

        return (b0 << 16) | (b1 << 8) | b2;
    }

    s32 readVarLen()
    {
        s32 value = 0;

        for (s32 i = 0; i < 4; i++)
        {
            u8 b = readU8();
            value = (value << 7) | (b & 0x7F);

            if (!(b & 0x80))
                break;
        }

        return value;
    }
};

u8 getSeqMidiChannel(u32 trackNo, u16 usedTrackFlags)
{
    if (trackNo < cMidiDrumChannel)
        return static_cast<u8>(trackNo);

    if (trackNo < cTrackNum - 1)
        return static_cast<u8>(trackNo + 1);

    for (u32 i = 0; i < cTrackNum - 1; i++)
    {
        if (!(usedTrackFlags & (1 << i)))
            return getSeqMidiChannel(i, usedTrackFlags);
    }

    return cMidiDrumChannel;
}

static u64 HashMix(u64 hash, u64 value)
{
    return (hash ^ value) * cHashPrime;
}

static u64 HashVars(const s16* vars, u32 num)
{
    u64 hash = cHashSeed;

    for (u32 i = 0; i < num; i++)
        hash = HashMix(hash, static_cast<u16>(vars[i]));

    return hash;
}

class SeqMidiConverter
{
public:
    SeqMidiConverter(const u8* base, u32 size, bool littleEndian, const char* name, const SeqMidiFilter* filter = nullptr);

    bool run(u32 startOffset);
    bool write(const char* path);
    SeqMidiContents getContents() const;

private:
    enum class ArgType
    {
        None,
        U8,
        S16,
        VarLen,
        Random,
        Variable
    };

    struct Command
    {
        u32  cmd   = 0;
        u32  cmdex = 0;
        s32  arg1  = 0;
        s32  arg2  = 0;
        bool exec  = true;
    };

    struct CallFrame
    {
        u32  address;
        bool loopFlag;
        u8   loopCount;
    };

    struct Track
    {
        MidiTrack               midi;
        u8                      no             = 0;
        u8                      channel        = 0;
        bool                    opened         = false;
        bool                    used           = false;
        u32                     noteNum        = 0;
        u32                     writtenNoteNum = 0;
        u32                     offset         = 0;
        s32                     wait           = 0;
        bool                    cmpFlag        = true;
        bool                    noteWait       = true;
        bool                    tieFlag        = false;
        s32                     tieKey         = -1;
        s8                      transpose      = 0;
        u8                      velocityRange  = 127;
        u8                      bankIndex      = 0;
        u16                     prgNo          = 0;
        s32                     volume         = 127;
        s32                     volume2        = 127;
        s32                     pan            = 64;
        s32                     surroundPan    = 0;
        s32                     pitchBend      = 0;
        s16                     vars[cVarNum];
        u64                     varsHash       = 0;
        CallFrame               callStack[cCallStackDepth];
        u32                     callStackDepth = 0;
        std::unordered_set<u64> visited;
    };

    u32 midiTickAt_(s32 ticksAhead = 0) const;
    s32 calcMinWait_() const;
    u64 hashState_(const Track& track, u32 offset) const;
    void updateVarsHash_(Track& track);
    s16* getVariablePtr_(Track& track, s32 varNo);

    void openTrack_(u32 trackNo, u32 offset);
    void closeTrack_(Track& track);
    void assignChannels_();
    void scaleVolumes_();
    void updateTrack_(Track& track);
    bool isTrackWritten_(const Track& track) const;
    void countNote_(Track& track, const SeqProgram& program);

    s32 readArg_(SeqReader& reader, Track& track, ArgType type);
    Command readCommand_(SeqReader& reader, Track& track);

    void execCommand_(Track& track, const Command& command);
    void execNote_(Track& track, const Command& command);
    bool execSequenceCommand_(Track& track, const Command& command);
    bool execChannelCommand_(Track& track, const Command& command);
    bool execNoteStateCommand_(Track& track, const Command& command);
    bool execControllerCommand_(Track& track, const Command& command);
    void execFlowCommand_(Track& track, const Command& command);
    void execVarCommand_(Track& track, const Command& command);
    void jump_(Track& track, u32 target);
    void return_(Track& track);
    void loopEnd_(Track& track);

    void noteOn_(Track& track, s32 key, s32 velocity, s32 length);
    void releaseTieNote_(Track& track);
    void addProgram_(Track& track);
    void addController_(Track& track, MidiCC controller, s32 value, s32 ticksAhead = 0);
    void addPitchBend_(Track& track, s32 value, s32 ticksAhead = 0);
    void rampController_(Track& track, MidiCC controller, s32* current, s32 target, s32 frames);
    void rampPitchBend_(Track& track, s32 target, s32 frames);

    template <typename Emit>
    static void Ramp_(s32* current, s32 target, s32 frames, Emit emit);

    const u8*                mBase;
    u32                      mSize;
    bool                     mLittleEndian;
    const char*              mName;
    const SeqMidiFilter*     mFilter;

    u32                      mTimebase   = cDefaultTimebase;
    f64                      mMidiTick   = 0.0;
    u32                      mEndTick    = 0;
    bool                     mHasTempo   = false;
    s32                      mMainVolume = 127;
    s16                      mVars[cVarNum * 2];
    u64                      mVarsHash   = 0;
    Track                    mTracks[cTrackNum];
    MidiTrack                mConductor;
    std::vector<SeqMidiPart> mParts;
};

SeqMidiConverter::SeqMidiConverter(const u8* base, u32 size, bool littleEndian, const char* name, const SeqMidiFilter* filter)
    : mBase(base)
    , mSize(size)
    , mLittleEndian(littleEndian)
    , mName(name)
    , mFilter(filter)
{
    std::fill(std::begin(mVars), std::end(mVars), cVarDefault);

    for (u32 i = 0; i < cTrackNum; i++)
    {
        Track& track = mTracks[i];

        track.no = static_cast<u8>(i);
        track.channel = getSeqMidiChannel(i, 0xFFFF);
        std::fill(std::begin(track.vars), std::end(track.vars), cVarDefault);
        updateVarsHash_(track);
    }

    mConductor.addTrackName(mName);
}

bool SeqMidiConverter::run(u32 startOffset)
{
    if (!mBase || mSize == 0)
        return false;

    if (startOffset >= mSize)
        startOffset = 0;

    if (mBase[startOffset] == MmlCommand::MML_ALLOC_TRACK)
        startOffset += cAllocTrackSize;

    openTrack_(0, startOffset);

    u32 tick = 0;
    while (tick < cTickMax)
    {
        for (Track& track : mTracks)
            updateTrack_(track);

        s32 minWait = calcMinWait_();
        if (minWait < 0)
            break;

        s32 step = sead::Mathi::max(minWait, 1);

        for (Track& track : mTracks)
        {
            if (track.opened)
                track.wait -= step - 1;
        }

        tick += step;
        mMidiTick += static_cast<f64>(step) * cMidiPPQN / mTimebase;
    }

    for (Track& track : mTracks)
        closeTrack_(track);

    assignChannels_();
    scaleVolumes_();

    if (!mHasTempo)
        mConductor.addTempo(0, cDefaultTempo);

    mEndTick = midiTickAt_();

    return true;
}

bool SeqMidiConverter::write(const char* path)
{
    u16 trackCount = 1;
    for (const Track& track : mTracks)
    {
        if (isTrackWritten_(track))
            trackCount++;
    }

    std::vector<u8> file;

    // Header
    WriteTag(file, "MThd");
    WriteBE32(file, 6);
    WriteBE16(file, 1);
    WriteBE16(file, trackCount);
    WriteBE16(file, cMidiPPQN);

    // Track chunks
    mConductor.writeChunk(file, mEndTick);

    for (Track& track : mTracks)
    {
        if (isTrackWritten_(track))
            track.midi.writeChunk(file, mEndTick);
    }

    return WriteExportFile(path, file);
}

SeqMidiContents SeqMidiConverter::getContents() const
{
    SeqMidiContents contents;

    for (const Track& track : mTracks)
    {
        if (track.used)
            contents.trackFlags |= 1 << track.no;
    }

    contents.parts = mParts;

    return contents;
}

bool SeqMidiConverter::isTrackWritten_(const Track& track) const
{
    if (!track.used)
        return false;

    if (!mFilter)
        return true;

    if (!mFilter->isTrackEnabled(track.no))
        return false;

    return track.writtenNoteNum > 0 || track.noteNum == 0;
}

void SeqMidiConverter::countNote_(Track& track, const SeqProgram& program)
{
    track.noteNum++;

    for (SeqMidiPart& part : mParts)
    {
        if (part.trackNo != track.no || part.program != program)
            continue;

        part.noteCount++;
        return;
    }

    mParts.push_back({ track.no, program, 1 });
}

u32 SeqMidiConverter::midiTickAt_(s32 ticksAhead) const
{
    return static_cast<u32>(mMidiTick + static_cast<f64>(ticksAhead) * cMidiPPQN / mTimebase + 0.5);
}

s32 SeqMidiConverter::calcMinWait_() const
{
    s32 minWait = -1;

    for (const Track& track : mTracks)
    {
        if (track.opened && (minWait < 0 || track.wait < minWait))
            minWait = track.wait;
    }

    return minWait;
}

u64 SeqMidiConverter::hashState_(const Track& track, u32 offset) const
{
    u64 hash = HashMix(cHashSeed, offset);

    hash = HashMix(hash, track.cmpFlag);
    hash = HashMix(hash, track.varsHash);
    hash = HashMix(hash, mVarsHash);

    for (u32 i = 0; i < track.callStackDepth; i++)
    {
        const CallFrame& frame = track.callStack[i];

        hash = HashMix(hash, frame.address);
        hash = HashMix(hash, (frame.loopCount << 1) | frame.loopFlag);
    }

    return hash;
}

void SeqMidiConverter::updateVarsHash_(Track& track)
{
    track.varsHash = HashVars(track.vars, cVarNum);
    mVarsHash = HashVars(mVars, cVarNum * 2);
}

s16* SeqMidiConverter::getVariablePtr_(Track& track, s32 varNo)
{
    if (varNo < 0)
        return nullptr;

    if (varNo < static_cast<s32>(cVarNum * 2))
        return &mVars[varNo];

    if (varNo < static_cast<s32>(cVarNum * 3))
        return &track.vars[varNo - cVarNum * 2];

    return nullptr;
}

void SeqMidiConverter::openTrack_(u32 trackNo, u32 offset)
{
    Track& track = mTracks[trackNo];

    closeTrack_(track);

    if (!track.used)
    {
        sead::FormatFixedSafeString<300> trackName("%s_track%d", mName, trackNo);
        track.midi.addTrackName(trackName.cstr());
        addProgram_(track);
        addController_(track, MidiCC::Volume, track.volume);
        addController_(track, MidiCC::Volume2, track.volume2);
        track.used = true;
    }

    track.offset = offset;
    track.wait = 0;
    track.callStackDepth = 0;
    track.opened = true;
}

void SeqMidiConverter::closeTrack_(Track& track)
{
    releaseTieNote_(track);
    track.opened = false;
}

void SeqMidiConverter::assignChannels_()
{
    const u16 usedTrackFlags = getContents().trackFlags;

    for (Track& track : mTracks)
    {
        const u8 channel = getSeqMidiChannel(track.no, usedTrackFlags);
        if (channel == track.channel)
            continue;

        track.channel = channel;
        track.midi.setChannel(channel);
    }
}

void SeqMidiConverter::scaleVolumes_()
{
    for (MidiCC controller : { MidiCC::Volume, MidiCC::Volume2, MidiCC::MainVolume })
    {
        s32 valueMax = 0;
        for (const Track& track : mTracks)
            valueMax = sead::Mathi::max(valueMax, track.midi.calcVolumeMax(controller));

        if (valueMax <= 127)
            continue;

        for (Track& track : mTracks)
            track.midi.scaleVolume(controller, valueMax);
    }
}

void SeqMidiConverter::updateTrack_(Track& track)
{
    if (!track.opened)
        return;

    if (track.wait > 0)
        track.wait--;

    for (s32 counter = 0; track.opened && track.wait == 0; counter++)
    {
        if (counter >= cCommandMax || track.offset >= mSize)
        {
            closeTrack_(track);
            return;
        }

        track.visited.insert(hashState_(track, track.offset));

        SeqReader reader = { mBase + track.offset, mBase + mSize, mLittleEndian };
        Command command = readCommand_(reader, track);
        track.offset = static_cast<u32>(reader.cur - mBase);

        if (command.exec)
            execCommand_(track, command);
    }
}

s32 SeqMidiConverter::readArg_(SeqReader& reader, Track& track, ArgType type)
{
    switch (type)
    {
        case ArgType::U8:
            return reader.readU8();

        case ArgType::S16:
            return static_cast<s16>(reader.readU16());

        case ArgType::VarLen:
            return reader.readVarLen();

        case ArgType::Random:
        {
            s16 min = static_cast<s16>(reader.readU16());
            s16 max = static_cast<s16>(reader.readU16());
            return (static_cast<s32>(min) + max) / 2;
        }

        case ArgType::Variable:
        {
            const s16* var = getVariablePtr_(track, reader.readU8());
            return var ? *var : 0;
        }

        default:
            return 0;
    }
}

SeqMidiConverter::Command SeqMidiConverter::readCommand_(SeqReader& reader, Track& track)
{
    Command command;
    ArgType argType = ArgType::None;
    ArgType timeArgType = ArgType::None;

    command.cmd = reader.readU8();

    if (command.cmd == MmlCommand::MML_IF)
    {
        command.cmd = reader.readU8();
        command.exec = track.cmpFlag;
    }

    switch (command.cmd)
    {
        case MmlCommand::MML_TIME:          timeArgType = ArgType::S16; break;
        case MmlCommand::MML_TIME_RANDOM:   timeArgType = ArgType::Random; break;
        case MmlCommand::MML_TIME_VARIABLE: timeArgType = ArgType::Variable; break;
    }

    if (timeArgType != ArgType::None)
        command.cmd = reader.readU8();

    switch (command.cmd)
    {
        case MmlCommand::MML_RANDOM:   argType = ArgType::Random; break;
        case MmlCommand::MML_VARIABLE: argType = ArgType::Variable; break;
    }

    if (argType != ArgType::None)
        command.cmd = reader.readU8();

    auto readArg = [&](ArgType defaultType)
    {
        return readArg_(reader, track, argType != ArgType::None ? argType : defaultType);
    };

    // Note
    if (command.cmd < 0x80)
    {
        command.arg1 = reader.readU8();
        command.arg2 = readArg(ArgType::VarLen);
        return command;
    }

    switch (command.cmd & 0xF0)
    {
        case 0x80:
            switch (command.cmd)
            {
                case MmlCommand::MML_WAIT:
                case MmlCommand::MML_PRG:
                    command.arg1 = readArg(ArgType::VarLen);
                    break;

                case MmlCommand::MML_OPEN_TRACK:
                    command.arg1 = reader.readU8();
                    command.arg2 = reader.readU24();
                    break;

                case MmlCommand::MML_JUMP:
                case MmlCommand::MML_CALL:
                    command.arg1 = reader.readU24();
                    break;
            }
            break;

        case 0xB0:
        case 0xC0:
        case 0xD0:
            command.arg1 = static_cast<u8>(readArg(ArgType::U8));

            if (timeArgType != ArgType::None)
                command.arg2 = static_cast<s16>(readArg_(reader, track, timeArgType));

            if (command.cmd == MmlCommand::MML_TRANSPOSE || command.cmd == MmlCommand::MML_PITCH_BEND)
                command.arg1 = static_cast<s8>(command.arg1);

            break;

        case 0xE0:
            command.arg1 = static_cast<s16>(readArg(ArgType::S16));
            break;

        case 0xF0:
            if (command.cmd != MmlCommand::MML_EX_COMMAND)
                break;

            command.cmdex = reader.readU8();

            switch (command.cmdex & 0xF0)
            {
                case 0x80:
                case 0x90:
                    command.arg1 = reader.readU8();
                    command.arg2 = static_cast<s16>(readArg(ArgType::S16));
                    break;

                case 0xA0:
                case 0xB0:
                    command.arg1 = static_cast<u8>(readArg(ArgType::U8));
                    break;

                case 0xE0:
                    command.arg1 = static_cast<u16>(readArg(ArgType::S16));
                    break;
            }
            break;
    }

    bool isPrefix = (command.cmd & 0xF0) == 0xA0;
    if (isPrefix || command.cmd == MmlCommand::MML_ALLOC_TRACK)
    {
        command.cmd = MmlCommand::MML_FIN;
        command.exec = true;
    }

    return command;
}

void SeqMidiConverter::execCommand_(Track& track, const Command& command)
{
    const s32 arg1 = command.arg1;

    // Note
    if (command.cmd < 0x80)
    {
        execNote_(track, command);
        return;
    }

    MidiCC controller = GetController(command.cmd);
    if (controller != MidiCC::None)
    {
        if (arg1 <= 127)
            addController_(track, controller, arg1);

        return;
    }

    if (execSequenceCommand_(track, command))
        return;

    if (execChannelCommand_(track, command))
        return;

    if (execNoteStateCommand_(track, command))
        return;

    if (execControllerCommand_(track, command))
        return;

    execFlowCommand_(track, command);
}

void SeqMidiConverter::execNote_(Track& track, const Command& command)
{
    const s32 arg1 = command.arg1;
    const s32 arg2 = command.arg2;

    s32 key = sead::Mathi::clamp2(0, static_cast<s32>(command.cmd) + track.transpose, 127);
    noteOn_(track, key, arg1, arg2);

    if (track.noteWait)
        track.wait = sead::Mathi::clamp2(0, arg2, static_cast<s32>(cTickMax));
}

bool SeqMidiConverter::execSequenceCommand_(Track& track, const Command& command)
{
    const s32 arg1 = command.arg1;

    switch (command.cmd)
    {
        case MmlCommand::MML_WAIT:
            track.wait = sead::Mathi::clamp2(0, arg1, static_cast<s32>(cTickMax));
            break;

        case MmlCommand::MML_FIN:
            closeTrack_(track);
            break;

        case MmlCommand::MML_EX_COMMAND:
            execVarCommand_(track, command);
            break;

        case MmlCommand::MML_TEMPO:
            if (arg1 <= 0)
                break;

            mConductor.addTempo(midiTickAt_(), sead::Mathi::min(arg1, cTempoMax));
            mHasTempo = mHasTempo || midiTickAt_() == 0;
            break;

        case MmlCommand::MML_TIMEBASE:
            if (arg1 > 0)
                mTimebase = static_cast<u32>(arg1);

            break;

        default:
            return false;
    }

    return true;
}

bool SeqMidiConverter::execChannelCommand_(Track& track, const Command& command)
{
    const s32 arg1 = command.arg1;
    const s32 arg2 = command.arg2;

    switch (command.cmd)
    {
        case MmlCommand::MML_PRG:
            if (arg1 > 0xFFFF)
                break;

            track.prgNo = static_cast<u16>(arg1);
            addProgram_(track);
            break;

        case MmlCommand::MML_BANK_SELECT:
            track.bankIndex = static_cast<u8>(arg1);
            addProgram_(track);
            break;

        case MmlCommand::MML_VOLUME:
            rampController_(track, MidiCC::Volume, &track.volume, arg1, arg2);
            break;

        case MmlCommand::MML_VOLUME2:
            rampController_(track, MidiCC::Volume2, &track.volume2, arg1, arg2);
            break;

        case MmlCommand::MML_MAIN_VOLUME:
            rampController_(track, MidiCC::MainVolume, &mMainVolume, arg1, arg2);
            break;

        case MmlCommand::MML_PAN:
            rampController_(track, MidiCC::Pan, &track.pan, arg1, arg2);
            break;

        case MmlCommand::MML_SURROUND_PAN:
            rampController_(track, MidiCC::SurroundPan, &track.surroundPan, arg1, arg2);
            break;

        case MmlCommand::MML_PITCH_BEND:
            rampPitchBend_(track, arg1, arg2);
            break;

        case MmlCommand::MML_BEND_RANGE:
            addController_(track, MidiCC::RpnMsb, 0);
            addController_(track, MidiCC::RpnLsb, 0);
            addController_(track, MidiCC::DataEntry, arg1);
            break;

        default:
            return false;
    }

    return true;
}

bool SeqMidiConverter::execNoteStateCommand_(Track& track, const Command& command)
{
    const s32 arg1 = command.arg1;

    switch (command.cmd)
    {
        case MmlCommand::MML_VELOCITY_RANGE:
            track.velocityRange = static_cast<u8>(arg1);
            break;

        case MmlCommand::MML_TRANSPOSE:
            track.transpose = static_cast<s8>(arg1);
            break;

        case MmlCommand::MML_NOTE_WAIT:
            track.noteWait = arg1 != 0;
            break;

        case MmlCommand::MML_TIE:
            track.tieFlag = arg1 != 0;
            releaseTieNote_(track);
            break;

        default:
            return false;
    }

    return true;
}

bool SeqMidiConverter::execControllerCommand_(Track& track, const Command& command)
{
    const s32 arg1 = command.arg1;

    switch (command.cmd)
    {
        case MmlCommand::MML_MONOPHONIC:
            addController_(track, MidiCC::Monophonic, arg1 != 0 ? 127 : 0);
            break;

        case MmlCommand::MML_FRONT_BYPASS:
            addController_(track, MidiCC::FrontBypass, arg1 != 0 ? 127 : 0);
            break;

        case MmlCommand::MML_DAMPER:
            addController_(track, MidiCC::Damper, arg1 >= 64 ? 127 : 0);
            break;

        case MmlCommand::MML_PORTA:
            addController_(track, MidiCC::Porta, arg1 + track.transpose);
            break;

        case MmlCommand::MML_PORTA_SW:
            addController_(track, MidiCC::PortaSw, arg1 != 0 ? 127 : 0);
            break;

        case MmlCommand::MML_MOD_DELAY:
            if (arg1 <= 127)
                addController_(track, MidiCC::ModDelay, arg1);
            else
                addController_(track, MidiCC::ModDelay10, arg1 / 10);

            break;

        case MmlCommand::MML_SWEEP_PITCH:
            if (-64 <= arg1 && arg1 <= 63)
                addController_(track, MidiCC::Sweep, arg1 + 64);
            else
                addController_(track, MidiCC::Sweep24, arg1 / 24 + 64);

            break;

        default:
            return false;
    }

    return true;
}

void SeqMidiConverter::execFlowCommand_(Track& track, const Command& command)
{
    const u32 arg1 = static_cast<u32>(command.arg1);
    const u32 arg2 = static_cast<u32>(command.arg2);

    switch (command.cmd)
    {
        case MmlCommand::MML_OPEN_TRACK:
            if (arg1 < cTrackNum && arg1 != track.no)
                openTrack_(arg1, arg2);

            break;

        case MmlCommand::MML_JUMP:
            jump_(track, arg1);
            break;

        case MmlCommand::MML_CALL:
            if (track.callStackDepth >= cCallStackDepth)
                break;

            track.callStack[track.callStackDepth++] = { track.offset, false, 0 };
            track.offset = arg1;
            break;

        case MmlCommand::MML_RET:
            return_(track);
            break;

        case MmlCommand::MML_LOOP_START:
            if (track.callStackDepth >= cCallStackDepth)
                break;

            track.callStack[track.callStackDepth++] = { track.offset, true, static_cast<u8>(arg1) };
            break;

        case MmlCommand::MML_LOOP_END:
            loopEnd_(track);
            break;

        default:
            break;
    }
}

void SeqMidiConverter::execVarCommand_(Track& track, const Command& command)
{
    const s32 value = command.arg2;

    if ((command.cmdex & 0xF0) != 0x80 && (command.cmdex & 0xF0) != 0x90)
        return;

    s16* var = getVariablePtr_(track, command.arg1);
    if (!var)
        return;

    switch (command.cmdex)
    {
        case MmlCommand::MML_SETVAR:  *var = static_cast<s16>(value); break;
        case MmlCommand::MML_ADDVAR:  *var += static_cast<s16>(value); break;
        case MmlCommand::MML_SUBVAR:  *var -= static_cast<s16>(value); break;
        case MmlCommand::MML_MULVAR:  *var *= static_cast<s16>(value); break;
        case MmlCommand::MML_RANDVAR: *var = static_cast<s16>(value / 2); break;
        case MmlCommand::MML_ANDVAR:  *var &= value; break;
        case MmlCommand::MML_ORVAR:   *var |= value; break;
        case MmlCommand::MML_XORVAR:  *var ^= value; break;
        case MmlCommand::MML_NOTVAR:  *var = static_cast<s16>(~static_cast<u16>(value)); break;

        case MmlCommand::MML_DIVVAR:
            if (value != 0)
                *var /= static_cast<s16>(value);

            break;

        case MmlCommand::MML_MODVAR:
            if (value != 0)
                *var %= value;

            break;

        case MmlCommand::MML_SHIFTVAR:
            if (value >= 0)
                *var = static_cast<s16>(*var << sead::Mathi::min(value, 15));
            else
                *var = static_cast<s16>(*var >> sead::Mathi::min(-value, 15));

            break;

        case MmlCommand::MML_CMP_EQ: track.cmpFlag = *var == value; return;
        case MmlCommand::MML_CMP_GE: track.cmpFlag = *var >= value; return;
        case MmlCommand::MML_CMP_GT: track.cmpFlag = *var > value; return;
        case MmlCommand::MML_CMP_LE: track.cmpFlag = *var <= value; return;
        case MmlCommand::MML_CMP_LT: track.cmpFlag = *var < value; return;
        case MmlCommand::MML_CMP_NE: track.cmpFlag = *var != value; return;

        default:
            return;
    }

    updateVarsHash_(track);
}

void SeqMidiConverter::jump_(Track& track, u32 target)
{
    bool isBackward = target <= track.offset;

    if (isBackward && track.visited.count(hashState_(track, target)) != 0)
    {
        closeTrack_(track);
        return;
    }

    track.offset = target;
}

void SeqMidiConverter::return_(Track& track)
{
    while (track.callStackDepth > 0)
    {
        const CallFrame& frame = track.callStack[--track.callStackDepth];
        if (frame.loopFlag)
            continue;

        track.offset = frame.address;
        return;
    }
}

void SeqMidiConverter::loopEnd_(Track& track)
{
    if (track.callStackDepth == 0)
        return;

    CallFrame& frame = track.callStack[track.callStackDepth - 1];
    if (!frame.loopFlag)
        return;

    // Endless loop
    if (frame.loopCount == 0)
    {
        jump_(track, frame.address);
        return;
    }

    frame.loopCount--;
    if (frame.loopCount == 0)
    {
        track.callStackDepth--;
        return;
    }

    track.offset = frame.address;
}

void SeqMidiConverter::noteOn_(Track& track, s32 key, s32 velocity, s32 length)
{
    if (velocity > 127)
        return;

    velocity = sead::Mathi::min(velocity * track.velocityRange / 127, 127);
    if (velocity <= 0)
        return;

    const SeqProgram program(track.bankIndex, track.prgNo);
    countNote_(track, program);

    if (mFilter && !mFilter->isEnabled(track.no, program))
        return;

    track.writtenNoteNum++;

    u32 onTick = midiTickAt_();

    if (track.tieFlag)
    {
        if (track.tieKey == key)
            return;

        releaseTieNote_(track);
        track.midi.addNoteOn(onTick, track.channel, static_cast<u8>(key), static_cast<u8>(velocity));
        track.tieKey = key;
        return;
    }

    u32 offTick = sead::Mathu::max(midiTickAt_(sead::Mathi::clamp2(0, length, static_cast<s32>(cTickMax))), onTick + 1);

    track.midi.addNoteOn(onTick, track.channel, static_cast<u8>(key), static_cast<u8>(velocity));
    track.midi.addNoteOff(offTick, track.channel, static_cast<u8>(key));
}

void SeqMidiConverter::releaseTieNote_(Track& track)
{
    if (track.tieKey < 0)
        return;

    track.midi.addNoteOff(midiTickAt_(), track.channel, static_cast<u8>(track.tieKey));
    track.tieKey = -1;
}

void SeqMidiConverter::addProgram_(Track& track)
{
    u32 tick = midiTickAt_();

    track.midi.addController(tick, track.channel, MidiCC::BankSelect, getMidiBank(track.bankIndex, track.prgNo));
    track.midi.addProgram(tick, track.channel, getMidiProgram(track.prgNo));
}

void SeqMidiConverter::addController_(Track& track, MidiCC controller, s32 value, s32 ticksAhead)
{
    track.midi.addController(midiTickAt_(ticksAhead), track.channel, controller, value);
}

void SeqMidiConverter::addPitchBend_(Track& track, s32 value, s32 ticksAhead)
{
    track.midi.addPitchBend(midiTickAt_(ticksAhead), track.channel, cPitchBendCenter + value * cPitchBendStep);
}

void SeqMidiConverter::rampController_(Track& track, MidiCC controller, s32* current, s32 target, s32 frames)
{
    Ramp_(current, target, frames, [&](s32 value, s32 ticksAhead) { addController_(track, controller, value, ticksAhead); });
}

void SeqMidiConverter::rampPitchBend_(Track& track, s32 target, s32 frames)
{
    Ramp_(&track.pitchBend, target, frames, [&](s32 value, s32 ticksAhead) { addPitchBend_(track, value, ticksAhead); });
}

template <typename Emit>
void SeqMidiConverter::Ramp_(s32* current, s32 target, s32 frames, Emit emit)
{
    const s32 origin = *current;
    s32 last = origin;

    for (s32 i = 1; i < frames; i++)
    {
        s32 value = origin + (target - origin) * i / frames;
        if (value == last)
            continue;

        emit(value, i);
        last = value;
    }

    emit(target, sead::Mathi::max(frames, 0));
    *current = target;
}

// Public API

u8 getMidiBank(u32 bankIndex, u32 programNo)
{
    u32 programBank = sead::Mathu::min(programNo / cProgramNum, cMidiBanksPerSeqBank - 1);

    return static_cast<u8>(sead::Mathu::min(bankIndex * cMidiBanksPerSeqBank + programBank, cMidiBankMax));
}

u8 getMidiProgram(u32 programNo)
{
    return static_cast<u8>(programNo % cProgramNum);
}

static bool IsLittleEndian(const SequenceFile& seqFile)
{
    return seqFile.getSeqParamEndian() == sead::Endian::eLittle;
}

static const SequenceFile* GetSequenceFile(const Sound& sound)
{
    if (sound.getSoundType() != Sound::SoundType::Seq)
        return nullptr;

    const Item* item = sound.getSequenceSoundInfo().getSequenceFileRef().getItem();
    if (!item || item->getItemType() != Item::ItemType::SequenceFile)
        return nullptr;

    return static_cast<const SequenceFile*>(item);
}

bool SeqMidiFilter::isTrackEnabled(u32 trackNo) const
{
    return (trackFlags & (1 << trackNo)) != 0;
}

bool SeqMidiFilter::isEnabled(u32 trackNo, const SeqProgram& program) const
{
    if (!isTrackEnabled(trackNo))
        return false;

    return std::find(excludedPrograms.begin(), excludedPrograms.end(), program) == excludedPrograms.end();
}

SeqMidiContents collectSeqMidiContents(const SequenceFile& seqFile, u32 startOffset)
{
    SeqMidiConverter converter(seqFile.getSeqBytes(), seqFile.getSeqBytesSize(), IsLittleEndian(seqFile), "");
    converter.run(startOffset);

    return converter.getContents();
}

SeqMidiContents collectSeqMidiContents(const Sound& sound)
{
    const SequenceFile* seqFile = GetSequenceFile(sound);
    if (!seqFile)
        return SeqMidiContents();

    return collectSeqMidiContents(*seqFile, sound.getSequenceSoundInfo().getStartOffset());
}

bool exportSeqToMidi(const sead::SafeString& path, const SequenceFile& seqFile, const char* trackName, u32 startOffset, const SeqMidiFilter* filter)
{
    SeqMidiConverter converter(seqFile.getSeqBytes(), seqFile.getSeqBytesSize(), IsLittleEndian(seqFile), trackName, filter);

    if (!converter.run(startOffset))
        return false;

    return converter.write(path.cstr());
}

bool exportSeqToMidi(const sead::SafeString& path, const Sound& sound, const SeqMidiFilter* filter)
{
    const SequenceFile* seqFile = GetSequenceFile(sound);
    if (!seqFile)
        return false;

    sead::FixedSafeString<256> name(sound.getNameOrNull());
    if (name.isEmpty())
        name = cDefaultSoundName;

    return exportSeqToMidi(path, *seqFile, name.cstr(), sound.getSequenceSoundInfo().getStartOffset(), filter);
}
