#include <ui/TimeUtil.h>

#include <cstdio>

namespace timeutil
{

double SamplesToSeconds(u32 samples, u32 sampleRate)
{
    return sampleRate != 0 ? static_cast<double>(samples) / static_cast<double>(sampleRate) : 0.0;
}

void FormatClock(char* buffer, size_t bufferSize, double seconds)
{
    if (seconds < 0.0)
        seconds = 0.0;

    const u32 minutes = static_cast<u32>(seconds) / 60;
    const u32 secs = static_cast<u32>(seconds) % 60;
    const u32 millis = static_cast<u32>(seconds * 1000.0) % 1000;

    snprintf(buffer, bufferSize, "%02u:%02u.%03u", minutes, secs, millis);
}

void FormatTimeLabel(char* buffer, size_t bufferSize, double seconds, double majorStepSeconds)
{
    const int decimals = majorStepSeconds < 0.001 ? 4 : majorStepSeconds < 0.01 ? 3 : 2;

    if (seconds >= 60.0)
    {
        const int minutes = static_cast<int>(seconds / 60.0);
        snprintf(buffer, bufferSize, "%d:%0*.*f", minutes, decimals + 3, decimals, seconds - minutes * 60.0);
    }
    else
    {
        snprintf(buffer, bufferSize, "%.*f", decimals, seconds);
    }
}

}
