#pragma once

#include <cstddef>
#include <cstdint>

namespace timeutil
{

using u32 = uint32_t;
double SamplesToSeconds(u32 samples, u32 sampleRate);
void FormatClock(char* buffer, size_t bufferSize, double seconds);
void FormatTimeLabel(char* buffer, size_t bufferSize, double seconds, double majorStepSeconds);

}
