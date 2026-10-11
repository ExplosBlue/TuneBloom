#pragma once

#include <bfsar/files/InnerFile.h>
#include <bfsar/model/Sound.h>
#include <bfsar/model/StateHash.h>

#include <filedevice/seadFileDevice.h>
#include <prim/seadEndian.h>

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

    static u32 GetFileVersion(const Sound::StreamSoundInfo &soundInfo, u32 archiveVersion, ArchiveFormat format);

    static bool WriteBfstmFile(sead::FileHandle &handle, const Sound::StreamSoundInfo &soundInfo, u32 archiveVersion, sead::Endian::Types endian, ArchiveFormat format = ArchiveFormat::BFSAR);
    static u64 ComputeContentSignature(const Sound::StreamSoundInfo &soundInfo, u32 archiveVersion, sead::Endian::Types endian, ArchiveFormat format);
    static bool IsComplete(const void *data, u32 size);
};

bool ReadStreamWaves(Sound *sound, const void *strmFile, u32 strmFileSize, const Sound *srcSound);
void MixWaveChannelData(StateHash &hash, const WaveFile &wave);

class StreamLoopInfoBatch
{
public:
    StreamLoopInfoBatch();
    ~StreamLoopInfoBatch();

    static void Flush();
};
