#include <scan/ArchiveProbe.h>

#include <bfsar/model/ArchiveFormat.h>
#include <core/ByteView.h>
#include <core/NwFile.h>
#include <snd/snd_ElementType.h>

#include <algorithm>
#include <cstring>

static constexpr u64 cStringEntrySize   = 0x0C;
static constexpr u64 cStringEntryLength = 0x08;
static constexpr u32 cItemCountMax      = 0x00FFFFFF;
static constexpr u32 cStringIdFlag      = 0x01;
static constexpr u32 cV3MajorVersion    = 3;

static constexpr u64 cSoundDetailOffset   = 0x0C;
static constexpr u64 cSoundOptionOffset   = 0x14;
static constexpr u64 cSoundDetailOffsetV3 = 0x2C;
static constexpr u64 cSoundOptionOffsetV3 = 0x34;

namespace
{

enum class InfoTable
{
    Sound,
    SoundSet,
    Bank,
    WaveArchive,
    Group,
    Player
};

struct ArchiveLayout
{
    nwfile::Block strings;
    nwfile::Block info;
    bool isV3 = false;
};

struct StringTable
{
    u64 offset = 0;
    u32 count = 0;
};

}

static bool FindArchiveBlock(const ByteView& view, u16 type, nwfile::Block* out)
{
    nwfile::Block block;

    if (!nwfile::FindBlock(view, type, &block) || block.size == nwfile::cNullOffset)
        return false;

    *out = block;
    return true;
}

static bool ReadLayout(const void* data, u64 size, ArchiveLayout* out)
{
    if (size < nwfile::cHeaderBlockRefs)
        return false;

    const InnerFileFormatInfo* format = FindInnerFileFormat(data, InnerFileKind::SoundArchive);

    if (!format || !ByteView::IsByteOrderMark(data, size, nwfile::cHeaderByteOrder))
        return false;

    const ByteView view(data, size, ByteView::IsBigEndianMark(data, size, nwfile::cHeaderByteOrder));

    if (!FindArchiveBlock(view, nw::snd::internal::ElementType_SoundArchiveFile_InfoBlock, &out->info))
        return false;

    FindArchiveBlock(view, nw::snd::internal::ElementType_SoundArchiveFile_StringBlock, &out->strings);

    const u32 version = view.readU32(nwfile::cHeaderVersion);
    const u32 majorShift = nwfile::GetVersionMajorShift(version);

    out->isV3 = format->format == ArchiveFormat::BFSAR && ((version >> majorShift) & 0xFF) >= cV3MajorVersion;

    return true;
}

u64 GetArchiveHeadSize(const void* data, u64 size)
{
    ArchiveLayout layout;

    if (!ReadLayout(data, size, &layout))
        return 0;

    return std::max(layout.info.offset + layout.info.size, layout.strings.offset + layout.strings.size);
}

static bool ReadTable(const ByteView& view, u64 base, u64 reference, u64* outTable, u32* outCount)
{
    if (!view.has(reference, nwfile::cReferenceSize))
        return false;

    const u64 table = base + view.readU32(reference + nwfile::cReferenceOffset);

    if (!view.has(table, sizeof(u32)))
        return false;

    const u32 count = view.readU32(table);

    if (count > cItemCountMax || !view.has(table + sizeof(u32), static_cast<u64>(count) * nwfile::cReferenceSize))
        return false;

    *outTable = table;
    *outCount = count;

    return true;
}

static u32 CountInfoTable(const ByteView& view, u64 infoBody, InfoTable table)
{
    u64 offset = 0;
    u32 count = 0;

    ReadTable(view, infoBody, infoBody + static_cast<u64>(table) * nwfile::cReferenceSize, &offset, &count);

    return count;
}

static StringTable ReadStringTable(const ByteView& view, const nwfile::Block& strings)
{
    StringTable table;
    const u64 body = strings.offset + nwfile::cBlockHeaderSize;

    if (strings.size == 0 || !view.has(body, nwfile::cReferenceSize))
        return table;

    const u64 offset = body + view.readU32(body + nwfile::cReferenceOffset);

    if (!view.has(offset, sizeof(u32)))
        return table;

    table.offset = offset;
    table.count = view.readU32(offset);

    return table;
}

static const char* FindString(const ByteView& view, const StringTable& table, u32 stringId)
{
    if (stringId >= table.count)
        return nullptr;

    const u64 entry = table.offset + sizeof(u32) + static_cast<u64>(stringId) * cStringEntrySize;

    if (!view.has(entry, cStringEntrySize))
        return nullptr;

    const u64 text = table.offset + view.readU32(entry + nwfile::cReferenceOffset);
    const u32 length = view.readU32(entry + cStringEntryLength);

    if (length == 0 || !view.has(text, length) || std::memchr(view.at(text), '\0', length) == nullptr)
        return nullptr;

    return reinterpret_cast<const char*>(view.at(text));
}

static ArchiveSoundType ToSoundType(u16 detailType)
{
    switch (detailType)
    {
        case nw::snd::internal::ElementType_SoundArchiveFile_StreamSoundInfo:
            return ArchiveSoundType::Stream;

        case nw::snd::internal::ElementType_SoundArchiveFile_WaveSoundInfo:
            return ArchiveSoundType::Wave;

        case nw::snd::internal::ElementType_SoundArchiveFile_SequenceSoundInfo:
            return ArchiveSoundType::Sequence;

        default:
            break;
    }

    return ArchiveSoundType::Unknown;
}

static void ReadSound(const ByteView& view, const ArchiveLayout& layout, const StringTable& strings, u64 info, ArchiveSound* out)
{
    const u64 detail = info + (layout.isV3 ? cSoundDetailOffsetV3 : cSoundDetailOffset);
    const u64 option = info + (layout.isV3 ? cSoundOptionOffsetV3 : cSoundOptionOffset);

    if (view.has(detail, nwfile::cReferenceSize))
        out->type = ToSoundType(view.readU16(detail));

    if (!view.has(option, sizeof(u32) * 2) || (view.readU32(option) & cStringIdFlag) == 0)
        return;

    if (const char* name = FindString(view, strings, view.readU32(option + sizeof(u32))))
        out->name = name;
}

static void ReadSounds(const ByteView& view, const ArchiveLayout& layout, u64 infoBody, ArchiveContents* out)
{
    u64 table = 0;
    u32 count = 0;

    if (!ReadTable(view, infoBody, infoBody + static_cast<u64>(InfoTable::Sound) * nwfile::cReferenceSize, &table, &count))
        return;

    const StringTable strings = ReadStringTable(view, layout.strings);

    out->soundCount = count;
    out->sounds.resize(count);

    for (u32 i = 0; i < count; i++)
    {
        const u64 info = table + view.readU32(table + sizeof(u32) + static_cast<u64>(i) * nwfile::cReferenceSize + nwfile::cReferenceOffset);

        ReadSound(view, layout, strings, info, &out->sounds[i]);
        out->soundTypeCounts[static_cast<s32>(out->sounds[i].type)]++;
    }
}

bool ProbeArchiveContents(const void* data, u64 size, ArchiveContents* out)
{
    *out = ArchiveContents();

    ArchiveLayout layout;

    if (!ReadLayout(data, size, &layout))
        return false;

    const ByteView view(data, size, ByteView::IsBigEndianMark(data, size, nwfile::cHeaderByteOrder));
    const u64 infoBody = layout.info.offset + nwfile::cBlockHeaderSize;

    if (!view.has(layout.info.offset, layout.info.size))
        return false;

    ReadSounds(view, layout, infoBody, out);

    out->soundSetCount = CountInfoTable(view, infoBody, InfoTable::SoundSet);
    out->bankCount = CountInfoTable(view, infoBody, InfoTable::Bank);
    out->waveArchiveCount = CountInfoTable(view, infoBody, InfoTable::WaveArchive);
    out->groupCount = CountInfoTable(view, infoBody, InfoTable::Group);
    out->playerCount = CountInfoTable(view, infoBody, InfoTable::Player);

    return true;
}
