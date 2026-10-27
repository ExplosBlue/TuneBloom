#pragma once

#include <aal/AmtaLoudness.h>

#include <basis/seadTypes.h>

#include <string>
#include <vector>

namespace aal
{

inline constexpr u16 cAmtaVersion1 = 0x0100;
inline constexpr u16 cAmtaVersion4 = 0x0400;
inline constexpr u16 cAmtaVersion5 = 0x0500;

struct AmtaWaveFields
{
    u32 sampleRate = 0;
    u32 storedSampleCount = 0;
    u32 sampleCount = 0;
    u32 channelCount = 0;
    bool isLoop = false;
    u32 loopStartFrame = 0;
};

bool IsAmta(const void* data, u64 size);
u32 MeasureAmta(const void* data, u64 size);
u16 GetAmtaVersion(const std::vector<u8>& meta);

bool ReadAmtaName(const std::vector<u8>& meta, std::string* outName);
bool RenameAmta(std::vector<u8>* meta, const std::string& name);
bool UpdateAmtaWaveFields(std::vector<u8>* meta, const AmtaWaveFields& fields);
bool HasAmtaLoudness(const std::vector<u8>& meta);
bool WriteAmtaLoudness(std::vector<u8>* meta, const AmtaLoudness& loudness);

}
