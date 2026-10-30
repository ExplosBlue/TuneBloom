#pragma once

#include <bfsar/files/InnerFile.h>
#include <bfsar/files/StreamWaveOwner.h>
#include <bfsar/model/Sound.h>
#include <bfsar/model/StateHash.h>

#include <filedevice/seadFileDevice.h>
#include <prim/seadEndian.h>

namespace nw { namespace snd { namespace internal {

class StreamSoundFileReader;

} } }

class BfstmFile
{
public:
    static bool IsTrackInfoAvailable(u32 version, ArchiveFormat format)
    {
        if (format == ArchiveFormat::BCSAR)
            return version < makeVersion(2, 2, 0); // CSTM

        return version <= 0x00020000; // FSTM
    }

    static bool IsOriginalLoopAvailable(u32 version, ArchiveFormat format)
    {
        if (format == ArchiveFormat::BCSAR)
            return version >= makeVersion(2, 3, 0);

        return version >= 0x00040000; // FSTM
    }

    static bool IsRegionInfoAvailable(u32 version, ArchiveFormat format)
    {
        return format != ArchiveFormat::BCSAR || version >= makeVersion(2, 2, 0);
    }

    static u32 GetFileVersion(const Sound::StreamSoundInfo &soundInfo, const InnerFileTarget &target);

    static bool WriteBfstmFile(sead::FileHandle &handle, const Sound::StreamSoundInfo &soundInfo, const InnerFileTarget &target);
    static u64 ComputeContentSignature(const Sound::StreamSoundInfo &soundInfo, const InnerFileTarget &target);
    static bool IsComplete(const void *data, u32 size);
};

bool ReadStreamFileTracks(nw::snd::internal::StreamSoundFileReader &reader, Sound::StreamSoundInfo &soundInfo, sead::Heap *heap);
void ApplyStreamTrackLayout(Sound::StreamSoundInfo &soundInfo, u32 trackCount, u32 channelCount);
u32 GetDefaultStreamTrackCount(u32 channelCount);
bool StreamWavesCoverLayout(const Sound::StreamSoundInfo &soundInfo);

bool ReadStreamWaves(StreamWaveOwner &owner, Sound::StreamSoundInfo &soundInfo, const sead::SafeString &streamName, const void *strmFile, u32 strmFileSize);
bool ReadStreamWaves(Bfsar &archive, Sound *sound, const void *strmFile, u32 strmFileSize, const Sound *srcSound);
void MixWaveChannelData(StateHash &hash, const WaveFile &wave);

class StreamLoopInfoBatch
{
public:
    StreamLoopInfoBatch();
    ~StreamLoopInfoBatch();

    static void Flush();
};
