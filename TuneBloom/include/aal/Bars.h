#pragma once

#include <basis/seadTypes.h>

#include <string>
#include <vector>

namespace aal
{

inline constexpr u16 cBarsVersion11 = 0x0101;
inline constexpr u16 cBarsVersion12 = 0x0102;
inline constexpr s32 cBarsNoAsset   = -1;

struct BarsEntry
{
    std::vector<u8> meta;
    s32 assetIndex = cBarsNoAsset;
    bool isInResetTable = false;
};

struct BarsFile
{
    bool isBigEndian = false;
    u16 version = cBarsVersion12;
    std::vector<BarsEntry> entries;
    std::vector<std::vector<u8>> assets;
    std::vector<u8> trailing;
};

bool IsBars(const void* data, u64 size);
bool ReadBars(const void* data, u64 size, BarsFile* out, const char** outError);
bool WriteBars(const BarsFile& bars, std::vector<u8>* out, const char** outError);

u32 HashBarsName(const std::string& name);

}
