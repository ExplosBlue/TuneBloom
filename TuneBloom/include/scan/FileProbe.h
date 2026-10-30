#pragma once

#include <basis/seadTypes.h>
#include <scan/ArchiveProbe.h>

#include <filesystem>
#include <vector>

enum class BrowserFileKind
{
    SoundArchive,
    AudioResource,
    ResourceList,
    Wave,
    Stream,
    StreamPrefetch,
    Wav,

    Count
};

enum class ProbeDepth
{
    Kind,
    Contents
};

struct FileProbe
{
    BrowserFileKind kind = BrowserFileKind::SoundArchive;
    char formatName[16] = {};
    char versionText[16] = {};
    bool isCompressed = false;
    bool hasItemCount = false;
    u32 itemCount = 0;
    u32 channelCount = 0;
    u32 sampleRate = 0;
    u32 sampleCount = 0;
    bool isLoop = false;
    bool isPrefetch = false;
    bool isPack = false;
    ArchiveContents contents;
};

bool ProbeFileBytes(const void* data, u64 size, FileProbe* out);
bool ProbeFile(const std::filesystem::path& path, u64 fileSize, ProbeDepth depth, FileProbe* out);
bool ProbeFileKind(const std::filesystem::path& path, BrowserFileKind* outKind);
bool IsPlayerFile(const std::filesystem::path& path);
bool ReadFileContents(const std::filesystem::path& path, std::vector<u8>* out);
