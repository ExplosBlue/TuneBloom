#pragma once

#include <basis/seadTypes.h>
#include <basis/seadAssert.h>
#include <stream/seadStream.h>

#include <unordered_map>
#include <vector>

class WaveFile;

class WaveIdTableWriter
{
public:
    WaveIdTableWriter(u32 maskedWaveArchiveId, const std::unordered_map<const WaveFile*, u32>* waveArchiveIndexes)
        : mMaskedWaveArchiveId(maskedWaveArchiveId)
        , mWaveArchiveIndexes(waveArchiveIndexes)
    {
    }

    void add(const WaveFile* waveFile)
    {
        if (mTableIndexes.contains(waveFile))
            return;

        SEAD_ASSERT(mWaveArchiveIndexes);

        const auto it = mWaveArchiveIndexes->find(waveFile);
        SEAD_ASSERT(it != mWaveArchiveIndexes->end());

        mTableIndexes[waveFile] = static_cast<u32>(mWaveIndexes.size());
        mWaveIndexes.push_back(it->second);
    }

    u32 getTableIndex(const WaveFile* waveFile) const
    {
        const auto it = mTableIndexes.find(waveFile);
        SEAD_ASSERT(it != mTableIndexes.end());

        return it->second;
    }

    u32 getCount() const
    {
        return static_cast<u32>(mWaveIndexes.size());
    }

    void write(sead::WriteStream* stream) const
    {
        stream->writeU32(getCount());

        for (u32 waveIndex : mWaveIndexes)
        {
            stream->writeU32(mMaskedWaveArchiveId);
            stream->writeU32(waveIndex);
        }
    }

private:
    u32 mMaskedWaveArchiveId;
    const std::unordered_map<const WaveFile*, u32>* mWaveArchiveIndexes;
    std::unordered_map<const WaveFile*, u32> mTableIndexes;
    std::vector<u32> mWaveIndexes;
};
