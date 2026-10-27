#pragma once

#include <bfsar/audio/DecodedPcm.h>
#include <core/BackgroundJob.h>

#include <memory>
#include <string>
#include <vector>

class WaveFile;

struct DecodedAudioFile
{
    std::string path;
    std::string name;
    std::vector<u8> bytes;
    DecodedPcm pcm;
    bool isWaveFile = false;
    bool isDecoded = false;
};

void DecodeAudioFileAt(DecodedAudioFile* file);
WaveFile* BuildDecodedWave(const DecodedAudioFile& file);

class FilePreview
{
public:
    void play(const std::string& path, const std::string& name);
    void stop();
    void update();
    bool isPlaying(const std::string& path) const;

private:
    void startDecode();
    void startPlayback(const DecodedAudioFile& decoded);
    void releaseWave();

    BackgroundJob mJob;
    std::unique_ptr<DecodedAudioFile> mDecoding;
    std::string mRequestedPath;
    std::string mRequestedName;
    WaveFile* mWave = nullptr;
    std::string mWavePath;
};
