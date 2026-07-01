#include <midi/SeqMidiExporter.h>

#include <bfsar/Bfsar.h>
#include <bfsar/SequenceFile.h>
#include <bfsar/Sound.h>
#include <bfsar/SoundSet.h>

#include <basis/seadTypes.h>
#include <math/seadMathCalcCommon.h>
#include <prim/seadEndian.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <utility>
#include <vector>

// ─── MIDI writing helpers ─────────────────────────────────────

static void writeVLQ(std::vector<u8>& out, u32 value)
{
    u8 buf[5];
    s32 n = 0;
    buf[n++] = value & 0x7F;
    value >>= 7;
    while (value > 0) {
        buf[n++] = (value & 0x7F) | 0x80;
        value >>= 7;
    }
    for (s32 i = n - 1; i >= 0; i--)
        out.push_back(buf[i]);
}

static void writeBE16(std::vector<u8>& out, u16 v)
{
    out.push_back((v >> 8) & 0xFF);
    out.push_back(v & 0xFF);
}

static void writeBE32(std::vector<u8>& out, u32 v)
{
    out.push_back((v >> 24) & 0xFF);
    out.push_back((v >> 16) & 0xFF);
    out.push_back((v >> 8) & 0xFF);
    out.push_back(v & 0xFF);
}

// ─── MIDI track builder ───────────────────────────────────────

enum { PRIO_OTHER = 0, PRIO_NOTE_OFF = 1, PRIO_NOTE_ON = 2 };

class MidiTrackBuilder
{
    struct AbsEvent {
        u32             tick;
        int             prio;
        u32             order;
        std::vector<u8> bytes;
    };

    std::vector<AbsEvent> mEvents;
    u32                   mOrder = 0;

public:
    void addEvent(u32 tick, u8 status, u8 data1, u8 data2 = 0, int prio = PRIO_OTHER)
    {
        AbsEvent e;
        e.tick = tick;
        e.prio = prio;
        e.order = mOrder++;
        e.bytes.push_back(status);
        e.bytes.push_back(data1);
        if ((status & 0xF0) != 0xC0 && (status & 0xF0) != 0xD0)
            e.bytes.push_back(data2);
        mEvents.push_back(std::move(e));
    }

    void addMeta(u32 tick, u8 type, const u8* data, u32 len, int prio = PRIO_OTHER)
    {
        AbsEvent e;
        e.tick = tick;
        e.prio = prio;
        e.order = mOrder++;
        e.bytes.push_back(0xFF);
        e.bytes.push_back(type);
        writeVLQ(e.bytes, len);
        for (u32 i = 0; i < len; i++)
            e.bytes.push_back(data[i]);
        mEvents.push_back(std::move(e));
    }

    void addTempo(u32 tick, u32 usPerQN)
    {
        u8 d[3] = {
            (u8)((usPerQN >> 16) & 0xFF),
            (u8)((usPerQN >> 8) & 0xFF),
            (u8)(usPerQN & 0xFF)
        };
        addMeta(tick, 0x51, d, 3);
    }

    void addTrackName(u32 tick, const char* name)
    {
        if (name && *name)
            addMeta(tick, 0x03, (const u8*)name, (u32)std::strlen(name));
    }

    std::vector<u8> finalize()
    {
        std::stable_sort(mEvents.begin(), mEvents.end(),
            [](const AbsEvent& a, const AbsEvent& b) {
                if (a.tick != b.tick) return a.tick < b.tick;
                if (a.prio != b.prio) return a.prio < b.prio;
                return a.order < b.order;
            });

        std::vector<u8> out;
        u32 last = 0;
        for (const AbsEvent& e : mEvents) {
            writeVLQ(out, e.tick - last);
            last = e.tick;
            out.insert(out.end(), e.bytes.begin(), e.bytes.end());
        }

        // End of track
        writeVLQ(out, 0);
        out.push_back(0xFF);
        out.push_back(0x2F);
        out.push_back(0x00);
        return out;
    }
};

// ─── Bounded bytecode reader ──────────────────────────────────

// All read functions take a pointer-to-pointer and an end bound.
// When *p reaches end they return 0xFF (FIN sentinel) to force
// the parser out of the loop on the next iteration.

static u8 readByte(const u8** p, const u8* end)
{
    if (*p >= end)
        return 0xFF;
    return *(*p)++;
}

static u32 read24(const u8** p, const u8* end)
{
    return ((u32)readByte(p, end) << 16) | ((u32)readByte(p, end) << 8) | readByte(p, end);
}

static u16 read16(const u8** p, const u8* end, bool little)
{
    u16 v = (u16)(readByte(p, end) << 8) | readByte(p, end);
    return little ? sead::Endian::swapU16(v) : v;
}

static u32 readVar(const u8** p, const u8* end)
{
    u32 r = 0;
    u8  b;
    int count = 0;
    do {
        if (count >= 10)  // max plausible VLQ for u32
            break;
        b = readByte(p, end);
        r = (r << 7) | (b & 0x7F);
        count++;
    } while (b & 0x80);
    return r;
}

enum { ARG_U8, ARG_S16, ARG_VMIDI, ARG_RANDOM, ARG_VARIABLE };

static s32 readArg(const u8** p, const u8* end, int type, bool little)
{
    switch (type) {
    case ARG_U8:      return readByte(p, end);
    case ARG_S16:     return (s16)read16(p, end, little);
    case ARG_VMIDI:   return (s32)readVar(p, end);
    case ARG_RANDOM: {
        s16 lo = (s16)read16(p, end, little);
        s16 hi = (s16)read16(p, end, little);
        return ((s32)lo + hi) / 2;
    }
    case ARG_VARIABLE:
        readByte(p, end); // var index
        return 0;
    }
    return 0;
}

// ─── Per-track parsing ────────────────────────────────────────

struct OpenedTrack {
    u8   trackNo;
    u32  offset;
};

struct TrackResult {
    MidiTrackBuilder                   track;
    std::vector<OpenedTrack>           openedTracks;
    u32                                endTick;
};

struct CallFrame {
    u32  retOffset;
    bool isLoop;
    u8   loopCount;
    u32  loopStartOffset;
};

static TrackResult parseTrack(const u8* base, u32 dataSize, bool littleEndian,
                              u32 startOffset, const char* trackName, u8 midiChannel,
                              std::set<std::pair<u16, u16>>* outUsedPrograms = nullptr)
{
    TrackResult   result;
    const u32     midiPPQN = 480;
    u32           off      = startOffset;
    const u8*     end      = base + dataSize;

    // Track-level state
    u32 seqTimebase = 48;
    u32 tempoBPM    = 120;
    u32 tick        = 0;
    s8  transpose   = 0;
    bool noteWait   = true;
    u8  bankIndex   = 0;

    // Control flow
    std::vector<CallFrame> callStack;
    std::vector<u32>       visitedJumps;

    // Skip optional ALLOC_TRACK
    if (off < dataSize && base[off] == 0xFE)
        off += 3;

    // Track name + initial tempo
    result.track.addTrackName(0, trackName);
    result.track.addTempo(0, 60000000 / tempoBPM);

    while (off < dataSize)
    {
        const u8* ptr       = base + off;
        u32       cmd       = readByte(&ptr, end);
        bool      doExec    = true;
        int       argType2  = -1;  // -1 = none
        int       argType   = ARG_U8;
        bool      useArgType = false;

        // ── IF prefix ──────────────────────────────────────────
        if (cmd == 0xA2) {
            cmd = readByte(&ptr, end);
            doExec = false; // cmpFlag is 0 by default
        }

        // ── TIME prefix ────────────────────────────────────────
        if (cmd == 0xA3)       { cmd = readByte(&ptr, end); argType2 = ARG_S16; }
        else if (cmd == 0xA4)  { cmd = readByte(&ptr, end); argType2 = ARG_RANDOM; }
        else if (cmd == 0xA5)  { cmd = readByte(&ptr, end); argType2 = ARG_VARIABLE; }

        // ── RANDOM / VARIABLE prefix ───────────────────────────
        if (cmd == 0xA0) {
            cmd = readByte(&ptr, end);
            argType = ARG_RANDOM;
            useArgType = true;
        } else if (cmd == 0xA1) {
            cmd = readByte(&ptr, end);
            argType = ARG_VARIABLE;
            useArgType = true;
        }

        if (cmd < 0x80)
        {
            // ── NOTE ───────────────────────────────────────────
            u8  key    = (u8)cmd;
            u8  vel    = readByte(&ptr, end);
            s32 length = readArg(&ptr, end, useArgType ? argType : ARG_VMIDI, littleEndian);

            if (doExec) {
                s32 noteKey = (s32)key + transpose;
                noteKey = sead::Mathi::clamp2(0, noteKey, 127);

                u32 scaledLen = 0;
                if (length > 0)
                    scaledLen = (u32)((u64)(s32)length * midiPPQN / seqTimebase);

                u32 offLen = scaledLen > 0 ? scaledLen : 1;
                result.track.addEvent(tick, 0x90 | midiChannel, (u8)noteKey, vel, PRIO_NOTE_ON);
                result.track.addEvent(tick + offLen, 0x80 | midiChannel, (u8)noteKey, 0, PRIO_NOTE_OFF);

                if (noteWait)
                    tick += scaledLen;
            }
            off = (u32)(ptr - base);
            continue;
        }

        s32 arg1 = 0;

        switch (cmd)
        {
        case 0x80: // MML_WAIT
        {
            s32 w = readArg(&ptr, end, useArgType ? argType : ARG_VMIDI, littleEndian);
            if (doExec)
                tick += (u32)((u64)(s32)w * midiPPQN / seqTimebase);
            off = (u32)(ptr - base);
            continue;
        }

        case 0x81: // MML_PRG
        {
            s32 pg = readArg(&ptr, end, useArgType ? argType : ARG_VMIDI, littleEndian);
            if (doExec) {
                result.track.addEvent(tick, 0xC0 | midiChannel, (u8)(pg & 0x7F));
                if (outUsedPrograms)
                    outUsedPrograms->insert({ (u16)bankIndex, (u16)pg });
            }
            off = (u32)(ptr - base);
            continue;
        }

        case 0x88: // MML_OPEN_TRACK
        {
            u8  tno = readByte(&ptr, end);
            u32 tof = read24(&ptr, end);
            if (doExec)
                result.openedTracks.push_back({tno, tof});
            off = (u32)(ptr - base);
            continue;
        }

        case 0x89: // MML_JUMP
        {
            u32 tgt = read24(&ptr, end);
            if (doExec) {
                bool visited = false;
                for (u32 v : visitedJumps)
                    if (v == tgt) { visited = true; break; }
                if (visited) {
                    off = dataSize; // stop
                    continue;
                }
                visitedJumps.push_back(tgt);
                off = tgt;
                continue;
            }
            off = (u32)(ptr - base);
            continue;
        }

        case 0x8A: // MML_CALL
        {
            u32 tgt = read24(&ptr, end);
            u32 retOff = (u32)(ptr - base);
            if (doExec) {
                if (callStack.size() < 64) {
                    callStack.push_back({retOff, false, 0, 0});
                    off = tgt;
                    continue;
                }
            }
            off = retOff;
            continue;
        }

        case 0xFD: // MML_RET
            if (doExec) {
                while (!callStack.empty()) {
                    CallFrame f = callStack.back();
                    callStack.pop_back();
                    if (!f.isLoop) {
                        off = f.retOffset;
                        goto next_track_iter;
                    }
                }
            }
            off = (u32)(ptr - base);
            continue;

        case 0xD4: // MML_LOOP_START
        {
            arg1 = readByte(&ptr, end);
            u32 nextOff = (u32)(ptr - base);
            if (doExec) {
                if (callStack.size() < 64) {
                    u8 count = (u8)(arg1 & 0xFF);
                    if (count == 0) count = 2;
                    callStack.push_back({0, true, count, nextOff});
                }
            }
            off = nextOff;
            continue;
        }

        case 0xFC: // MML_LOOP_END
            if (doExec) {
                for (s32 i = (s32)callStack.size() - 1; i >= 0; i--) {
                    if (callStack[i].isLoop) {
                        if (callStack[i].loopCount > 0) {
                            callStack[i].loopCount--;
                            off = callStack[i].loopStartOffset;
                        } else {
                            callStack.pop_back();
                            off = (u32)(ptr - base);
                        }
                        goto next_track_iter;
                    }
                }
            }
            off = (u32)(ptr - base);
            continue;

        case 0xFF: // MML_FIN
            off = dataSize;
            continue;

        case 0xFB: // MML_ENV_RESET
            off = (u32)(ptr - base);
            continue;

        case 0xF0: // MML_EX_COMMAND
        {
            u32 cmdex = readByte(&ptr, end);
            switch (cmdex & 0xF0)
            {
            case 0x80: case 0x90:
                readByte(&ptr, end);
                readArg(&ptr, end, ARG_S16, littleEndian);
                break;
            case 0xA0: case 0xB0:
                readByte(&ptr, end);
                break;
            case 0xE0:
                readArg(&ptr, end, ARG_S16, littleEndian);
                break;
            }
            off = (u32)(ptr - base);
            continue;
        }

        default:
        {
            bool  inB0DF = ((cmd & 0xF0) == 0xB0) || ((cmd & 0xF0) == 0xC0) || ((cmd & 0xF0) == 0xD0);
            bool  inE0   = (cmd & 0xF0) == 0xE0;

            if (inB0DF) {
                arg1 = readArg(&ptr, end, useArgType ? argType : ARG_U8, littleEndian);
                if (argType2 >= 0)
                    readArg(&ptr, end, argType2, littleEndian);
            } else if (inE0) {
                arg1 = readArg(&ptr, end, useArgType ? argType : ARG_S16, littleEndian);
            }

            if (doExec) {
                switch (cmd)
                {
                case 0xB0:
                    seqTimebase = (u32)(u8)(arg1 & 0xFF);
                    if (seqTimebase == 0) seqTimebase = 48;
                    break;

                case 0xB6:
                    bankIndex = (u8)(arg1 & 0xFF);
                    result.track.addEvent(tick, 0xB0 | midiChannel, 0, (u8)(arg1 & 0xFF));
                    break;

                case 0xC0:
                    result.track.addEvent(tick, 0xB0 | midiChannel, 10, (u8)(arg1 & 0x7F));
                    break;

                case 0xC1:
                    result.track.addEvent(tick, 0xB0 | midiChannel, 7, (u8)(arg1 & 0x7F));
                    break;

                case 0xC3:
                    transpose = (s8)arg1;
                    break;

                case 0xC4:
                {
                    s16 bend14 = (s16)(((s32)(s8)arg1 + 64) * 128);
                    result.track.addEvent(tick, 0xE0 | midiChannel,
                        (u8)(bend14 & 0x7F),
                        (u8)((bend14 >> 7) & 0x7F));
                    break;
                }

                case 0xC7:
                    noteWait = (arg1 != 0);
                    break;

                case 0xDF:
                    result.track.addEvent(tick, 0xB0 | midiChannel, 64,
                        (u8)(arg1 & 0xFF) >= 64 ? 127 : 0);
                    break;

                case 0xE1:
                {
                    s32 t = arg1;
                    if (t < 0) t = 0;
                    if (t > 1023) t = 1023;
                    if (t > 0) {
                        tempoBPM = (u32)t;
                        result.track.addTempo(tick, 60000000 / tempoBPM);
                    }
                    break;
                }

                default:
                    break;
                }
            }

            off = (u32)(ptr - base);
            continue;
        }
        }

    next_track_iter:;
    }

    result.endTick = tick;
    return result;
}

// ─── SMF file writer ──────────────────────────────────────────

static bool writeSMF(const char* path,
                     std::vector<MidiTrackBuilder>& tracks)
{
    std::vector<u8> file;

    auto writeTag = [&](const char tag[4]) {
        for (int i = 0; i < 4; i++)
            file.push_back((u8)tag[i]);
    };

    // ── Header ──
    writeTag("MThd");
    writeBE32(file, 6);          // chunk length
    writeBE16(file, 1);          // format 1
    writeBE16(file, (u16)tracks.size());
    writeBE16(file, 480);        // ticks per quarter note

    // ── Track chunks ──
    for (auto& t : tracks) {
        std::vector<u8> td = t.finalize();
        writeTag("MTrk");
        writeBE32(file, (u32)td.size());
        file.insert(file.end(), td.begin(), td.end());
    }

    // ── Write to disk ──
    FILE* fp = std::fopen(path, "wb");
    if (!fp) return false;
    bool ok = std::fwrite(file.data(), 1, file.size(), fp) == file.size();
    std::fclose(fp);
    return ok;
}

// ─── Public API ───────────────────────────────────────────────

std::set<std::pair<u16, u16>> collectUsedPrograms(const SequenceFile& seqFile, u32 startOffset)
{
    std::set<std::pair<u16, u16>> used;

    const u8* bytes = seqFile.getSeqBytes();
    u32       sz    = seqFile.getSeqBytesSize();
    if (!bytes || sz == 0) return used;

    bool littleEndian = seqFile.getSeqParamEndian() == sead::Endian::eLittle;
    u32 startOff = startOffset < sz ? startOffset : 0;

    auto mainResult = parseTrack(bytes, sz, littleEndian, startOff, "", 0, &used);

    std::vector<OpenedTrack> pending = std::move(mainResult.openedTracks);
    u8 nextChannel = 1;
    while (!pending.empty() && nextChannel < 16)
    {
        OpenedTrack ot = pending.back();
        pending.pop_back();

        auto subResult = parseTrack(bytes, sz, littleEndian, ot.offset, "", nextChannel++, &used);
        for (auto& st : subResult.openedTracks)
            pending.push_back(st);
    }

    return used;
}

bool exportSeqToMidi(const sead::SafeString& path, const SequenceFile& seqFile, const char* trackName, u32 startOffset)
{
    const u8* bytes = seqFile.getSeqBytes();
    u32       sz    = seqFile.getSeqBytesSize();
    if (!bytes || sz == 0) return false;

    bool littleEndian = seqFile.getSeqParamEndian() == sead::Endian::eLittle;
    u32 startOff = startOffset < sz ? startOffset : 0;

    // First pass: parse main track, discover opened sub-tracks
    auto mainResult = parseTrack(bytes, sz, littleEndian, startOff, trackName, 0);

    // Collect all tracks (main + sub)
    struct TrackEntry {
        MidiTrackBuilder builder;
        u32              tickCount;
    };
    std::vector<TrackEntry> trackEntries;
    trackEntries.push_back({std::move(mainResult.track), mainResult.endTick});

    // Discover and parse sub-tracks (breadth-first)
    std::vector<OpenedTrack> pending = std::move(mainResult.openedTracks);
    u8 nextChannel = 1;
    while (!pending.empty() && nextChannel < 16)
    {
        OpenedTrack ot = pending.back();
        pending.pop_back();

        sead::FormatFixedSafeString<32> subName("%s_track%d", trackName, ot.trackNo);
        auto subResult = parseTrack(bytes, sz, littleEndian, ot.offset, subName.cstr(), nextChannel++);

        for (auto& st : subResult.openedTracks)
            pending.push_back(st);

        trackEntries.push_back({std::move(subResult.track), subResult.endTick});
    }

    // ── Write ──
    std::vector<MidiTrackBuilder> midiTracks;
    for (auto& te : trackEntries)
        midiTracks.push_back(std::move(te.builder));

    return writeSMF(path.cstr(), midiTracks);
}

bool exportSeqToMidi(const sead::SafeString& path, const Sound& sound)
{
    if (sound.getSoundType() != Sound::SoundType::Seq)
        return false;

    const Item* item = sound.getSequenceSoundInfo().getSequenceFileRef().getItem();
    if (!item || item->getItemType() != Item::ItemType::SequenceFile)
        return false;

    const SequenceFile* seqFile = static_cast<const SequenceFile*>(item);
    sead::FixedSafeString<256> name(sound.getNameOrNull());
    if (name.isEmpty())
        name = "Sequence";

    u32 startOffset = sound.getSequenceSoundInfo().getStartOffset();
    return exportSeqToMidi(path, *seqFile, name.cstr(), startOffset);
}

bool exportSeqSoundSetToMidiDir(const sead::SafeString& dirPath, const SoundSet& soundSet)
{
    if (soundSet.getIsEmpty() || soundSet.getSoundSetType() != SoundSet::SoundSetType::Seq)
        return false;

    u32 startId = soundSet.getStartId();
    u32 endId   = soundSet.getEndId();

    // Access the BFSAR sound list
    extern Bfsar sBfsar;
    auto& soundList = sBfsar.getSoundList();

    bool anyOk = false;
    for (auto it = soundList.begin(); it != soundList.end(); ++it)
    {
        Sound* sound = static_cast<Sound*>(*it);
        u32    id    = sound->getId();
        if (id < startId || id > endId)
            continue;
        if (sound->getSoundType() != Sound::SoundType::Seq)
            continue;

        sead::FixedSafeString<256> name(sound->getNameOrNull());
        if (name.isEmpty())
            name = "Sequence";

        sead::FormatFixedSafeString<512> midiPath("%s/%s.midi", dirPath.cstr(), name.cstr());
        if (exportSeqToMidi(midiPath, *sound))
            anyOk = true;
    }

    return anyOk;
}
