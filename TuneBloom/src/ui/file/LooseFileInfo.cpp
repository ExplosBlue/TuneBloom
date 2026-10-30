#include <ui/file/LooseFileInfo.h>

#include <messages/Messages.h>
#include <ui/widgets/Properties.h>

#include <bfsar/files/WaveFile.h>

#include <imgui/imgui.h>

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

void FormatLooseFileFacts(sead::BufferedSafeString* out, const LooseFileFormat& format)
{
    out->copy(format.name);

    if (!format.version.isEmpty())
        out->appendWithFormat(" %s", format.version.cstr());
}

void AppendWaveFacts(sead::BufferedSafeString* out, const WaveFile& wave)
{
    AppendFileFact(out, WaveFile::sEncodingTypes[static_cast<s32>(wave.getEncoding())]);
    AppendRateFact(out, wave.getSampleRate());
}

void AppendRateFact(sead::BufferedSafeString* out, u32 sampleRate)
{
    AppendFileFact(out, sead::FormatFixedSafeString<32>(messages::unit::cHertzFormat, sampleRate).cstr());
}

void AppendFileFact(sead::BufferedSafeString* out, const char* fact)
{
    if (!out->isEmpty())
        out->append(messages::playerframe::cFactSeparator);

    out->append(fact);
}

static void AppendCountFact(sead::BufferedSafeString* out, u32 count, const char* oneFormat, const char* manyFormat)
{
    AppendFileFact(out, sead::FormatFixedSafeString<32>(count == 1 ? oneFormat : manyFormat, count).cstr());
}

void AppendTrackFact(sead::BufferedSafeString* out, u32 trackNum)
{
    AppendCountFact(out, trackNum, messages::common::cTrackCountOneFormat, messages::common::cTrackCountFormat);
}

void AppendChannelFact(sead::BufferedSafeString* out, u32 channelNum)
{
    AppendCountFact(out, channelNum, messages::common::cChannelCountOneFormat, messages::common::cChannelCountFormat);
}
