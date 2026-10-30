#include <ui/file/LooseFileInfo.h>

#include <messages/Messages.h>
#include <ui/widgets/Properties.h>

#include <bfsar/files/WaveFile.h>

#include <imgui/imgui.h>

static constexpr u32 cAdpcmFrameBits = 64;

const char* GetByteOrderText(sead::Endian::Types endian)
{
    return endian == sead::Endian::eBig ? messages::innerfile::cBigEndian : messages::innerfile::cLittleEndian;
}

LooseFileFormat GetLooseFileFormat(const InnerFileTarget& target, InnerFileKind kind)
{
    LooseFileFormat format;
    format.name = GetInnerFileDisplayName(target.format, kind);
    format.extension = GetInnerFileExtension(target.format, kind);
    format.endian = target.endian;

    FormatInnerFileVersion(&format.version, target.version, target.format);

    return format;
}

void DrawLooseFileRows(const LooseFileFormat& format)
{
    PropertyRow(messages::common::cFormat);
    ImGui::TextUnformatted(format.name);

    if (!format.version.isEmpty())
    {
        PropertyRow(messages::common::cVersion);
        ImGui::TextUnformatted(format.version.cstr());
    }

    PropertyRow(messages::innerfile::cByteOrder);
    ImGui::TextUnformatted(GetByteOrderText(format.endian));
}

void SetWaveFacts(FileFacts* facts, const WaveFile& wave)
{
    facts->encoding = WaveFile::sEncodingTypes[static_cast<s32>(wave.getEncoding())];
    facts->sampleRate = wave.getSampleRate();
    facts->sampleCount = wave.getSampleCount();
    facts->channelNum = static_cast<u32>(wave.getChannels().size());
    facts->bitrate = CalcWaveBitrate(wave, facts->channelNum);
    facts->isLoop = wave.getIsLoop();
    facts->loopStartFrame = wave.getOriginalLoopStartFrame();
    facts->loopEndFrame = wave.getOriginalLoopEndFrame();
}

u32 CalcWaveBitrate(const WaveFile& wave, u32 channelNum)
{
    switch (wave.getEncoding())
    {
        case WaveFile::Encoding::Pcm8:
            return CalcPcmBitrate(wave.getSampleRate(), channelNum, 8);
        case WaveFile::Encoding::Pcm16:
            return CalcPcmBitrate(wave.getSampleRate(), channelNum, 16);
        case WaveFile::Encoding::DspAdpcm:
            return CalcAdpcmBitrate(wave.getSampleRate(), channelNum);
    }

    return 0;
}

u32 CalcAdpcmBitrate(u32 sampleRate, u32 channelNum)
{
    return static_cast<u32>(static_cast<u64>(sampleRate) * channelNum * cAdpcmFrameBits / WaveFile::cSamplesPerFrame);
}

u32 CalcPcmBitrate(u32 sampleRate, u32 channelNum, u32 bitsPerSample)
{
    return sampleRate * channelNum * bitsPerSample;
}

static void AppendFact(sead::BufferedSafeString* out, const char* fact)
{
    if (!out->isEmpty())
        out->append(messages::playerframe::cFactSeparator);

    out->append(fact);
}

void FormatFileFactsLine(sead::BufferedSafeString* out, const FileFacts& facts)
{
    out->copy(facts.format.name);

    if (!facts.encoding.isEmpty())
        AppendFact(out, facts.encoding.cstr());

    if (facts.bitrate != 0)
        AppendFact(out, sead::FormatFixedSafeString<32>(messages::unit::cKilobitsFormat, (facts.bitrate + 500) / 1000).cstr());

    if (facts.sampleRate != 0)
        AppendFact(out, sead::FormatFixedSafeString<32>(messages::unit::cHertzFormat, facts.sampleRate).cstr());
}
