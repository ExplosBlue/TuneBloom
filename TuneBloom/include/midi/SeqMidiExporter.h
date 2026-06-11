#pragma once

#include <prim/seadSafeString.h>

class Sound;
class SoundSet;
class SequenceFile;

bool exportSeqToMidi(const sead::SafeString& path, const Sound& sound);
bool exportSeqToMidi(const sead::SafeString& path, const SequenceFile& seqFile, const char* trackName);
bool exportSeqSoundSetToMidiDir(const sead::SafeString& dirPath, const SoundSet& soundSet);
