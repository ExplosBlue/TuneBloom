#include <ui/wave/WaveFormatUI.h>

#include <messages/Messages.h>
#include <ui/widgets/Widgets.h>

#include <aal/Bwav.h>
#include <bfsar/audio/NxOpus.h>
#include <bfsar/files/WaveFile.h>

#include <imgui/imgui.h>

#include <algorithm>
#include <cstdio>
#include <iterator>

const char* const cWaveEncodingDetails[cWaveEncodingDetailCount] = { messages::wavefile::cPcm8Detail, messages::wavefile::cPcm16Detail, messages::wavefile::cDspAdpcmDetail };

static_assert(cWaveEncodingDetailCount == IM_ARRAYSIZE(WaveFile::sEncodingTypes));

const char* const cBwavCodecNames[cBwavCodecCount] = { messages::wavefile::cEncodingPcm16, messages::wavefile::cEncodingDspAdpcm, messages::stream::cTypeOpus };
const char* const cBwavCodecDetails[cBwavCodecCount] = { messages::wavefile::cPcm16Detail, messages::wavefile::cDspAdpcmDetail, messages::wavefile::cOpusDetail };

bool AcceptsBwavCodecSampleRate(s32 codec, u32 sampleRate)
{
    return codec != static_cast<s32>(aal::BwavCodec::Opus) || nxopus::IsSupportedSampleRate(sampleRate);
}

static void FormatBitrate(char* buffer, size_t size, u32 bitrate, bool isOriginal)
{
    snprintf(buffer, size, isOriginal ? messages::import::cWaveBitrateOriginalFormat : messages::unit::cKilobitsFormat, bitrate / 1000);
}

bool DrawOpusBitrateCombo(const char* id, u32* bitrate, u32 originalBitrate, bool isUsed)
{
    const s32 bitrateNum = IM_ARRAYSIZE(nxopus::cChannelBitrates);
    const s32 found = static_cast<s32>(std::find(std::begin(nxopus::cChannelBitrates), std::end(nxopus::cChannelBitrates), *bitrate) - std::begin(nxopus::cChannelBitrates));
    const s32 oldIndex = found < bitrateNum ? found : -1;
    s32 current = oldIndex;

    char preview[40];

    if (isUsed)
        FormatBitrate(preview, sizeof(preview), *bitrate, *bitrate == originalBitrate);
    else
        snprintf(preview, sizeof(preview), "%s", messages::common::cNoValue);

    ImGui::BeginDisabled(!isUsed);

    if (ImGui::BeginCombo(id, preview))
    {
        for (s32 i = 0; i < bitrateNum; i++)
        {
            char label[40];
            FormatBitrate(label, sizeof(label), nxopus::cChannelBitrates[i], nxopus::cChannelBitrates[i] == originalBitrate);

            if (ImGui::Selectable(label, i == current))
                current = i;
        }

        ImGui::EndCombo();
    }

    ImGui::EndDisabled();

    const s32 wheelSteps = isUsed ? GetItemWheelSteps() : 0;

    if (wheelSteps != 0)
        current = std::clamp(std::max(current, 0) + wheelSteps, 0, bitrateNum - 1);

    if (current == oldIndex)
        return false;

    *bitrate = nxopus::cChannelBitrates[current];
    return true;
}

void FormatSampleTime(sead::BufferedSafeString* out, u32 samples, u32 sampleRate)
{
    u32 minutes = 0;
    f64 seconds = 0.0;
    SplitSampleTime(samples, sampleRate, &minutes, &seconds);

    out->format(messages::wavefile::cTimeFormat, minutes, seconds);
}
