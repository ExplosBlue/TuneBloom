#pragma once

#include <basis/seadTypes.h>

#include <string>
#include <vector>

namespace TempWavWriter
{

std::string write(const std::vector<std::vector<float>>& channels, u32 sampleRate, bool includeLoop, u32 loopStart, u32 loopEnd, const std::string& tempPath);

}
