#pragma once

#include <bfsar/InnerFile.h>
#include <bfsar/Sound.h>

#include <filedevice/seadFileDevice.h>
#include <prim/seadEndian.h>

class BfstmFile
{
public:
    static bool IsTrackInfoAvailable(u32 version, ArchiveFormat format)
    {
        if (format == ArchiveFormat::BCSAR)
            return true;                        // CSTM always has track info

        return version <= 0x00020000;           // FSTM
    }

    static bool IsOriginalLoopAvailable(u32 version, ArchiveFormat format)
    {
        if (format == ArchiveFormat::BCSAR)
            return version >= makeVersion(2, 3, 0);

        return version >= 0x00040000;           // FSTM
    }

    static bool WriteBfstmFile(sead::FileHandle& handle, const Sound::StreamSoundInfo& soundInfo, u32 version, sead::Endian::Types endian, ArchiveFormat format = ArchiveFormat::BFSAR);
};
