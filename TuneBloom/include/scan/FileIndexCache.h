#pragma once

#include <scan/FileProbe.h>

#include <filesystem>
#include <string>
#include <unordered_map>

struct CachedFile
{
    u64 size = 0;
    s64 modified = 0;
    bool isAudio = false;
    FileProbe probe;
};

using FileIndexCache = std::unordered_map<std::string, CachedFile>;

bool LoadFileIndexCache(const std::filesystem::path& path, FileIndexCache* out);
bool SaveFileIndexCache(const std::filesystem::path& path, const FileIndexCache& cache);
