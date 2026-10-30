#include <aal/BarsList.h>

#include <core/ByteBuilder.h>
#include <core/ByteView.h>

#include <cstring>

namespace aal
{

static constexpr u32 cHeaderByteOrder = 0x04;
static constexpr u32 cHeaderVersion   = 0x06;
static constexpr u32 cHeaderReserved  = 0x08;
static constexpr u32 cHeaderCount     = 0x0C;
static constexpr u32 cHeaderSize      = 0x10;
static constexpr u16 cByteOrderMark   = 0xFEFF;

static constexpr char cBarsListMagic[] = "ARSL";

bool IsBarsList(const void* data, u64 size)
{
    return data && size >= cHeaderSize && std::memcmp(data, cBarsListMagic, sizeof(u32)) == 0 && ByteView::IsByteOrderMark(data, size, cHeaderByteOrder);
}

bool ReadBarsList(const void* data, u64 size, BarsList* out)
{
    if (!IsBarsList(data, size))
        return false;

    const ByteView view(data, size, ByteView::IsBigEndianMark(data, size, cHeaderByteOrder));
    const u32 count = view.readU32(cHeaderCount);
    const u64 poolStart = cHeaderSize + static_cast<u64>(count) * sizeof(u32);

    if (!view.has(cHeaderSize, poolStart - cHeaderSize))
        return false;

    BarsList list;
    list.isBigEndian = view.isBigEndian();
    list.version = view.readU16(cHeaderVersion);
    list.reserved = view.readU32(cHeaderReserved);
    list.entries.resize(count);

    if (!view.readCString(poolStart, &list.name))
        return false;

    for (u32 i = 0; i < count; i++)
    {
        if (!view.readCString(poolStart + view.readU32(cHeaderSize + i * sizeof(u32)), &list.entries[i]))
            return false;
    }

    *out = std::move(list);
    return true;
}

std::vector<u8> WriteBarsList(const BarsList& list)
{
    ByteBuilder out(list.isBigEndian);

    out.appendBytes(cBarsListMagic, sizeof(u32));
    out.appendU16(cByteOrderMark);
    out.appendU16(list.version);
    out.appendU32(list.reserved);
    out.appendU32(static_cast<u32>(list.entries.size()));

    u32 poolOffset = static_cast<u32>(list.name.size() + 1);

    for (const std::string& entry : list.entries)
    {
        out.appendU32(poolOffset);
        poolOffset += static_cast<u32>(entry.size() + 1);
    }

    out.appendBytes(list.name.c_str(), list.name.size() + 1);

    for (const std::string& entry : list.entries)
        out.appendBytes(entry.c_str(), entry.size() + 1);

    return out.take();
}

}
