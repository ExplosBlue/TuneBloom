#pragma once

#include <bfsar/model/Item.h>

#include <memory>
#include <string>
#include <vector>

class WaveFile;
struct DecodedPcm;

namespace aal
{
struct Bwav;
struct AmtaSourceAudio;
enum class BwavCodec : u16;
}

using AudioAssetFile = std::shared_ptr<const std::vector<u8>>;

inline AudioAssetFile MakeAudioAssetFile(std::vector<u8> bytes)
{
    return std::make_shared<const std::vector<u8>>(std::move(bytes));
}

enum class AudioAssetKind
{
    None,
    Bwav,
    Fwav,
    StreamPrefetch,
    Stream,
    Unknown
};

enum class StreamFileState
{
    NotChecked,
    Missing,
    Found,
    Loaded,
    Damaged,
    Mismatched
};

struct AudioAssetInfo
{
    AudioAssetKind kind = AudioAssetKind::None;
    u32 channelCount = 0;
    u32 sampleRate = 0;
    u32 sampleCount = 0;
    u32 storedSampleCount = 0;
    u32 prefetchSampleCount = 0;
    bool isLoop = false;
    u32 loopStartFrame = 0;
    u32 loopEndFrame = 0;
    bool isPrefetch = false;
    u32 fileSize = 0;
};

struct AudioAssetPcm
{
    u32 sampleRate = 0;
    bool isLoop = false;
    u32 loopStartFrame = 0;
    std::vector<std::vector<s16>> channels;
};

class AudioAsset : public Item
{
public:
    AudioAsset();

    const AudioResource* getResource() const;
    AudioResource* getResource();
    ~AudioAsset() override;

    const Item* validate(sead::BufferedSafeString& error) const override;
    bool isStateTracked() const override;
    u64 computeStateSignature() const override;

    const std::vector<u8>& getMeta() const;
    void setMeta(std::vector<u8> meta);
    u16 getMetaVersion() const;

    const AudioAssetFile& getFile() const;
    const AudioAssetFile& getCompleteFile() const;
    AudioAssetKind getCompleteFileKind() const;
    void setFile(AudioAssetFile file);

    const AudioAssetFile& getStreamFile() const;
    StreamFileState getStreamFileState() const;
    void setStreamFile(AudioAssetFile file, StreamFileState state, bool isModified);
    bool needsStreamFileWrite() const;
    void markStreamFileWritten();

    const AudioAssetInfo& getInfo() const;
    bool isStream() const;
    AudioAssetKind getStreamFileKind() const;
    bool getPlaybackError(sead::BufferedSafeString* outError) const;
    const char* getNoAudioReason() const;

    bool decode(DecodedPcm* out);
    bool decodePcm16(AudioAssetPcm* out);
    bool writeWavFile(const sead::SafeString& path);
    bool isPlaying() const;
    const WaveFile* getPreviewWave();

private:
    void refreshInfo();
    void releasePreviewWave();
    WaveFile* buildPreviewWave();
    const char* findPlaybackError() const;
    bool readsStreamFile() const;

    std::vector<u8> mMeta;
    AudioAssetKind mMetaStreamKind;
    AudioAssetFile mFile;
    AudioAssetFile mStreamFile;
    StreamFileState mStreamFileState;
    std::string mStreamFileName;
    bool mIsStreamFileModified;
    u32 mRevision;
    AudioAssetInfo mInfo;
    WaveFile* mPreviewWave;
};

const char* GetStreamFileStateError(StreamFileState state);
const char* GetAudioAssetKindName(AudioAssetKind kind);
const char* GetAudioAssetKindExtension(AudioAssetKind kind);
AudioAssetKind DetectAudioAssetKind(const std::vector<u8>& file);
bool ReadAudioAssetBwav(const AudioAssetFile& file, aal::Bwav* out);
bool GetAudioAssetBwavCodec(const AudioAsset& asset, aal::BwavCodec* out);
u32 GetAudioAssetOpusBitrate(const AudioAsset& asset);
aal::AmtaSourceAudio GetAudioAssetLength(const AudioAsset& asset);
bool IsAudioAssetLoop(const AudioAsset& asset);
bool IsReadableFwav(const std::vector<u8>& file);
WaveFile* ReadFwavFile(const void* data, size_t size);

inline WaveFile* ReadFwavFile(const std::vector<u8>& file)
{
    return ReadFwavFile(file.data(), file.size());
}
bool DecodeAudioFile(const AudioAssetFile& file, DecodedPcm* out);
