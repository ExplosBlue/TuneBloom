#include <bfsar/model/ArchiveFormat.h>

#include <ui/Messages.h>

constexpr ArchiveFormatInfo cFormatTable[cFormatTableSize] = {
    {
        ArchiveFormat::BFSAR,
        ArchivePlatform::CAFE,
        sead::Endian::eBig,
        0x00020000,
        "bfsar",
        "BFSAR",
        messages::format::cLabelCafe,
        messages::format::cSystemWiiU
    },
    {
        ArchiveFormat::BCSAR,
        ArchivePlatform::CTR,
        sead::Endian::eLittle,
        0x02000000,
        "bcsar",
        "BCSAR",
        messages::format::cLabelCtr,
        messages::format::cSystem3ds
    },
    {
        ArchiveFormat::BFSAR,
        ArchivePlatform::NX,
        sead::Endian::eLittle,
        0x00020400,
        "bfsar",
        "BFSAR",
        messages::format::cLabelCafe,
        messages::format::cSystemSwitch
    },
};

static_assert(cFormatTable[cFormatTableSize - 1].extension != nullptr);

constexpr InnerFileFormatInfo cUnknownInnerFileFormat = {
    ArchiveFormat::BFSAR, InnerFileKind::SoundArchive, "????", "bin", messages::common::cUnknown
};
