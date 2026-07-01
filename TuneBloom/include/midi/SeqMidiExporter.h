#pragma once

#include <basis/seadTypes.h>
#include <prim/seadSafeString.h>

class Sound;
class SoundSet;
class SequenceFile;

bool exportSeqToMidi(const sead::SafeString& path, const Sound& sound);
bool exportSeqToMidi(const sead::SafeString& path, const SequenceFile& seqFile, const char* trackName, u32 startOffset = 0);
bool exportSeqSoundSetToMidiDir(const sead::SafeString& dirPath, const SoundSet& soundSet);
