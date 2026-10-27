#pragma once

#include <bfsar/files/InnerFile.h>

#include <prim/seadSafeString.h>

class WaveFile;

const char* GetByteOrderText(sead::Endian::Types endian);
void DrawLooseFileRows(const InnerFileTarget& target, InnerFileKind kind);
void FormatLooseFileFacts(sead::BufferedSafeString* out, const InnerFileTarget& target, InnerFileKind kind);
void AppendFileFact(sead::BufferedSafeString* out, const char* fact);
void AppendWaveFacts(sead::BufferedSafeString* out, const WaveFile& wave);
void AppendRateFact(sead::BufferedSafeString* out, u32 sampleRate);
void AppendTrackFact(sead::BufferedSafeString* out, u32 trackNum);
void AppendChannelFact(sead::BufferedSafeString* out, u32 channelNum);
