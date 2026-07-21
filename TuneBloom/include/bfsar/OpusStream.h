#pragma once

#include <basis/seadTypes.h>

#include <vector>

namespace sead
{
    class FileHandle;
}

class Sound;

namespace opusstream
{

    struct Info
    {
        u8 channelCount = 0;
        u32 sampleRate = 0;
        u32 preSkip = 0;
        u32 sampleCount = 0;
    };

    bool IsOpusStream(const void *data, u32 size);
    bool DecodeToPcm16(const void *data, u32 size, Info &outInfo, std::vector<s16 *> &outChannelData, const char **outError);
    bool AttachStreamWaves(Sound *sound);
    bool WriteStreamFile(sead::FileHandle &handle, const Sound *sound);
    u64 ComputeContentSignature(const Sound &sound);

}