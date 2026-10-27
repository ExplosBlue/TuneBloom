#pragma once

#include <bfsar/files/WaveFile.h>

void NameStreamTrackWave(WaveFile* wave, const sead::SafeString& streamName, u32 trackNo, u32 trackCount);

class StreamWaveOwner
{
public:
    virtual ~StreamWaveOwner() = default;

    virtual Item::List& getStreamWaveList() = 0;
    virtual void prepareStreamWave(WaveFile* wave) = 0;

    virtual void nameStreamWave(WaveFile* wave, const sead::SafeString& streamName, u32 trackNo, u32 trackCount)
    {
        NameStreamTrackWave(wave, streamName, trackNo, trackCount);
    }

    void addStreamWave(WaveFile* wave)
    {
        Item::List& list = getStreamWaveList();

        wave->setId(list.size());
        list.pushBack(wave);
    }
};
