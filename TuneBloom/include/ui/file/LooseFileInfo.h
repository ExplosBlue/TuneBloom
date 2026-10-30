#pragma once

#include <bfsar/files/InnerFile.h>

#include <prim/seadSafeString.h>

class WaveFile;

struct LooseFileFormat
{
    const char* name = nullptr;
    const char* extension = nullptr;
    sead::FixedSafeString<24> version;
    sead::Endian::Types endian = sead::Endian::eLittle;
};

const char* GetByteOrderText(sead::Endian::Types endian);
LooseFileFormat GetLooseFileFormat(const InnerFileTarget& target, InnerFileKind kind);
void DrawLooseFileRows(const LooseFileFormat& format);
void FormatLooseFileFacts(sead::BufferedSafeString* out, const LooseFileFormat& format);
void AppendFileFact(sead::BufferedSafeString* out, const char* fact);
void AppendWaveFacts(sead::BufferedSafeString* out, const WaveFile& wave);
void AppendRateFact(sead::BufferedSafeString* out, u32 sampleRate);
void AppendTrackFact(sead::BufferedSafeString* out, u32 trackNum);
void AppendChannelFact(sead::BufferedSafeString* out, u32 channelNum);
