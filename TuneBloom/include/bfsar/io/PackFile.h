#pragma once

#include <basis/seadTypes.h>

#include <string>
#include <vector>

namespace packfile
{

inline constexpr char cFolderName[] = "Pack";

struct Resource
{
    std::string name;
    u64 offset = 0;
    u64 size = 0;
};

bool Split(const std::string& path, std::string* outPackPath, std::string* outInnerName);
bool IsPackPath(const std::string& path);
bool IsOnDisk(const std::string& path);
std::string GetDiskPath(const std::string& path);
std::string GetFolderOnDisk(const std::string& path);
std::string MakePath(const std::string& packPath, const std::string& innerName);
bool GetFileSize(const std::string& path, u64* outSize);

bool IsSoundPackFile(const std::string& path);
bool ListResources(const std::string& packPath, std::vector<Resource>* out);

bool ReadPackContents(const std::string& packPath, std::vector<u8>* out);
bool ReadFile(const std::string& path, std::vector<u8>* out);
bool WriteFile(const std::string& path, const void* data, size_t size);
bool CopyFileOver(const std::string& srcPath, const std::string& destPath);
bool MakePackWithFile(const std::string& packPath, const std::string& innerName, const void* data, size_t size, std::vector<u8>* out);

}
