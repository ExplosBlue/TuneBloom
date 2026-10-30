#pragma once

#include <basis/seadTypes.h>

#include <string>
#include <vector>

namespace sarc
{

inline constexpr u32 cHeaderSize       = 0x14;
inline constexpr u32 cDefaultHashKey   = 0x65;
inline constexpr u32 cDefaultAlignment = 0x20;

struct Entry
{
    std::string name;
    bool hasName = true;
    u32 hash = 0;
    u8 nameFlags = 1;
    std::vector<u8> data;
};

struct IndexEntry
{
    std::string name;
    bool hasName = true;
    u32 hash = 0;
    u8 nameFlags = 1;
    u64 offset = 0;
    u64 size = 0;
};

struct Index
{
    bool isBigEndian = true;
    u16 version = 0x0100;
    u16 reserved = 0;
    u32 hashKey = cDefaultHashKey;
    u32 alignment = cDefaultAlignment;
    u32 dataOffset = 0;
    std::vector<IndexEntry> entries;
};

struct File
{
    bool isBigEndian = true;
    u16 version = 0x0100;
    u16 reserved = 0;
    u32 hashKey = cDefaultHashKey;
    u32 alignment = cDefaultAlignment;
    std::vector<Entry> entries;
};

bool IsSarc(const void* data, u64 size);
u32 GetIndexSize(const void* data, u64 size);
u32 GetDeclaredFileSize(const void* data, u64 size);
bool ReadIndex(const void* data, u64 size, Index* out);
bool Read(const void* data, u64 size, File* out);
std::vector<u8> Write(const File& file);

u32 HashName(const std::string& name, u32 key);
const Entry* FindEntry(const File& file, const std::string& name);
Entry* FindEntry(File& file, const std::string& name);
const IndexEntry* FindIndexEntry(const Index& index, const std::string& name);

}
