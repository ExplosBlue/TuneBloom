#pragma once

#include <bfsar/files/StreamWaveOwner.h>
#include <bfsar/model/StreamSource.h>
#include <core/SaveResult.h>

#include <vector>

class LooseStreamFile : public StreamWaveOwner
{
public:
    static bool IsStreamFile(const void* data, u32 size);

    LooseStreamFile() = default;
    ~LooseStreamFile() override = default;

    LooseStreamFile(const LooseStreamFile&) = delete;
    LooseStreamFile& operator=(const LooseStreamFile&) = delete;

    bool open(const void* data, u32 size, const sead::SafeString& path, sead::BufferedSafeString* outError);
    void create(const InnerFileTarget& target, Sound::StreamSoundInfo::StreamType streamType, const sead::SafeString& fileName);
    SaveResult save(sead::BufferedSafeString* outError);
    SaveResult saveAs(const sead::SafeString& path, sead::BufferedSafeString* outError);

    StreamSource getStreamSource();

    Sound::StreamSoundInfo& getStream()
    {
        return mStream;
    }

    const Sound::StreamSoundInfo& getStream() const
    {
        return mStream;
    }

    const InnerFileTarget& getTarget() const
    {
        return mTarget;
    }

    const sead::SafeString& getFilePath() const
    {
        return mFilePath;
    }

    const sead::SafeString& getFileName() const
    {
        return mFileName;
    }

    bool isOpus() const;
    bool isModified() const;
    bool writeTo(const sead::SafeString& path) const;

    bool setLoop(bool isLoop, u32 loopStartFrame, const char** outError);
    bool replaceAudio(const WaveFile::RiffWaveInfo& info, const char** outError);
    bool setAudio(const WaveFile::RiffWaveInfo& info, WaveFile::Encoding encoding, const char** outError);
    bool setAudioChannels(std::vector<std::vector<float>> channels, const WaveFile::RiffWaveInfo& info, WaveFile::Encoding encoding, const char** outError);

    Item::List& getStreamWaveList() override
    {
        return mWaveList;
    }

    void prepareStreamWave(WaveFile* wave) override;
    void nameStreamWave(WaveFile* wave, const sead::SafeString& streamName, u32 trackNo, u32 trackCount) override;

private:
    void setStreamType(Sound::StreamSoundInfo::StreamType streamType);
    bool openOpus(const void* data, u32 size, sead::BufferedSafeString* outError);
    bool openStream(const void* data, u32 size, sead::BufferedSafeString* outError);
    void setChannelCount(u32 channelCount);
    u64 computeSignature() const;

    sead::FixedSafeString<512> mFilePath;
    sead::FixedSafeString<256> mFileName;
    InnerFileTarget mTarget;
    Item::List mWaveList;
    Sound::StreamSoundInfo mStream;
    u64 mSavedSignature = 0;
};
