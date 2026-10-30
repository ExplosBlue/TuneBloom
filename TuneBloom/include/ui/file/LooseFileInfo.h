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

struct FileFacts
{
    LooseFileFormat format;
    const char* kindName = nullptr;
    sead::FixedSafeString<32> encoding;
    u32 bitrate = 0;
    u32 sampleRate = 0;
    u32 sampleCount = 0;
    u32 channelNum = 0;
    u32 trackNum = 0;
    bool isLoop = false;
    u32 loopStartFrame = 0;
    u32 loopEndFrame = 0;
};

const char* GetByteOrderText(sead::Endian::Types endian);
LooseFileFormat GetLooseFileFormat(const InnerFileTarget& target, InnerFileKind kind);
void DrawLooseFileRows(const LooseFileFormat& format);
void SetWaveFacts(FileFacts* facts, const WaveFile& wave);
u32 CalcWaveBitrate(const WaveFile& wave, u32 channelNum);
u32 CalcAdpcmBitrate(u32 sampleRate, u32 channelNum);
u32 CalcPcmBitrate(u32 sampleRate, u32 channelNum, u32 bitsPerSample);
void FormatFileFactsLine(sead::BufferedSafeString* out, const FileFacts& facts);
