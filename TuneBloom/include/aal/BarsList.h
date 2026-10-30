#pragma once

#include <basis/seadTypes.h>

#include <string>
#include <vector>

namespace aal
{

inline constexpr char cBarsListFileExtension[] = "barslist";
inline constexpr u16 cBarsListVersion          = 0x0001;

struct BarsList
{
    bool isBigEndian = false;
    u16 version = cBarsListVersion;
    u32 reserved = 0;
    std::string name;
    std::vector<std::string> entries;
};

bool IsBarsList(const void* data, u64 size);
bool ReadBarsList(const void* data, u64 size, BarsList* out);
std::vector<u8> WriteBarsList(const BarsList& list);

}
