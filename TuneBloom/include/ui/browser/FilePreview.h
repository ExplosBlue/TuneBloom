#pragma once

#include <bfsar/audio/DecodedPcm.h>
#include <core/BackgroundJob.h>

#include <memory>
#include <string>
#include <vector>

class LooseStreamFile;
class WaveFile;

struct DecodedAudioFile
{
    std::string path;
    std::string name;
    std::vector<u8> bytes;
    DecodedPcm pcm;
    bool isRead = false;
    bool isWaveFile = false;
    bool isStream = false;
    bool isDecoded = false;
};

void DecodeAudioBytes(DecodedAudioFile* file);
WaveFile* BuildDecodedWave(const DecodedAudioFile& file);

class FilePreview
{
public:
    FilePreview();
    ~FilePreview();

    void play(const std::string& path, const std::string& name, bool reportsReadFailure = true);
    void stop();
    void update();
    bool isPlaying(const std::string& path) const;

private:
    void startDecode();
    void startPlayback(DecodedAudioFile& decoded);
    bool startStream(const DecodedAudioFile& decoded);
    void startWave(const DecodedAudioFile& decoded);
    void releaseWave();

    BackgroundJob mJob;
    std::unique_ptr<DecodedAudioFile> mDecoding;
    std::string mRequestedPath;
    std::string mRequestedName;
    WaveFile* mWave = nullptr;
    std::unique_ptr<LooseStreamFile> mStream;
    std::string mWavePath;
    bool mReportsReadFailure = true;
};
