#pragma once

#include <basis/seadAssert.h>
#include <basis/seadTypes.h>
#include <prim/seadEndian.h>
#include <prim/seadSafeString.h>

#include <cctype>
#include <cstring>

enum class ArchiveFormat
{
    BFSAR,
    BCSAR
};

enum class ArchivePlatform
{
    CAFE,
    CTR,
    NX
};

enum class InnerFileKind
{
    SoundArchive,
    Wave,
    Stream,
    Sequence,
    Bank,
    WaveArchive,
    Group,
    WaveSoundData,
    StreamPrefetch
};

struct ArchiveFormatInfo
{
    ArchiveFormat format;
    ArchivePlatform platform;
    sead::Endian::Types endian;
    const u32 defaultVersion;
    const char* extension;
    const char* fmtName;
    const char* label;
    const char* systemName;
};

inline constexpr s32 cFormatTableSize = 3;

extern const ArchiveFormatInfo cFormatTable[cFormatTableSize];

inline const ArchiveFormatInfo *getFormatInfo(ArchiveFormat format, ArchivePlatform platform)
{
    for (const auto &info : cFormatTable)
    {
        if (info.format != format)
            continue;

        if (info.platform == platform)
            return &info;
    }

    return nullptr;
}

struct InnerFileFormatInfo
{
    ArchiveFormat format;
    InnerFileKind kind;
    const char* magic;
    const char* extension;
    const char* displayName;
};

inline constexpr InnerFileFormatInfo cInnerFileFormatTable[] = {
    { ArchiveFormat::BFSAR, InnerFileKind::SoundArchive,   "FSAR", "bfsar", "BFSAR" },
    { ArchiveFormat::BFSAR, InnerFileKind::Wave,           "FWAV", "bfwav", "BFWAV" },
    { ArchiveFormat::BFSAR, InnerFileKind::Stream,         "FSTM", "bfstm", "BFSTM" },
    { ArchiveFormat::BFSAR, InnerFileKind::Sequence,       "FSEQ", "bfseq", "BFSEQ" },
    { ArchiveFormat::BFSAR, InnerFileKind::Bank,           "FBNK", "bfbnk", "BFBNK" },
    { ArchiveFormat::BFSAR, InnerFileKind::WaveArchive,    "FWAR", "bfwar", "BFWAR" },
    { ArchiveFormat::BFSAR, InnerFileKind::Group,          "FGRP", "bfgrp", "BFGRP" },
    { ArchiveFormat::BFSAR, InnerFileKind::WaveSoundData,  "FWSD", "bfwsd", "BFWSD" },
    { ArchiveFormat::BFSAR, InnerFileKind::StreamPrefetch, "FSTP", "bfstp", "BFSTP" },

    { ArchiveFormat::BCSAR, InnerFileKind::SoundArchive,   "CSAR", "bcsar", "BCSAR" },
    { ArchiveFormat::BCSAR, InnerFileKind::Wave,           "CWAV", "bcwav", "BCWAV" },
    { ArchiveFormat::BCSAR, InnerFileKind::Stream,         "CSTM", "bcstm", "BCSTM" },
    { ArchiveFormat::BCSAR, InnerFileKind::Sequence,       "CSEQ", "bcseq", "BCSEQ" },
    { ArchiveFormat::BCSAR, InnerFileKind::Bank,           "CBNK", "bcbnk", "BCBNK" },
    { ArchiveFormat::BCSAR, InnerFileKind::WaveArchive,    "CWAR", "bcwar", "BCWAR" },
    { ArchiveFormat::BCSAR, InnerFileKind::Group,          "CGRP", "bcgrp", "BCGRP" },
    { ArchiveFormat::BCSAR, InnerFileKind::WaveSoundData,  "CWSD", "bcwsd", "BCWSD" },
    { ArchiveFormat::BCSAR, InnerFileKind::StreamPrefetch, "CSTP", "bcstp", "BCSTP" },
};

extern const InnerFileFormatInfo cUnknownInnerFileFormat;

inline const InnerFileFormatInfo& GetInnerFileFormat(ArchiveFormat format, InnerFileKind kind)
{
    for (const auto& info : cInnerFileFormatTable)
    {
        if (info.format == format && info.kind == kind)
            return info;
    }

    SEAD_ASSERT_MSG(false, "No inner file format entry for archive format %d, kind %d", (s32)format, (s32)kind);
    return cUnknownInnerFileFormat;
}

inline const char* GetInnerFileMagic(ArchiveFormat format, InnerFileKind kind)
{
    return GetInnerFileFormat(format, kind).magic;
}

inline const char* GetInnerFileExtension(ArchiveFormat format, InnerFileKind kind)
{
    return GetInnerFileFormat(format, kind).extension;
}

inline const char* GetInnerFileDisplayName(ArchiveFormat format, InnerFileKind kind)
{
    return GetInnerFileFormat(format, kind).displayName;
}

inline u32 GetArchiveVersionByteCount(ArchiveFormat format)
{
    return format == ArchiveFormat::BCSAR ? 4 : 3;
}

inline void FormatInnerFileVersion(sead::BufferedSafeString* out, u32 version, ArchiveFormat format)
{
    if (GetArchiveVersionByteCount(format) == 4)
        out->format("%u.%u.%u.%u", (version >> 24) & 0xFF, (version >> 16) & 0xFF, (version >> 8) & 0xFF, version & 0xFF);
    else
        out->format("%u.%u.%u", (version >> 16) & 0xFF, (version >> 8) & 0xFF, version & 0xFF);
}

inline u32 GetInnerFileDataAlignment(ArchiveFormat format, sead::Endian::Types endian)
{
    return format == ArchiveFormat::BFSAR && endian == sead::Endian::eLittle ? 0x40 : 0x20;
}

constexpr s32 cInnerFileMagicLength = 4;

inline const InnerFileFormatInfo* FindInnerFileFormat(const void* signature, InnerFileKind kind)
{
    if (!signature)
        return nullptr;

    for (const auto& info : cInnerFileFormatTable)
    {
        if (info.kind != kind)
            continue;

        if (memcmp(signature, info.magic, cInnerFileMagicLength) == 0)
            return &info;
    }

    return nullptr;
}

inline const InnerFileFormatInfo* FindInnerFileFormat(const void* signature)
{
    if (!signature)
        return nullptr;

    for (const auto& info : cInnerFileFormatTable)
    {
        if (memcmp(signature, info.magic, cInnerFileMagicLength) == 0)
            return &info;
    }

    return nullptr;
}

inline bool MatchesInnerFileKind(const void* signature, InnerFileKind kind)
{
    return FindInnerFileFormat(signature, kind) != nullptr;
}

inline bool EqualsIgnoreCase(const char* left, const char* right)
{
    while (*left && *right)
    {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right))
            return false;

        left++;
        right++;
    }

    return *left == *right;
}

inline const InnerFileFormatInfo* FindInnerFileFormatByExtension(const char* extension, InnerFileKind kind)
{
    if (!extension)
        return nullptr;

    if (*extension == '.')
        extension++;

    for (const auto& info : cInnerFileFormatTable)
    {
        if (info.kind != kind)
            continue;

        if (EqualsIgnoreCase(extension, info.extension))
            return &info;
    }

    return nullptr;
}

inline bool MatchesInnerFileExtension(const char* path, InnerFileKind kind)
{
    if (!path)
        return false;

    const char* extension = strrchr(path, '.');

    return extension && FindInnerFileFormatByExtension(extension, kind) != nullptr;
}

enum class ResourceFormat
{
    Bars,
    Bwav
};

struct ResourceFormatInfo
{
    ResourceFormat format;
    const char* extension;
    const char* displayName;
};

inline constexpr ResourceFormatInfo cResourceFormatTable[] = {
    { ResourceFormat::Bars, "bars", "BARS" },
    { ResourceFormat::Bwav, "bwav", "BWAV" },
};

inline constexpr const char* cZstdExtension = "zs";
inline constexpr const char* cCmpbinExtension = "cmpbin";
inline constexpr const char* cBeaExtension = "bea";
inline constexpr const char* cWavExtension = "wav";
inline constexpr const char* cOpusExtension = "opus";
inline constexpr const char* cResourceStreamFolder = "Stream";
inline constexpr const char* cResourceNamedStreamSuffix = "_Stream";
inline constexpr const char* cResourceNamedStreamPrefix = "Stream_";

inline const ResourceFormatInfo& GetResourceFormatInfo(ResourceFormat format)
{
    return cResourceFormatTable[static_cast<s32>(format)];
}

inline bool HasFileExtension(const char* path, size_t pathLength, const char* extension)
{
    const size_t extensionLength = strlen(extension);

    if (pathLength <= extensionLength || path[pathLength - extensionLength - 1] != '.')
        return false;

    for (size_t i = 0; i < extensionLength; i++)
    {
        if (tolower((unsigned char)path[pathLength - extensionLength + i]) != tolower((unsigned char)extension[i]))
            return false;
    }

    return true;
}

inline bool IsZstdPath(const char* path)
{
    return path && HasFileExtension(path, strlen(path), cZstdExtension);
}
