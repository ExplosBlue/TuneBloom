#include <ui/resource/ResourceViews.h>

#include <ui/file/File.h>
#include <ui/file/LooseFileInfo.h>
#include <ui/widgets/Dialogs.h>
#include <ui/widgets/Knob.h>
#include <ui/Messages.h>
#include <ui/widgets/Properties.h>
#include <ui/widgets/Widgets.h>

#include <aal/AudioAsset.h>

static constexpr u32 cVersionShift     = 8;
static constexpr u32 cVersionMinorMask = 0xFF;

static constexpr AudioAssetKind cBarsAudioKinds[] = { AudioAssetKind::Fwav, AudioAssetKind::Bwav };

static const char* const cBarsAudioDetails[] = {
    messages::resource::cAudioFormatBfwavDetail,
    messages::resource::cAudioFormatBwavDetail
};

static constexpr s32 cBarsAudioNum = IM_ARRAYSIZE(cBarsAudioKinds);

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

    const AudioResource& resource = ActiveAudioResource();

    LooseFileFormat format;
    format.name = resource.getContainer() == AudioResource::Container::Sarc ? messages::resource::cSarcFormat : GetResourceFormatInfo(resource.getFormat()).displayName;
    format.endian = resource.isBigEndian() ? sead::Endian::eBig : sead::Endian::eLittle;
    FormatResourceVersion(&format.version, resource.getVersion());

    RegionHeader(messages::common::cFile);
    DrawLooseFileRows(format);

    PropertyRow(messages::resource::cCompression, messages::resource::cCompressionDetail);
    ImGui::TextUnformatted(resource.isCompressed() ? messages::resource::cCompressionZstd : messages::common::cNone);

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

static s32 FindBarsAudioIndex(AudioAssetKind kind)
{
    for (s32 i = 0; i < cBarsAudioNum; i++)
    {
        if (cBarsAudioKinds[i] == kind)
            return i;
    }

    return 0;
}

bool DrawBarsAudioFormatRow(AudioAssetKind* kind, const char* fixedReason, const char* bfwavReason)
{
    const char* labels[cBarsAudioNum];

    for (s32 i = 0; i < cBarsAudioNum; i++)
        labels[i] = GetAudioAssetKindName(cBarsAudioKinds[i]);

    const char* const reasons[cBarsAudioNum] = { bfwavReason, nullptr };
    s32 shown = FindBarsAudioIndex(fixedReason ? AudioAssetKind::Bwav : *kind);

    DialogRow(messages::resource::cAudioFormat);
    DetailTooltip(messages::resource::cAudioFormatDetail);
    ImGui::BeginDisabled(fixedReason != nullptr);

    const bool isChanged = ComboScroll("##BarsAudio", &shown, labels, cBarsAudioDetails, reasons, cBarsAudioNum) && !fixedReason;

    ImGui::EndDisabled();

    if (fixedReason)
        ImGui::SetItemTooltip("%s", fixedReason);

    if (isChanged)
        *kind = cBarsAudioKinds[shown];

    return isChanged;
}

void DrawAudioResourceInfoUI()
{
    DrawResourceFormatBox();
    DrawResourceContentBox();
}
