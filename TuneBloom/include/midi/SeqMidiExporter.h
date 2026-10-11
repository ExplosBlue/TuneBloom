#pragma once

#include <basis/seadTypes.h>
#include <prim/seadSafeString.h>

#include <utility>
#include <vector>

class Sound;
class SequenceFile;

inline constexpr u32 cSeqMidiTrackNum = 16;
inline constexpr u8  cMidiDrumChannel = 9;

using SeqProgram = std::pair<u16, u16>;

struct SeqMidiPart
{
    u8         trackNo;
    SeqProgram program;
    u32        noteCount;
};

struct SeqMidiContents
{
    u16                      trackFlags = 0;
    std::vector<SeqMidiPart> parts;
};

struct SeqMidiFilter
{
    u16                     trackFlags = 0xFFFF;
    std::vector<SeqProgram> excludedPrograms;

    bool isTrackEnabled(u32 trackNo) const;
    bool isEnabled(u32 trackNo, const SeqProgram& program) const;
};

bool exportSeqToMidi(const sead::SafeString& path, const Sound& sound, const SeqMidiFilter* filter = nullptr);
bool exportSeqToMidi(const sead::SafeString& path, const SequenceFile& seqFile, const char* trackName, u32 startOffset = 0, const SeqMidiFilter* filter = nullptr);

u8 getMidiBank(u32 bankIndex, u32 programNo);
u8 getMidiProgram(u32 programNo);
u8 getSeqMidiChannel(u32 trackNo, u16 usedTrackFlags);

SeqMidiContents collectSeqMidiContents(const SequenceFile& seqFile, u32 startOffset = 0);
SeqMidiContents collectSeqMidiContents(const Sound& sound);
