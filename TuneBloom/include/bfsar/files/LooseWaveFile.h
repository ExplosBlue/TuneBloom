#pragma once

#include <bfsar/files/WaveFile.h>
#include <core/SaveResult.h>

class LooseWaveFile
{
public:
    static bool IsWaveFile(const void* data, u32 size);

    LooseWaveFile() = default;
    ~LooseWaveFile() = default;

    LooseWaveFile(const LooseWaveFile&) = delete;
    LooseWaveFile& operator=(const LooseWaveFile&) = delete;

    bool open(const void* data, u32 size, const sead::SafeString& path, sead::BufferedSafeString* outError);
    void create(const InnerFileTarget& target, const sead::SafeString& fileName);
    SaveResult save(sead::BufferedSafeString* outError);
    SaveResult saveAs(const sead::SafeString& path, sead::BufferedSafeString* outError);

    WaveFile* getWave()
    {
        return mWave;
    }

    const WaveFile* getWave() const
    {
        return mWave;
    }

    Item::List& getWaveList()
    {
        return mWaveList;
    }

    InnerFileTarget getTarget() const
    {
        return mWave->getTarget();
    }

    const sead::SafeString& getFilePath() const
    {
        return mFilePath;
    }

    const sead::SafeString& getFileName() const
    {
        return mFileName;
    }

    bool isModified() const;
    bool writeTo(const sead::SafeString& path) const;

private:
    void setPath(const sead::SafeString& path);
    void adoptWave(WaveFile* wave);

    sead::FixedSafeString<512> mFilePath;
    sead::FixedSafeString<256> mFileName;
    Item::List mWaveList;
    WaveFile* mWave = nullptr;
    u64 mSavedSignature = 0;
};
