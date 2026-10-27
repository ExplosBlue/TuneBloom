#include <ui/resource/ResourceViews.h>

#include <ui/file/File.h>
#include <ui/file/LooseFileInfo.h>
#include <ui/widgets/Knob.h>
#include <ui/Messages.h>
#include <ui/widgets/Properties.h>

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

    RegionHeader(messages::common::cFile);

    PropertyRow(messages::common::cFormat);
    ImGui::TextUnformatted(ActiveAudioResource().getContainer() == AudioResource::Container::Sarc ? messages::resource::cSarcFormat : GetResourceFormatInfo(ActiveAudioResource().getFormat()).displayName);

    PropertyRow(messages::common::cVersion);
    ImGui::TextUnformatted(version.cstr());

    PropertyRow(messages::innerfile::cByteOrder);
    ImGui::TextUnformatted(GetByteOrderText(ActiveAudioResource().isBigEndian() ? sead::Endian::eBig : sead::Endian::eLittle));

    PropertyRow(messages::resource::cCompression, messages::resource::cCompressionDetail);
    ImGui::TextUnformatted(ActiveAudioResource().isCompressed() ? messages::resource::cCompressionZstd : messages::common::cNone);

    EndPropertyBox();
}

static void DrawResourceContentBox()
{
    const StreamCounts counts = CountStreams();
    const u32 otherFiles = ActiveAudioResource().getOtherFileCount();

    if ((counts.streams == 0 && otherFiles == 0) || !BeginPropertyBox("ResourceContent"))
        return;

    RegionHeader(messages::resource::cContentTitle);

    if (counts.streams > 0)
    {
        PropertyRow(messages::common::cStreams, messages::resource::cStreamsDetail);
        ImGui::Text(messages::resource::cStreamsLoadedFormat, counts.loaded, counts.streams);
    }

    if (otherFiles > 0)
    {
        PropertyRow(messages::resource::cOtherFiles, messages::resource::cOtherFilesDetail);
        ImGui::Text("%u", otherFiles);
    }

    EndPropertyBox();
}

void FormatResourceVersion(sead::BufferedSafeString* out, u16 version)
{
    out->format(messages::resource::cResourceVersionFormat, version >> cVersionShift, version & cVersionMinorMask);
}

void DrawAudioResourceInfoUI()
{
    DrawResourceFormatBox();
    DrawResourceContentBox();
}
