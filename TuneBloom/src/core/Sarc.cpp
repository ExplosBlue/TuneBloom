#include <core/Sarc.h>

#include <core/ByteBuilder.h>
#include <core/ByteView.h>

#include <algorithm>
#include <cstring>
#include <numeric>

namespace sarc
{

static constexpr u32 cHeaderByteOrder  = 0x06;
static constexpr u32 cHeaderFileSize   = 0x08;
static constexpr u32 cHeaderDataOffset = 0x0C;
static constexpr u32 cHeaderVersion    = 0x10;
static constexpr u32 cHeaderReserved   = 0x12;
static constexpr u32 cSfatHeaderSize   = 0x0C;
static constexpr u32 cSfatNodeCount    = 0x06;
static constexpr u32 cSfatHashKey      = 0x08;
static constexpr u32 cSfatNodeSize     = 0x10;
static constexpr u32 cSfntHeaderSize   = 0x08;
static constexpr u32 cNameAlignment    = 4;
static constexpr u32 cNameOffsetMask   = 0x00FFFFFF;
static constexpr u32 cNameFlagsShift   = 24;
static constexpr u32 cAlignmentMax     = 0x2000;
static constexpr u16 cByteOrderMark    = 0xFEFF;

static constexpr char cSarcMagic[] = "SARC";
static constexpr char cSfatMagic[] = "SFAT";
static constexpr char cSfntMagic[] = "SFNT";

struct Node
{
    u32 hash = 0;
    u32 attributes = 0;
    u32 start = 0;
    u32 end = 0;
};

static u64 AlignUp(u64 value, u64 alignment)
{
    return (value + alignment - 1) / alignment * alignment;
}

static u32 LowestBit(u64 value)
{
    return value == 0 ? cAlignmentMax : static_cast<u32>(std::min<u64>(value & (~value + 1), cAlignmentMax));
}

static bool HasMagic(const ByteView& view, u64 offset, const char* magic)
{
    return view.has(offset, sizeof(u32)) && std::memcmp(view.at(offset), magic, sizeof(u32)) == 0;
}

static bool ReadNodes(const ByteView& view, u32 count, std::vector<Node>* out)
{
    const u64 nodesStart = cHeaderSize + cSfatHeaderSize;

    if (!view.has(nodesStart, static_cast<u64>(count) * cSfatNodeSize))
        return false;

    out->resize(count);

    for (u32 i = 0; i < count; i++)
    {
        const u64 offset = nodesStart + static_cast<u64>(i) * cSfatNodeSize;
        Node& node = (*out)[i];

        node.hash = view.readU32(offset);
        node.attributes = view.readU32(offset + 0x4);
        node.start = view.readU32(offset + 0x8);
        node.end = view.readU32(offset + 0xC);
    }

    return true;
}

static bool ReadName(const ByteView& view, u64 namesStart, u64 namesEnd, u32 attributes, std::string* out)
{
    return view.readCString(namesStart + static_cast<u64>(attributes & cNameOffsetMask) * cNameAlignment, namesEnd, out);
}

bool IsSarc(const void* data, u64 size)
{
    return data && size >= cHeaderSize && std::memcmp(data, cSarcMagic, sizeof(u32)) == 0 && ByteView::IsByteOrderMark(data, size, cHeaderByteOrder);
}

u32 GetIndexSize(const void* data, u64 size)
{
    return IsSarc(data, size) ? ByteView(data, size, ByteView::IsBigEndianMark(data, size, cHeaderByteOrder)).readU32(cHeaderDataOffset) : 0;
}

u32 GetDeclaredFileSize(const void* data, u64 size)
{
    return IsSarc(data, size) ? ByteView(data, size, ByteView::IsBigEndianMark(data, size, cHeaderByteOrder)).readU32(cHeaderFileSize) : 0;
}

bool ReadIndex(const void* data, u64 size, Index* out)
{
    if (!IsSarc(data, size))
        return false;

    const ByteView view(data, size, ByteView::IsBigEndianMark(data, size, cHeaderByteOrder));
    const u32 dataOffset = view.readU32(cHeaderDataOffset);
    const u64 fileSize = view.readU32(cHeaderFileSize);

    if (!HasMagic(view, cHeaderSize, cSfatMagic) || !view.has(cHeaderSize, cSfatHeaderSize) || dataOffset > size || dataOffset > fileSize)
        return false;

    const u32 count = view.readU16(cHeaderSize + cSfatNodeCount);
    std::vector<Node> nodes;

    if (!ReadNodes(view, count, &nodes))
        return false;

    const u64 sfntStart = cHeaderSize + cSfatHeaderSize + static_cast<u64>(count) * cSfatNodeSize;

    if (!HasMagic(view, sfntStart, cSfntMagic) || sfntStart + cSfntHeaderSize > dataOffset)
        return false;

    Index index;
    index.isBigEndian = view.isBigEndian();
    index.version = view.readU16(cHeaderVersion);
    index.reserved = view.readU16(cHeaderReserved);
    index.hashKey = view.readU32(cHeaderSize + cSfatHashKey);
    index.alignment = LowestBit(dataOffset);
    index.dataOffset = dataOffset;
    index.entries.resize(count);

    for (u32 i = 0; i < count; i++)
    {
        const Node& node = nodes[i];
        IndexEntry& entry = index.entries[i];

        entry.offset = static_cast<u64>(dataOffset) + node.start;
        entry.size = node.end - node.start;

        if (node.start > node.end || entry.offset + entry.size > fileSize)
            return false;

        entry.hash = node.hash;
        entry.nameFlags = static_cast<u8>(node.attributes >> cNameFlagsShift);
        entry.hasName = entry.nameFlags != 0;

        if (entry.hasName && !ReadName(view, sfntStart + cSfntHeaderSize, dataOffset, node.attributes, &entry.name))
            return false;

        index.alignment = std::min(index.alignment, LowestBit(entry.offset));
    }

    *out = std::move(index);
    return true;
}

bool Read(const void* data, u64 size, File* out)
{
    Index index;

    if (!ReadIndex(data, size, &index))
        return false;

    const u8* bytes = static_cast<const u8*>(data);

    File file;
    file.isBigEndian = index.isBigEndian;
    file.version = index.version;
    file.reserved = index.reserved;
    file.hashKey = index.hashKey;
    file.alignment = index.alignment;
    file.entries.resize(index.entries.size());

    for (u32 i = 0; i < index.entries.size(); i++)
    {
        const IndexEntry& source = index.entries[i];
        Entry& entry = file.entries[i];

        if (source.offset + source.size > size)
            return false;

        entry.name = source.name;
        entry.hasName = source.hasName;
        entry.hash = source.hash;
        entry.nameFlags = source.nameFlags;
        entry.data.assign(bytes + source.offset, bytes + source.offset + source.size);
    }

    *out = std::move(file);
    return true;
}

static u32 GetEntryHash(const File& file, const Entry& entry)
{
    return entry.hasName ? HashName(entry.name, file.hashKey) : entry.hash;
}

std::vector<u8> Write(const File& file)
{
    std::vector<u32> order(file.entries.size());
    std::vector<u32> hashes(file.entries.size());

    for (u32 i = 0; i < file.entries.size(); i++)
        hashes[i] = GetEntryHash(file, file.entries[i]);

    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&hashes](u32 left, u32 right) { return hashes[left] < hashes[right]; });

    ByteBuilder names(file.isBigEndian);
    std::vector<u32> nameOffsets(order.size(), 0);

    for (u32 i = 0; i < order.size(); i++)
    {
        const Entry& entry = file.entries[order[i]];

        if (!entry.hasName)
            continue;

        nameOffsets[i] = static_cast<u32>(names.size() / cNameAlignment);
        names.appendBytes(entry.name.c_str(), entry.name.size() + 1);
        names.resize(AlignUp(names.size(), cNameAlignment));
    }

    const u32 alignment = std::max<u32>(file.alignment, 1);
    const u64 namesEnd = cHeaderSize + cSfatHeaderSize + order.size() * cSfatNodeSize + cSfntHeaderSize + names.size();
    const u64 dataOffset = AlignUp(namesEnd, alignment);

    std::vector<Node> nodes(order.size());
    u64 dataEnd = dataOffset;

    for (u32 i = 0; i < order.size(); i++)
    {
        const Entry& entry = file.entries[order[i]];
        const u64 start = AlignUp(dataEnd, alignment);

        nodes[i].hash = hashes[order[i]];
        nodes[i].attributes = entry.hasName ? static_cast<u32>(entry.nameFlags) << cNameFlagsShift | nameOffsets[i] : 0;
        nodes[i].start = static_cast<u32>(start - dataOffset);
        nodes[i].end = static_cast<u32>(start - dataOffset + entry.data.size());

        dataEnd = start + entry.data.size();
    }

    ByteBuilder out(file.isBigEndian);
    out.reserve(dataEnd);

    out.appendBytes(cSarcMagic, sizeof(u32));
    out.appendU16(cHeaderSize);
    out.appendU16(cByteOrderMark);
    out.appendU32(static_cast<u32>(dataEnd));
    out.appendU32(static_cast<u32>(dataOffset));
    out.appendU16(file.version);
    out.appendU16(file.reserved);

    out.appendBytes(cSfatMagic, sizeof(u32));
    out.appendU16(cSfatHeaderSize);
    out.appendU16(static_cast<u16>(nodes.size()));
    out.appendU32(file.hashKey);

    for (const Node& node : nodes)
    {
        out.appendU32(node.hash);
        out.appendU32(node.attributes);
        out.appendU32(node.start);
        out.appendU32(node.end);
    }

    out.appendBytes(cSfntMagic, sizeof(u32));
    out.appendU16(cSfntHeaderSize);
    out.appendU16(0);
    out.appendBytes(names.bytes().data(), names.size());

    for (u32 i = 0; i < order.size(); i++)
    {
        const Entry& entry = file.entries[order[i]];

        out.resize(dataOffset + nodes[i].start);
        out.appendBytes(entry.data.data(), entry.data.size());
    }

    out.resize(dataEnd);
    return out.take();
}

u32 HashName(const std::string& name, u32 key)
{
    u32 hash = 0;

    for (const char character : name)
        hash = hash * key + static_cast<u32>(static_cast<s32>(static_cast<signed char>(character)));

    return hash;
}

template <typename T>
static const T* FindNamedEntry(const std::vector<T>& entries, const std::string& name)
{
    for (const T& entry : entries)
    {
        if (entry.hasName && entry.name == name)
            return &entry;
    }

    return nullptr;
}

const Entry* FindEntry(const File& file, const std::string& name)
{
    return FindNamedEntry(file.entries, name);
}

Entry* FindEntry(File& file, const std::string& name)
{
    return const_cast<Entry*>(FindEntry(static_cast<const File&>(file), name));
}

const IndexEntry* FindIndexEntry(const Index& index, const std::string& name)
{
    return FindNamedEntry(index.entries, name);
}

}
