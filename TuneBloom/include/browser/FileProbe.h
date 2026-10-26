#pragma once

#include <basis/seadTypes.h>
#include <browser/ArchiveProbe.h>

#include <filesystem>
#include <vector>

enum class BrowserFileKind
{
    SoundArchive,
    AudioResource,
    Wave,
    Stream,
    StreamPrefetch,

    Count
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
    ArchiveContents contents;
};

bool ProbeFileBytes(const void* data, u64 size, FileProbe* out);
bool ProbeFile(const std::filesystem::path& path, u64 fileSize, FileProbe* out);
bool ReadFileContents(const std::filesystem::path& path, std::vector<u8>* out);
