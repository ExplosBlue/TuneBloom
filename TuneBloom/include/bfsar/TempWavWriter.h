#pragma once

#include <cstdint>
#include <cstdio>
#include <vector>
#include <string>

namespace TempWavWriter {

using u32 = uint32_t;

std::string write(const std::vector<std::vector<float>>& channels, u32 sampleRate, bool includeLoop, u32 loopStart, u32 loopEnd, const std::string& tempPath);

}