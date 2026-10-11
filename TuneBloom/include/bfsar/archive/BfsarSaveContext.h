#pragma once

#include <bfsar/archive/Bfsar.h>
#include <bfsar/model/File.h>
#include <bfsar/io/FileWriter.h>

#include <stream/seadStream.h>

#include <core/VectorMap.h>
#include <core/VectorSet.h>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class BfgrpFile;

struct BfsarSaveContext
{
    BfsarSaveContext(sead::FileHandle& handle_)
        : handle(handle_)
        , streamSrc(&handle_)
        , stream(&streamSrc, sead::Stream::Modes::eBinary)
        , writer(&handle_, &stream)
    {
    }

    u32 getStringId(const sead::SafeString& str) const
    {
        auto it = std::find(strings.begin(), strings.end(), str.cstr());

        if (it != strings.end())
            return it - strings.begin();
        else
            return nw::snd::internal::DEFAULT_STRING_ID;
    }

    bool isGeneratedWaveArchive(const WaveArchive* warc) const
    {
        return std::find(generatedWarcs.begin(), generatedWarcs.end(), warc) != generatedWarcs.end();
    }

    const VectorSet<u32>& getGroupItemFiles(const Group* group) const
    {
        static const VectorSet<u32> cNoFiles;

        const auto it = groupItemFiles.find(group);

        return it != groupItemFiles.end() ? it->second : cNoFiles;
    }

    u32 getPrefetchFileId(const Sound* sound) const
    {
        const auto it = prefetchFileIds.find(sound);

        return it != prefetchFileIds.end() ? it->second : nw::snd::SoundArchive::INVALID_ID;
    }

    bool hasWriteFailed() const
    {
        return streamSrc.hasFailed() || writer.hasSeekFailed();
    }

    sead::FileHandle& handle;
    CheckedFileStreamSrc streamSrc;
    sead::WriteStream stream;
    FileWriter writer;

    std::vector<std::string> strings;

    std::unordered_map<const Item*, VectorSet<const Group*>> itemAttachedGroups;
    std::unordered_map<const WaveArchive*, VectorSet<const WaveFile*>> warcWaveFiles;
    std::unordered_map<const WaveArchive*, std::unordered_map<const WaveFile*, u32>> warcWaveFilesIndexes;

    std::unordered_map<const SoundSet*, VectorMap<const Group*, const WaveArchive*>> waveSoundSetsWarcs;
    std::unordered_map<const Bank*, VectorMap<const Group*, const WaveArchive*>> banksWarcs;

    std::unordered_map<const Sound*, u32> wsdIndexes;

    std::vector<const WaveArchive*> generatedWarcs;
    std::unordered_map<const Group*, const WaveArchive*> groupTargetWarcs;

    std::unordered_map<const Item*, File> itemFileIds;
    std::vector<File> files;
    std::unordered_map<u32, VectorSet<const Item*>> filesItems;
    std::unordered_map<const Sound*, u32> prefetchFileIds;
    std::vector<Bfsar::SkippedStream> skippedStreams;

    std::vector<InnerFile*> generatedInnerFiles;
    std::unordered_map<const WaveArchive*, bool> includeGeneratedWarcInBfsar;

    std::unordered_map<const Group*, VectorSet<u32>> groupItemFiles;
    std::unordered_map<u32, VectorSet<const Group*>> fileGroups;

    std::unordered_set<u32> embeddedFileIds;
    std::unordered_map<u32, u32> embeddedFileToGroupFile;

    // External group files
    std::vector<std::pair<const Group*, const BfgrpFile*>> externalGroupFiles;
    std::unordered_set<u32> externalGroupFileIds;
    std::unordered_set<u32> nullDataFileIds;
};

inline const WaveArchive* FindWaveArchiveForGroup(const VectorMap<const Group*, const WaveArchive*>& warcMap, const Group* group)
{
    auto it = warcMap.find(group);

    if (!warcMap.isInMap(it))
        it = warcMap.find(nullptr);

    return warcMap.isInMap(it) ? it->second : nullptr;
}

void PrepareInnerFileForWrite(const BfsarSaveContext& ctx, const File& file, const Group* embedGroup);
