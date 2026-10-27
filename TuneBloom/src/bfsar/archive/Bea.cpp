#include <bfsar/archive/Bea.h>

#include <bfsar/archive/Cmpbin.h>
#include <bfsar/model/ArchiveFormat.h>
#include <core/ByteBuilder.h>
#include <core/ByteView.h>
#include <libs/md5.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>
#include <numeric>

namespace bea
{

static constexpr u32 cMagicLength = 4;

static constexpr u8 cMagic[cMagicLength]      = { 'S', 'C', 'N', 'E' };
static constexpr u8 cAssetMagic[cMagicLength] = { 'A', 'S', 'S', 'T' };

static constexpr u64 cByteOrderOffset   = 0x0C;
static constexpr u64 cMetadataEndOffset = 0x1C;
static constexpr u64 cAssetCountOffset  = 0x20;
static constexpr u64 cAssetArrayOffset  = 0x30;
static constexpr u64 cHeaderSize        = 0x38;

static constexpr u64 cEntryCompressedSizeOffset = 0x14;
static constexpr u64 cEntrySizeOffset           = 0x18;
static constexpr u64 cEntryHashOffset           = 0x28;
static constexpr u64 cEntryDataOffset           = 0x38;
static constexpr u64 cEntryNameOffset           = 0x40;
static constexpr u64 cEntrySize                 = 0x48;

static constexpr u32 cHashSize       = 16;
static constexpr u32 cAssetCountMax  = 0x10000;
static constexpr u64 cDataAlignment  = 16;
static constexpr u64 cDataTerminator = 1;
static constexpr u64 cSoundPeekSize  = 16;

static u64 ReadU64(const ByteView& view, u64 offset)
{
    return static_cast<u64>(view.readU32(offset)) | static_cast<u64>(view.readU32(offset + sizeof(u32))) << 32;
}

static void PutU64(ByteBuilder* builder, u64 offset, u64 value)
{
    builder->putU32(offset, static_cast<u32>(value));
    builder->putU32(offset + sizeof(u32), static_cast<u32>(value >> 32));
}

static bool HasMagic(const ByteView& view, u64 offset, const u8* magic)
{
    return view.has(offset, cMagicLength) && std::memcmp(view.at(offset), magic, cMagicLength) == 0;
}

static u8 ParseHexDigit(char digit)
{
    return static_cast<u8>(digit <= '9' ? digit - '0' : std::tolower(static_cast<unsigned char>(digit)) - 'a' + 10);
}

static void CalcHash(const void* data, size_t size, u8* outHash)
{
    const std::string hex = md5(data, static_cast<unsigned int>(size));

    for (u32 i = 0; i < cHashSize; i++)
        outHash[i] = static_cast<u8>(ParseHexDigit(hex[i * 2]) << 4 | ParseHexDigit(hex[i * 2 + 1]));
}

static bool ReadAsset(const ByteView& view, u64 metadataEnd, u64 fileSize, u64 entryOffset, Asset* outAsset)
{
    if (entryOffset + cEntrySize > metadataEnd || !HasMagic(view, entryOffset, cAssetMagic))
        return false;

    outAsset->entryOffset = entryOffset;
    outAsset->compressedSize = view.readU32(entryOffset + cEntryCompressedSizeOffset);
    outAsset->size = view.readU32(entryOffset + cEntrySizeOffset);
    outAsset->dataOffset = ReadU64(view, entryOffset + cEntryDataOffset);

    const u64 nameOffset = ReadU64(view, entryOffset + cEntryNameOffset);

    if (outAsset->dataOffset < metadataEnd || outAsset->dataOffset > fileSize || outAsset->compressedSize > fileSize - outAsset->dataOffset)
        return false;

    if (nameOffset >= metadataEnd || !view.has(nameOffset, sizeof(u16)))
        return false;

    const u16 nameLength = view.readU16(nameOffset);

    if (nameOffset + sizeof(u16) + nameLength > metadataEnd)
        return false;

    outAsset->name.assign(reinterpret_cast<const char*>(view.at(nameOffset + sizeof(u16))), nameLength);

    return true;
}

bool IsBea(const void* data, size_t size)
{
    const ByteView view(data, size, false);

    return HasMagic(view, 0, cMagic);
}

u64 GetMetadataSize(const void* data, size_t size)
{
    const ByteView view(data, size, false);

    if (!IsBea(data, size) || !view.has(0, cHeaderSize) || !ByteView::IsByteOrderMark(data, size, cByteOrderOffset) || ByteView::IsBigEndianMark(data, size, cByteOrderOffset))
        return 0;

    const u64 metadataEnd = view.readU32(cMetadataEndOffset);

    return metadataEnd >= cHeaderSize ? metadataEnd : 0;
}

bool ReadAssetTable(const void* metadata, size_t metadataSize, u64 fileSize, std::vector<Asset>* outAssets)
{
    outAssets->clear();

    const u64 metadataEnd = GetMetadataSize(metadata, metadataSize);

    if (metadataEnd == 0 || metadataEnd > metadataSize || metadataEnd > fileSize)
        return false;

    const ByteView view(metadata, metadataSize, false);
    const u32 count = view.readU32(cAssetCountOffset);
    const u64 arrayOffset = ReadU64(view, cAssetArrayOffset);

    if (count > cAssetCountMax || arrayOffset > metadataEnd || count * sizeof(u64) > metadataEnd - arrayOffset)
        return false;

    outAssets->resize(count);

    for (u32 i = 0; i < count; i++)
    {
        if (!ReadAsset(view, metadataEnd, fileSize, ReadU64(view, arrayOffset + i * sizeof(u64)), &(*outAssets)[i]))
        {
            outAssets->clear();
            return false;
        }
    }

    return true;
}

bool ReadAssets(const void* data, size_t size, std::vector<Asset>* outAssets)
{
    return ReadAssetTable(data, size, size, outAssets);
}

bool PeekAsset(const void* data, size_t size, const Asset& asset, size_t limit, std::vector<u8>* out)
{
    if (asset.dataOffset > size || asset.compressedSize > size - asset.dataOffset)
        return false;

    return cmpbin::PeekZstdFrame(static_cast<const u8*>(data) + asset.dataOffset, asset.compressedSize, limit, *out);
}

bool ExtractAsset(const void* data, size_t size, const Asset& asset, std::vector<u8>* out)
{
    if (asset.dataOffset > size || asset.compressedSize > size - asset.dataOffset)
        return false;

    const u8* frame = static_cast<const u8*>(data) + asset.dataOffset;

    return cmpbin::IsZstdFrame(frame, asset.compressedSize) && cmpbin::DecompressZstdFrame(frame, asset.compressedSize, *out) && out->size() == asset.size;
}

s32 FindSoundArchiveAsset(const void* data, size_t size, const std::vector<Asset>& assets)
{
    std::vector<u8> head;

    for (size_t i = 0; i < assets.size(); i++)
    {
        if (PeekAsset(data, size, assets[i], cSoundPeekSize, &head) && head.size() >= cInnerFileMagicLength && MatchesInnerFileKind(head.data(), InnerFileKind::SoundArchive))
            return static_cast<s32>(i);
    }

    return -1;
}

ExtractResult ExtractSoundArchive(std::vector<u8> container, std::vector<u8>* outArchive, Source* outSource)
{
    std::vector<Asset> assets;

    if (!ReadAssets(container.data(), container.size(), &assets))
        return ExtractResult::Damaged;

    const s32 index = FindSoundArchiveAsset(container.data(), container.size(), assets);

    if (index < 0)
        return ExtractResult::NoSoundArchive;

    if (!ExtractAsset(container.data(), container.size(), assets[index], outArchive))
        return ExtractResult::Damaged;

    outSource->container = std::make_shared<const std::vector<u8>>(std::move(container));
    outSource->assetIndex = static_cast<u32>(index);

    return ExtractResult::Ok;
}

bool ReplaceAsset(const Source& source, const void* data, size_t size, std::vector<u8>* out)
{
    const std::vector<u8>& container = *source.container;
    std::vector<Asset> assets;

    if (!ReadAssets(container.data(), container.size(), &assets) || source.assetIndex >= assets.size())
        return false;

    std::vector<u8> frame;

    if (size > std::numeric_limits<u32>::max() || !cmpbin::CompressZstdFrame(data, size, frame) || frame.size() > std::numeric_limits<u32>::max())
        return false;

    std::vector<u32> order(assets.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](u32 left, u32 right) { return assets[left].dataOffset < assets[right].dataOffset; });

    const u64 metadataEnd = ByteView(container.data(), container.size(), false).readU32(cMetadataEndOffset);
    ByteBuilder builder(std::vector<u8>(container.begin(), container.begin() + metadataEnd), false);

    for (u32 index : order)
    {
        const Asset& asset = assets[index];
        const bool isReplaced = index == source.assetIndex;
        const u8* bytes = isReplaced ? frame.data() : container.data() + asset.dataOffset;
        const u32 compressedSize = isReplaced ? static_cast<u32>(frame.size()) : asset.compressedSize;

        builder.alignTo(cDataAlignment);
        PutU64(&builder, asset.entryOffset + cEntryDataOffset, builder.size());

        if (isReplaced)
        {
            u8 hash[cHashSize];
            CalcHash(data, size, hash);

            builder.putU32(asset.entryOffset + cEntryCompressedSizeOffset, compressedSize);
            builder.putU32(asset.entryOffset + cEntrySizeOffset, static_cast<u32>(size));
            builder.putBytes(asset.entryOffset + cEntryHashOffset, hash, cHashSize);
        }

        builder.appendBytes(bytes, compressedSize);
        builder.appendZeros(cDataTerminator);
    }

    *out = builder.take();

    return true;
}

}
