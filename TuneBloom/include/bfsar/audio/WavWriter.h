#pragma once

#include <bfsar/files/StreamRegion.h>

#include <vector>

struct WavLoop
{
    bool enabled = false;
    u32 start = 0;
    u32 end = 0;
};

struct Pcm16Wav
{
    u32 sampleRate = 0;
    std::vector<std::vector<s16>> channels;
    WavLoop loop;
    std::vector<StreamRegion> regions;
};

void WriteWavLoop(sead::WriteStream* stream, const WavLoop& loop);
u32 CalcPcm16WavSize(u64 sampleCount, u32 channelCount, bool hasLoop);
bool WritePcm16Wav(const sead::SafeString& path, const Pcm16Wav& wav);
