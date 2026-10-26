#include <ui/resource/ResourceViews.h>

#include <ui/file/File.h>
#include <ui/widgets/Knob.h>
#include <ui/Messages.h>
#include <ui/widgets/Properties.h>

static constexpr f32 cInfoColumnEm     = 30.0f;
static constexpr u32 cVersionShift     = 8;
static constexpr u32 cVersionMinorMask = 0xFF;

struct StreamCounts
{
    u32 streams = 0;
    u32 loaded = 0;
};

static StreamCounts CountStreams()
{
    StreamCounts counts;

    for (const Item* item : ActiveAudioResource().getAssetList())
    {
        const AudioAsset* asset = static_cast<const AudioAsset*>(item);

        if (!asset->isStream())
            continue;

        counts.streams++;

        if (asset->getStreamFileState() == StreamFileState::Loaded || asset->getStreamFileState() == StreamFileState::Found)
            counts.loaded++;
    }

    return counts;
}

static void DrawResourceFormatBox()
{
    if (!BeginPropertyBox("ResourceFormat"))
        return;

    sead::FixedSafeString<16> version;
    FormatResourceVersion(&version, ActiveAudioResource().getVersion());

    RegionHeader(messages::common::cFormat);

    PropertyRow(messages::common::cFormat);
    ImGui::TextUnformatted(GetResourceFormatInfo(ActiveAudioResource().getFormat()).displayName);

    PropertyRow(messages::common::cVersion);
    ImGui::TextUnformatted(version.cstr());

    PropertyRow(messages::innerfile::cByteOrder);
    ImGui::TextUnformatted(ActiveAudioResource().isBigEndian() ? messages::innerfile::cBigEndian : messages::innerfile::cLittleEndian);

    PropertyRow(messages::resource::cCompression, messages::resource::cCompressionDetail);
    ImGui::TextUnformatted(ActiveAudioResource().isCompressed() ? messages::resource::cCompressionZstd : messages::common::cNone);

    EndPropertyBox();
}

static void DrawResourceContentBox()
{
    if (!BeginPropertyBox("ResourceContent"))
        return;

    const StreamCounts counts = CountStreams();

    RegionHeader(messages::resource::cContentTitle);

    PropertyRow(messages::project::cRowAudioAssets);
    ImGui::Text("%u", static_cast<u32>(ActiveAudioResource().getAssetList().size()));

    if (counts.streams > 0)
    {
        PropertyRow(messages::resource::cStreams, messages::resource::cStreamsDetail);
        ImGui::Text(messages::resource::cStreamsLoadedFormat, counts.loaded, counts.streams);
    }

    EndPropertyBox();
}

void FormatResourceVersion(sead::BufferedSafeString* out, u16 version)
{
    out->format(messages::resource::cResourceVersionFormat, version >> cVersionShift, version & cVersionMinorMask);
}

void DrawAudioResourceInfoUI()
{
    const PageColumn column = BeginPageColumn(cInfoColumnEm);

    DrawResourceFormatBox();
    DrawResourceContentBox();

    EndPageColumn(column);
}
