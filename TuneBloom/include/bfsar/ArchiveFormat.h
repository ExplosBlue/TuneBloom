#pragma once

#include <basis/seadAssert.h>
#include <basis/seadTypes.h>
#include <prim/seadEndian.h>

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
    WaveSoundData
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

inline constexpr ArchiveFormatInfo cFormatTable[] = {
    {
        ArchiveFormat::BFSAR,
        ArchivePlatform::CAFE,
        sead::Endian::eBig,
        0x00020000,
        "bfsar",
        "BFSAR",
        "Cafe Sound Archive",
        "Wii U"
    },
    {
        ArchiveFormat::BCSAR,
        ArchivePlatform::CTR,
        sead::Endian::eLittle,
        0x02000000,
        "bcsar",
        "BCSAR",
        "CTR Sound Archive",
        "3DS"
    },
    {
        ArchiveFormat::BFSAR,
        ArchivePlatform::NX,
        sead::Endian::eLittle,
        0x00020400,
        "bfsar",
        "BFSAR",
        "Cafe Sound Archive",
        "Switch"
    },
};

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
    { ArchiveFormat::BFSAR, InnerFileKind::SoundArchive,  "FSAR", "bfsar", "BFSAR" },
    { ArchiveFormat::BFSAR, InnerFileKind::Wave,          "FWAV", "bfwav", "BFWAV" },
    { ArchiveFormat::BFSAR, InnerFileKind::Stream,        "FSTM", "bfstm", "BFSTM" },
    { ArchiveFormat::BFSAR, InnerFileKind::Sequence,      "FSEQ", "bfseq", "BFSEQ" },
    { ArchiveFormat::BFSAR, InnerFileKind::Bank,          "FBNK", "bfbnk", "BFBNK" },
    { ArchiveFormat::BFSAR, InnerFileKind::WaveArchive,   "FWAR", "bfwar", "BFWAR" },
    { ArchiveFormat::BFSAR, InnerFileKind::Group,         "FGRP", "bfgrp", "BFGRP" },
    { ArchiveFormat::BFSAR, InnerFileKind::WaveSoundData, "FWSD", "bfwsd", "BFWSD" },

    { ArchiveFormat::BCSAR, InnerFileKind::SoundArchive,  "CSAR", "bcsar", "BCSAR" },
    { ArchiveFormat::BCSAR, InnerFileKind::Wave,          "CWAV", "bcwav", "BCWAV" },
    { ArchiveFormat::BCSAR, InnerFileKind::Stream,        "CSTM", "bcstm", "BCSTM" },
    { ArchiveFormat::BCSAR, InnerFileKind::Sequence,      "CSEQ", "bcseq", "BCSEQ" },
    { ArchiveFormat::BCSAR, InnerFileKind::Bank,          "CBNK", "bcbnk", "BCBNK" },
    { ArchiveFormat::BCSAR, InnerFileKind::WaveArchive,   "CWAR", "bcwar", "BCWAR" },
    { ArchiveFormat::BCSAR, InnerFileKind::Group,         "CGRP", "bcgrp", "BCGRP" },
    { ArchiveFormat::BCSAR, InnerFileKind::WaveSoundData, "CWSD", "bcwsd", "BCWSD" },
};

inline constexpr InnerFileFormatInfo cUnknownInnerFileFormat = {
    ArchiveFormat::BFSAR, InnerFileKind::SoundArchive, "????", "bin", "Unknown"
};

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
