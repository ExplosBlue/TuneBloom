#include <aal/Bars.h>

#include <aal/Amta.h>
#include <aal/Bwav.h>
#include <core/ByteBuilder.h>
#include <core/ByteView.h>
#include <ui/Messages.h>

#include <codec/seadHashCRC32.h>

#include <algorithm>
#include <cstring>
#include <numeric>

namespace aal
{

static constexpr u32 cHeaderSize          = 0x10;
static constexpr u32 cHeaderFileSize      = 0x04;
static constexpr u32 cHeaderByteOrder     = 0x08;
static constexpr u32 cHeaderVersion       = 0x0A;
static constexpr u32 cHeaderEntryCount    = 0x0C;
static constexpr u32 cOffsetPairSize      = 0x08;
static constexpr u32 cAssetAlignment      = 0x40;
static constexpr u32 cNoOffset            = 0xFFFFFFFF;
static constexpr u16 cByteOrderMark       = 0xFEFF;
static constexpr u32 cNwHeaderByteOrder   = 0x04;
static constexpr u32 cNwHeaderFileSize    = 0x0C;
static constexpr u32 cNwHeaderSize        = 0x14;

static constexpr char cBarsMagic[] = "BARS";
static constexpr char cFwavMagic[] = "FWAV";
static constexpr char cFstpMagic[] = "FSTP";

struct EntryOffsets
{
    u32 meta = 0;
    u32 asset = cNoOffset;
};

static bool HasMagic(const void* data, u64 size, const char* magic)
{
    return data && size >= sizeof(u32) && std::memcmp(data, magic, sizeof(u32)) == 0;
}

static u64 MeasureNwFile(const void* data, u64 size)
{
    if (size < cNwHeaderSize || !ByteView::IsByteOrderMark(data, size, cNwHeaderByteOrder))
        return size;

    const ByteView view(data, size, ByteView::IsBigEndianMark(data, size, cNwHeaderByteOrder));

    return std::min<u64>(view.readU32(cNwHeaderFileSize), size);
}

static u64 MeasureBarsAsset(const void* data, u64 size)
{
    if (IsBwav(data, size))
        return MeasureBwav(data, size);

    if (HasMagic(data, size, cFwavMagic) || HasMagic(data, size, cFstpMagic))
        return MeasureNwFile(data, size);

    return size;
}

static bool ReadEntryOffsets(const ByteView& view, u32 count, std::vector<EntryOffsets>* out)
{
    const u64 offsetsStart = cHeaderSize + static_cast<u64>(count) * sizeof(u32);

    if (!view.has(offsetsStart, static_cast<u64>(count) * cOffsetPairSize))
        return false;

    out->resize(count);

    for (u32 i = 0; i < count; i++)
    {
        (*out)[i].meta = view.readU32(offsetsStart + i * cOffsetPairSize);
        (*out)[i].asset = view.readU32(offsetsStart + i * cOffsetPairSize + sizeof(u32));
    }

    return true;
}

static bool ReadResetTable(const ByteView& view, u32 count, std::vector<u32>* out)
{
    const u64 tableStart = cHeaderSize + static_cast<u64>(count) * (sizeof(u32) + cOffsetPairSize);

    if (!view.has(tableStart, sizeof(u32)))
        return false;

    const u32 tableCount = view.readU32(tableStart);

    if (!view.has(tableStart + sizeof(u32), static_cast<u64>(tableCount) * sizeof(u32)))
        return false;

    out->resize(tableCount);

    for (u32 i = 0; i < tableCount; i++)
        (*out)[i] = view.readU32(tableStart + sizeof(u32) + i * sizeof(u32));

    return true;
}

static const char* GetAssetError(const void* data, u64 size)
{
    Bwav bwav;
    const char* error = messages::resource::cBarsDamaged;

    if (IsBwav(data, size))
        ReadBwav(data, size, &bwav, &error);

    return error;
}

static bool ReadAssets(const ByteView& view, const std::vector<EntryOffsets>& offsets, BarsFile* bars, std::vector<u32>* outAssetOffsets, u64* outEnd, const char** outError)
{
    std::vector<u32> assetOffsets;

    for (const EntryOffsets& entry : offsets)
    {
        if (entry.asset != cNoOffset)
            assetOffsets.push_back(entry.asset);
    }

    std::sort(assetOffsets.begin(), assetOffsets.end());
    assetOffsets.erase(std::unique(assetOffsets.begin(), assetOffsets.end()), assetOffsets.end());

    for (u32 i = 0; i < assetOffsets.size(); i++)
    {
        const u64 offset = assetOffsets[i];
        const u64 limit = i + 1 < assetOffsets.size() ? std::min<u64>(assetOffsets[i + 1], view.size()) : view.size();

        if (offset >= limit)
            return false;

        const u64 size = MeasureBarsAsset(view.at(offset), limit - offset);

        if (size == 0)
        {
            *outError = GetAssetError(view.at(offset), limit - offset);
            return false;
        }

        bars->assets.emplace_back(view.at(offset), view.at(offset) + size);
        *outEnd = std::max(*outEnd, offset + size);
    }

    *outAssetOffsets = std::move(assetOffsets);
    return true;
}

u32 HashBarsName(const std::string& name)
{
    return sead::HashCRC32::calcHash(name.data(), static_cast<u32>(name.size()));
}

bool IsBars(const void* data, u64 size)
{
    return size >= cHeaderSize && HasMagic(data, size, cBarsMagic) && ByteView::IsByteOrderMark(data, size, cHeaderByteOrder);
}

bool ReadBars(const void* data, u64 size, BarsFile* out, const char** outError)
{
    *outError = messages::resource::cBarsDamaged;

    if (!IsBars(data, size))
        return false;

    const ByteView view(data, size, ByteView::IsBigEndianMark(data, size, cHeaderByteOrder));

    BarsFile bars;
    bars.isBigEndian = view.isBigEndian();
    bars.version = view.readU16(cHeaderVersion);

    if (bars.version != cBarsVersion11 && bars.version != cBarsVersion12)
    {
        *outError = messages::resource::cBarsVersionUnsupported;
        return false;
    }

    if (view.readU32(cHeaderFileSize) > size)
        return false;

    const u32 count = view.readU32(cHeaderEntryCount);

    std::vector<EntryOffsets> offsets;
    if (!ReadEntryOffsets(view, count, &offsets))
        return false;

    std::vector<u32> resetTable;
    if (bars.version == cBarsVersion12 && !ReadResetTable(view, count, &resetTable))
        return false;

    u64 end = cHeaderSize;

    std::vector<u32> assetOffsets;
    if (!ReadAssets(view, offsets, &bars, &assetOffsets, &end, outError))
        return false;

    bars.entries.resize(count);

    for (u32 i = 0; i < count; i++)
    {
        BarsEntry& entry = bars.entries[i];
        const u32 metaOffset = offsets[i].meta;
        const u32 metaSize = metaOffset < size ? MeasureAmta(view.at(metaOffset), size - metaOffset) : 0;

        if (metaSize == 0)
            return false;

        entry.meta.assign(view.at(metaOffset), view.at(metaOffset) + metaSize);
        end = std::max<u64>(end, static_cast<u64>(metaOffset) + metaSize);

        std::string name;
        if (!ReadAmtaName(entry.meta, &name))
            return false;

        if (offsets[i].asset != cNoOffset)
            entry.assetIndex = static_cast<s32>(std::lower_bound(assetOffsets.begin(), assetOffsets.end(), offsets[i].asset) - assetOffsets.begin());

        entry.isInResetTable = std::find(resetTable.begin(), resetTable.end(), HashBarsName(name)) != resetTable.end();
    }

    bars.trailing.assign(view.at(end), view.at(size));

    *out = std::move(bars);
    *outError = nullptr;
    return true;
}

bool WriteBars(const BarsFile& bars, std::vector<u8>* out, const char** outError)
{
    const u32 count = static_cast<u32>(bars.entries.size());

    std::vector<u32> hashes(count);

    for (u32 i = 0; i < count; i++)
    {
        std::string name;

        if (!ReadAmtaName(bars.entries[i].meta, &name))
        {
            *outError = messages::resource::cAmtaDamaged;
            return false;
        }

        hashes[i] = HashBarsName(name);
    }

    std::vector<u32> order(count);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&hashes](u32 left, u32 right)
    {
        return hashes[left] < hashes[right];
    });

    for (u32 i = 1; i < count; i++)
    {
        if (hashes[order[i]] == hashes[order[i - 1]])
        {
            *outError = messages::resource::cNameHashCollision;
            return false;
        }
    }

    std::vector<u32> resetTable;

    if (bars.version == cBarsVersion12)
    {
        for (u32 index : order)
        {
            if (bars.entries[index].isInResetTable)
                resetTable.push_back(hashes[index]);
        }
    }

    ByteBuilder builder(bars.isBigEndian);
    builder.appendBytes(cBarsMagic, sizeof(u32));
    builder.appendU32(0);
    builder.appendU16(cByteOrderMark);
    builder.appendU16(bars.version);
    builder.appendU32(count);

    for (u32 index : order)
        builder.appendU32(hashes[index]);

    const u64 offsetsStart = builder.size();
    builder.appendZeros(static_cast<u64>(count) * cOffsetPairSize);

    if (bars.version == cBarsVersion12)
    {
        builder.appendU32(static_cast<u32>(resetTable.size()));

        for (u32 hash : resetTable)
            builder.appendU32(hash);
    }

    for (u32 i = 0; i < count; i++)
    {
        const BarsEntry& entry = bars.entries[order[i]];

        builder.putU32(offsetsStart + i * cOffsetPairSize, static_cast<u32>(builder.size()));
        builder.appendBytes(entry.meta.data(), entry.meta.size());
    }

    std::vector<u32> assetOffsets(bars.assets.size(), cNoOffset);

    for (u32 i = 0; i < count; i++)
    {
        const s32 assetIndex = bars.entries[order[i]].assetIndex;
        u32 assetOffset = cNoOffset;

        if (assetIndex != cBarsNoAsset)
        {
            if (assetOffsets[assetIndex] == cNoOffset)
            {
                builder.alignTo(cAssetAlignment);
                assetOffsets[assetIndex] = static_cast<u32>(builder.size());
                builder.appendBytes(bars.assets[assetIndex].data(), bars.assets[assetIndex].size());
            }

            assetOffset = assetOffsets[assetIndex];
        }

        builder.putU32(offsetsStart + i * cOffsetPairSize + sizeof(u32), assetOffset);
    }

    builder.putU32(cHeaderFileSize, static_cast<u32>(builder.size()));
    builder.appendBytes(bars.trailing.data(), bars.trailing.size());

    *out = builder.take();
    *outError = nullptr;
    return true;
}

}
