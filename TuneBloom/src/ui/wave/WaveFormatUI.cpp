#include <ui/wave/WaveFormatUI.h>

#include <ui/Messages.h>

#include <bfsar/files/WaveFile.h>

#include <imgui/imgui.h>

const char* const cWaveEncodingDetails[cWaveEncodingDetailCount] = { messages::wavefile::cPcm8Detail, messages::wavefile::cPcm16Detail, messages::wavefile::cDspAdpcmDetail };

static_assert(cWaveEncodingDetailCount == IM_ARRAYSIZE(WaveFile::sEncodingTypes));

void FormatSampleTime(sead::BufferedSafeString* out, u32 samples, u32 sampleRate)
{
    u32 minutes = 0;
    f64 seconds = 0.0;
    SplitSampleTime(samples, sampleRate, &minutes, &seconds);

    out->format(messages::wavefile::cTimeFormat, minutes, seconds);
}
