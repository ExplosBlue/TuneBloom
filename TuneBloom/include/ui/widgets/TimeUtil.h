#pragma once

#include <cstddef>
#include <cstdint>

namespace timeutil
{

using u32 = uint32_t;
double SamplesToSeconds(u32 samples, u32 sampleRate);
void FormatClock(char* buffer, size_t bufferSize, double seconds);
void FormatTimeLabel(char* buffer, size_t bufferSize, double seconds, double majorStepSeconds);
void SetTwelveHourClock(bool twelveHour);
void FormatTimeOfDay(char* buffer, size_t bufferSize, int64_t time);
void FormatTimeOfDay(char* buffer, size_t bufferSize, int64_t time, bool twelveHour);
void FormatDate(char* buffer, size_t bufferSize, int64_t time);
void FormatFullDate(char* buffer, size_t bufferSize, int64_t time);
void FormatRelativeDate(char* buffer, size_t bufferSize, int64_t time);

}
