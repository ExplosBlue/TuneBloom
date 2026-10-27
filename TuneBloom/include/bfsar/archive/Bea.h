#pragma once

#include <basis/seadTypes.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace bea
{

struct Asset
{
    std::string name;
    u64 entryOffset = 0;
    u64 dataOffset = 0;
    u32 compressedSize = 0;
    u32 size = 0;
};

struct Source
{
    std::shared_ptr<const std::vector<u8>> container;
    u32 assetIndex = 0;

    bool isSet() const
    {
        return container != nullptr;
    }
};

enum class ExtractResult
{
    Ok,
    Damaged,
    NoSoundArchive
};

bool IsBea(const void* data, size_t size);
u64 GetMetadataSize(const void* data, size_t size);
bool ReadAssetTable(const void* metadata, size_t metadataSize, u64 fileSize, std::vector<Asset>* outAssets);
bool ReadAssets(const void* data, size_t size, std::vector<Asset>* outAssets);
bool PeekAsset(const void* data, size_t size, const Asset& asset, size_t limit, std::vector<u8>* out);
bool ExtractAsset(const void* data, size_t size, const Asset& asset, std::vector<u8>* out);
s32 FindSoundArchiveAsset(const void* data, size_t size, const std::vector<Asset>& assets);
ExtractResult ExtractSoundArchive(std::vector<u8> container, std::vector<u8>* outArchive, Source* outSource);
bool ReplaceAsset(const Source& source, const void* data, size_t size, std::vector<u8>* out);

}
