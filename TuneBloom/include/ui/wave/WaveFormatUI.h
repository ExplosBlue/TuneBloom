#pragma once

#include <ui/Messages.h>

#include <basis/seadTypes.h>
#include <prim/seadSafeString.h>

inline constexpr const char* cWaveEncodingDetails[] = { messages::wavefile::cPcm8Detail, messages::wavefile::cPcm16Detail, messages::wavefile::cDspAdpcmDetail };

inline constexpr u32 cImportSampleRates[] = { 8000, 11025, 16000, 22050, 32000, 44100, 48000 };

inline void SplitSampleTime(u32 samples, u32 sampleRate, u32* outMinutes, f64* outSeconds)
{
    const f64 seconds = sampleRate > 0 ? static_cast<f64>(samples) / sampleRate : 0.0;

    *outMinutes = static_cast<u32>(seconds / 60.0);
    *outSeconds = seconds - *outMinutes * 60.0;
}

inline void FormatSampleTime(sead::BufferedSafeString* out, u32 samples, u32 sampleRate)
{
    u32 minutes = 0;
    f64 seconds = 0.0;
    SplitSampleTime(samples, sampleRate, &minutes, &seconds);

    out->format(messages::wavefile::cTimeFormat, minutes, seconds);
}
