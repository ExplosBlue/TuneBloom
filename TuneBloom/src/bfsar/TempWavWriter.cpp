#include <bfsar/TempWavWriter.h>
#include <algorithm>
#include <cstring>
#include <cmath>

namespace TempWavWriter {

static void writeU32LE(std::vector<uint8_t>& buf, uint32_t v)
{
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}
static void writeU16LE(std::vector<uint8_t>& buf, uint16_t v)
{
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}
static void writeTag(std::vector<uint8_t>& buf, const char* tag)
{
    buf.push_back(tag[0]); buf.push_back(tag[1]); buf.push_back(tag[2]); buf.push_back(tag[3]);
}

std::string write(const std::vector<std::vector<float>>& channels, u32 sampleRate, bool includeLoop, u32 loopStart, u32 loopEnd, const std::string& tempPath)
{
    if (channels.empty() || channels[0].empty() || sampleRate == 0)
        return std::string();

    const u32 numChannels = static_cast<u32>(channels.size());
    const u32 numFrames = static_cast<u32>(channels[0].size());
    const u32 bitsPerSample = 16;
    const u32 blockAlign = numChannels * (bitsPerSample / 8);
    const u32 byteRate = sampleRate * blockAlign;
    const u32 dataBytes = numFrames * blockAlign;

    std::vector<uint8_t> out;
    out.reserve(44 + dataBytes + 68);

    writeTag(out, "RIFF");
    writeU32LE(out, 0);
    writeTag(out, "WAVE");

    writeTag(out, "fmt ");
    writeU32LE(out, 16);
    writeU16LE(out, 1);
    writeU16LE(out, static_cast<uint16_t>(numChannels));
    writeU32LE(out, sampleRate);
    writeU32LE(out, byteRate);
    writeU16LE(out, static_cast<uint16_t>(blockAlign));
    writeU16LE(out, static_cast<uint16_t>(bitsPerSample));

    writeTag(out, "data");
    writeU32LE(out, dataBytes);
    for (u32 i = 0; i < numFrames; i++)
    {
        for (u32 ch = 0; ch < numChannels; ch++)
        {
            float v = channels[ch][i];
            v = v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
            const float scale = v < 0.0f ? 32768.0f : 32767.0f;

            int16_t s = static_cast<int16_t>(std::lround(v * scale));
            writeU16LE(out, static_cast<uint16_t>(s));
        }
    }
    if (dataBytes % 2 != 0)
        out.push_back(0);

    if (includeLoop)
    {
        writeTag(out, "smpl");
        writeU32LE(out, 60);

        for (int i = 0; i < 28; i++) out.push_back(0);

        writeU32LE(out, 1);
        writeU32LE(out, 0);

        writeU32LE(out, 0);
        writeU32LE(out, 0);
        writeU32LE(out, loopStart);
        writeU32LE(out, loopEnd);
        writeU32LE(out, 0);
        writeU32LE(out, 0);
    }

    const uint32_t riffSize = static_cast<uint32_t>(out.size()) - 8;
    out[4] = static_cast<uint8_t>(riffSize & 0xFF);
    out[5] = static_cast<uint8_t>((riffSize >> 8) & 0xFF);
    out[6] = static_cast<uint8_t>((riffSize >> 16) & 0xFF);
    out[7] = static_cast<uint8_t>((riffSize >> 24) & 0xFF);

    FILE* f = fopen(tempPath.c_str(), "wb");
    if (!f)
        return std::string();
    fwrite(out.data(), 1, out.size(), f);
    fclose(f);

    return tempPath;
}

}
