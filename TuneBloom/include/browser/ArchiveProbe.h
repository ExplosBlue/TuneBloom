#pragma once

#include <basis/seadTypes.h>

#include <string>
#include <vector>

enum class ArchiveSoundType : u8
{
    Stream,
    Wave,
    Sequence,
    Unknown,

    Count
};

struct ArchiveSound
{
    std::string name;
    ArchiveSoundType type = ArchiveSoundType::Unknown;
};

struct ArchiveContents
{
    u32 soundCount = 0;
    u32 soundTypeCounts[static_cast<s32>(ArchiveSoundType::Count)] = {};
    u32 soundSetCount = 0;
    u32 bankCount = 0;
    u32 waveArchiveCount = 0;
    u32 groupCount = 0;
    u32 playerCount = 0;
    std::vector<ArchiveSound> sounds;
};

u64 GetArchiveHeadSize(const void* data, u64 size);
bool ProbeArchiveContents(const void* data, u64 size, ArchiveContents* out);
