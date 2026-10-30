#pragma once

#include <basis/seadTypes.h>

#include <string>
#include <vector>

namespace aal
{

inline constexpr char cBarsParamFileName[] = "_param.bin";
inline constexpr u32 cBarsParamTextCount = 9;
inline constexpr u32 cBarsParamValueCount = 12;

enum class BarsParamText : u32
{
    Sound = 0,
    File = 1,
    StreamFile = 2
};

struct BarsParamRecord
{
    std::string texts[cBarsParamTextCount];
    u32 values[cBarsParamValueCount] = {};

    std::string& text(BarsParamText field)
    {
        return texts[static_cast<u32>(field)];
    }

    const std::string& text(BarsParamText field) const
    {
        return texts[static_cast<u32>(field)];
    }

    u32 getId() const
    {
        return values[0];
    }

    bool operator==(const BarsParamRecord&) const = default;
};

struct BarsParam
{
    bool isBigEndian = true;
    std::vector<BarsParamRecord> records;
};

bool ReadBarsParam(const std::vector<u8>& data, BarsParam* out);
std::vector<u8> WriteBarsParam(const BarsParam& param);

}
