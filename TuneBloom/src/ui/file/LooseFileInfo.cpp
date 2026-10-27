#include <ui/file/LooseFileInfo.h>

#include <ui/Messages.h>
#include <ui/widgets/Properties.h>

#include <bfsar/files/WaveFile.h>

#include <imgui/imgui.h>

const char* GetByteOrderText(sead::Endian::Types endian)
{
    return endian == sead::Endian::eBig ? messages::innerfile::cBigEndian : messages::innerfile::cLittleEndian;
}

void DrawLooseFileRows(const InnerFileTarget& target, InnerFileKind kind)
{
    sead::FixedSafeString<24> version;
    FormatInnerFileVersion(&version, target.version, target.format);

    PropertyRow(messages::common::cFormat);
    ImGui::TextUnformatted(GetInnerFileDisplayName(target.format, kind));

    PropertyRow(messages::common::cVersion);
    ImGui::TextUnformatted(version.cstr());

    PropertyRow(messages::innerfile::cByteOrder);
    ImGui::TextUnformatted(GetByteOrderText(target.endian));
}

void FormatLooseFileFacts(sead::BufferedSafeString* out, const InnerFileTarget& target, InnerFileKind kind)
{
    sead::FixedSafeString<24> version;
    FormatInnerFileVersion(&version, target.version, target.format);

    out->format("%s %s", GetInnerFileDisplayName(target.format, kind), version.cstr());
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
    AppendCountFact(out, trackNum, messages::stream::cReloadedTracksOneFormat, messages::stream::cReloadedTracksFormat);
}

void AppendChannelFact(sead::BufferedSafeString* out, u32 channelNum)
{
    AppendCountFact(out, channelNum, messages::common::cChannelCountOneFormat, messages::common::cChannelCountFormat);
}
