#pragma once

#include <bfsar/audio/DecodedPcm.h>
#include <core/BackgroundJob.h>

#include <memory>
#include <string>
#include <vector>

class WaveFile;

class FilePreview
{
public:
    void play(const std::string& path, const std::string& name);
    void stop();
    void update();
    bool isPlaying(const std::string& path) const;

private:
    struct DecodedFile
    {
        std::string path;
        std::string name;
        std::vector<u8> bytes;
        DecodedPcm pcm;
        bool isWaveFile = false;
        bool isDecoded = false;
    };

    void startDecode();
    void startPlayback(const DecodedFile& decoded);
    void releaseWave();

    BackgroundJob mJob;
    std::unique_ptr<DecodedFile> mDecoding;
    std::string mRequestedPath;
    std::string mRequestedName;
    WaveFile* mWave = nullptr;
    std::string mWavePath;
};
