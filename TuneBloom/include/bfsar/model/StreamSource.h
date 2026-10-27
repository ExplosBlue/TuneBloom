#pragma once

#include <bfsar/files/InnerFile.h>
#include <bfsar/model/Sound.h>

class StreamWaveOwner;

struct StreamSource
{
    Sound::StreamSoundInfo* stream = nullptr;
    Sound* sound = nullptr;
    StreamWaveOwner* waveOwner = nullptr;
    InnerFileTarget target;
    const char* fileName = nullptr;
    bool enableSend = false;
    bool enableFilter = false;
    bool hasTrackMix = true;

    bool isValid() const
    {
        return stream != nullptr;
    }

    sead::FixedSafeString<256> getDisplayName() const
    {
        if (sound)
            return sound->getFormattedName();

        sead::FixedSafeString<256> name;
        name.copy(fileName ? fileName : "");

        return name;
    }

    bool hasRegionSupport() const
    {
        return target.format == ArchiveFormat::BFSAR && (!stream || stream->getStreamType() == Sound::StreamSoundInfo::StreamType::NwStreamBinary);
    }
};
