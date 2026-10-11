#pragma once

#include <bfsar/model/ArchiveFormat.h>
#include <bfsar/archive/BfsarLayoutCheck.h>
#include <bfsar/files/BfstmFile.h>
#include <bfsar/model/Group.h>
#include <bfsar/model/Sound.h>

#include <core/VectorSet.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct WaveCacheEntry
{
    std::string hash;
    u32 id;
    const void* fileData;
    u32 fileDataSize;
};

struct BfsarOpenContext
{
    BfsarOpenContext(const nw::snd::MemorySoundArchive& soundArchive_, u32 bfsarSize_, sead::Heap* heap_, const sead::SafeString& groupFileSource_)
        : soundArchive(soundArchive_)
        , archiveBytes(soundArchive_.mData, bfsarSize_)
        , heap(heap_)
        , groupFileSource(groupFileSource_)
    {
    }

    const nw::snd::MemorySoundArchive& soundArchive;
    ByteRange archiveBytes;
    sead::Heap* heap;
    const sead::SafeString& groupFileSource;

    std::vector<ByteRange> externalGroupFiles;
    std::unordered_map<u32, u32> seqFileIdxMap;
    std::vector< std::vector<WaveCacheEntry> > warcFileCache;
    std::vector< std::unordered_set<u32> > waveIdMapSet;
    std::vector< std::vector<std::string> > warcWaveHashes;
    std::unordered_map<u64, u32> bankFileWarcId;
    std::unordered_map<u64, u32> wsdFileWarcId;
    std::unordered_map<u32, u32> bankFileIdxMap;
    std::unordered_map<std::string, const Sound*> streamSounds;
    std::optional<StreamLoopInfoBatch> loopInfoBatch;
};

struct BfsarGroupItemFiles
{
    const Group* group;
    u32 groupId;
    u32 itemIdx;
    u32 itemId;
    u32 loadFlag;
    VectorSet<u32>* fileIds;
};

ByteRange LoadFileBytes(const sead::SafeString& path);
void FreeFileBytes(const ByteRange& file);

struct LoadedFile
{
    explicit LoadedFile(const sead::SafeString& path)
        : bytes(LoadFileBytes(path))
    {
    }

    ~LoadedFile()
    {
        FreeFileBytes(bytes);
    }

    LoadedFile(const LoadedFile&) = delete;
    LoadedFile& operator=(const LoadedFile&) = delete;

    const ByteRange bytes;
};

inline u64 MakeGroupFileKey(u32 fileId, u32 groupId)
{
    return static_cast<u64>(fileId) << 32 | groupId;
}

inline bool IsInnerFileKind(const ByteRange& file, InnerFileKind kind)
{
    return file.size() >= cInnerFileMagicLength && MatchesInnerFileKind(file.data(), kind);
}

inline ByteRange FindLoadedBytes(const BfsarOpenContext& ctx, const void* data, u64 size)
{
    if (ctx.archiveBytes.contains(data, size))
        return ByteRange(data, size, ctx.archiveBytes);

    for (const ByteRange& groupFile : ctx.externalGroupFiles)
    {
        if (groupFile.contains(data, size))
            return ByteRange(data, size, groupFile);
    }

    return ByteRange();
}

inline ByteRange FindArchiveFile(const BfsarOpenContext& ctx, u32 fileId, u32 variationId = 0, u32* outGroupId = nullptr)
{
    u32 fileSize = 0;
    const void* file = ctx.soundArchive.detail_GetFileAddress(fileId, &fileSize, variationId, nullptr, outGroupId);

    if (!file)
        return ByteRange();

    for (const ByteRange& groupFile : ctx.externalGroupFiles)
    {
        if (groupFile.data() == file)
            return groupFile;
    }

    return FindLoadedBytes(ctx, file, fileSize);
}
