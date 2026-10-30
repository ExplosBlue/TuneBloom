#pragma once

#include <bfsar/model/Sound.h>

#include <basis/seadTypes.h>
#include <prim/seadSafeString.h>

#include <vector>

namespace sead
{
    class FileHandle;
}

class Bfsar;
class StreamWaveOwner;

namespace opusstream
{

    inline constexpr u32 cDefaultChannelBitrate = 128000;

    struct Info
    {
        u8 channelCount = 0;
        u32 sampleRate = 0;
        u32 preSkip = 0;
        u32 sampleCount = 0;
    };

    bool IsOpusStream(const void *data, u32 size);
    bool ReadInfoHeader(const void *data, u32 size, Info &outInfo);
    u32 ReadChannelBitrate(const void *data, u32 size);
    u32 GetEncodeChannelBitrate(const Sound::StreamSoundInfo &stream);
    bool DecodeToPcm16(const void *data, u32 size, Info &outInfo, std::vector<s16 *> &outChannelData, const char **outError);
    bool AttachStreamWaves(Bfsar &archive, Sound *sound, const sead::SafeString *filePathOverride = nullptr);
    bool ReadStreamFile(StreamWaveOwner &owner, Sound::StreamSoundInfo &stream, const sead::SafeString &streamName, const void *data, u32 size, const char **outError);
    bool WriteStreamFile(sead::FileHandle &handle, const Sound::StreamSoundInfo &streamInfo, Item *popupItem);
    u64 ComputeContentSignature(const Sound::StreamSoundInfo &stream);

}
