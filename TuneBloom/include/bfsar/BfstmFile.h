#pragma once

#include <bfsar/Sound.h>

#include <filedevice/seadFileDevice.h>
#include <prim/seadEndian.h>

class BfstmFile
{
public:
    static bool IsTrackInfoAvailable(u32 version)
    {
        return version <= 0x00020000;
    }

    static bool IsOriginalLoopAvailable(u32 version)
    {
        return version >= 0x00040000;
    }

    static bool WriteBfstmFile(sead::FileHandle& handle, const Sound::StreamSoundInfo& soundInfo, u32 version, sead::Endian::Types endian);
};
