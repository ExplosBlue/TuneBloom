#include <ui/resource/ResourceViews.h>

#include <aal/AudioAsset.h>
#include <aal/AudioResource.h>
#include <ui/widgets/ContextMenu.h>
#include <ui/widgets/Dialogs.h>
#include <ui/export/ExportInternal.h>
#include <ui/file/File.h>
#include <ui/file/FileDrag.h>
#include <ui/file/FileDialogs.h>
#include <ui/items/ItemColumns.h>
#include <ui/items/ItemList.h>
#include <ui/widgets/ListFilter.h>
#include <messages/Messages.h>
#include <ui/player/PlayerViews.h>
#include <bfsar/model/MessageLog.h>
#include <ui/items/Selection.h>
#include <ui/file/UnsavedChanges.h>
#include <ui/wave/WaveFormatUI.h>
#include <ui/wave/WaveImportPanel.h>
#include <ui/widgets/Widgets.h>

#include <libs/IconsLucide.h>

#include <aal/Bwav.h>
#include <bfsar/audio/DecodedPcm.h>
#include <bfsar/audio/NxOpus.h>
#include <bfsar/io/FileIo.h>

#include <string>
#include <vector>

static constexpr f32 cStreamStartSeconds = 10.0f;

static const char* const cOpusOffReasons[cBwavCodecCount] = { nullptr, nullptr, messages::import::cWaveOpusStreamTip };

namespace
{

struct AssetAudioImport
{
    AudioAsset* target = nullptr;
    WaveFile::RiffWaveInfo info;
    WaveFile::Encoding encoding = WaveFile::Encoding::DspAdpcm;
    s32 bwavCodec = static_cast<s32>(aal::BwavCodec::DspAdpcm);
    u32 opusBitrate = aal::cBwavOpusChannelBitrate;
    u32 originalOpusBitrate = 0;
    bool isStream = false;
    OldAudioData oldData;
    OldAudioClear cleared;
    sead::FixedSafeString<512> fileName;
};

}

static AssetAudioImport sReplaceImport;
static AssetAudioImport sCreateImport;
static sead::FixedSafeString<512> sDroppedAssetPath;
static bool sCreateNeedsFile = true;

static std::vector<AudioAsset*> sPendingWavExports;
static std::vector<AudioAsset*> sPendingFileExports;

bool IsResourceAsset(const AudioAsset* asset)
{
    const AudioResource* resource = FindActiveAudioResource();

    if (!resource)
        return false;

    for (const Item* item : resource->getAssetList())
    {
        if (item == asset)
            return true;
    }

    return false;
}

static bool OffersOpus(const AudioAsset* target)
{
    aal::BwavCodec codec = aal::BwavCodec::DspAdpcm;

    if (!target)
        return false;

    const AudioResource* resource = target->getResource();
    const bool isLooseBwav = resource && resource->getContainer() == AudioResource::Container::Bwav;

    return isLooseBwav || target->isStream() || (GetAudioAssetBwavCodec(*target, &codec) && codec == aal::BwavCodec::Opus);
}

static WaveImportEncoding GetImportEncoding(AssetAudioImport& import, AudioAssetKind kind, const AudioAsset* target)
{
    if (kind != AudioAssetKind::Bwav)
        return MakeWaveImportEncoding(kind == AudioAssetKind::Fwav ? &import.encoding : nullptr);

    WaveImportEncoding choice;
    choice.value = &import.bwavCodec;
    choice.names = cBwavCodecNames;
    choice.details = cBwavCodecDetails;
    choice.count = static_cast<s32>(cBwavCodecCount);
    choice.reasons = (target ? OffersOpus(target) : import.isStream) ? nullptr : cOpusOffReasons;
    choice.acceptsSampleRate = &AcceptsBwavCodecSampleRate;
    choice.sampleRateReason = messages::import::cWaveOpusRateTip;
    choice.bitrate = &import.opusBitrate;
    choice.bitrateValue = static_cast<s32>(aal::BwavCodec::Opus);
    choice.originalBitrate = import.originalOpusBitrate;

    return choice;
}

static void SetStartFormat(AssetAudioImport* import, AudioAsset* asset)
{
    aal::BwavCodec codec = aal::BwavCodec::DspAdpcm;
    const WaveFile* wave = asset && asset->getInfo().kind == AudioAssetKind::Fwav ? asset->getPreviewWave() : nullptr;

    if (asset)
        GetAudioAssetBwavCodec(*asset, &codec);

    import->bwavCodec = static_cast<s32>(codec);
    import->encoding = wave ? wave->getEncoding() : WaveFile::Encoding::DspAdpcm;

    const u32 bitrate = asset ? nxopus::SnapChannelBitrate(GetAudioAssetOpusBitrate(*asset)) : 0;

    import->opusBitrate = bitrate != 0 ? nxopus::FindNearestChannelBitrate(bitrate) : aal::cBwavOpusChannelBitrate;
    import->originalOpusBitrate = nxopus::IsChannelBitratePreset(bitrate) ? bitrate : 0;
}

static bool AcceptsAnyWav(AudioAssetKind kind)
{
    return kind != AudioAssetKind::Fwav;
}

static AudioAssetKind GetCreateKind()
{
    return ActiveAudioResource().getNewAssetKind();
}

static bool AskForWav(AssetAudioImport* import, bool acceptsAnyWav, AudioAsset* formatSource)
{
    import->info.clear();
    import->fileName.clear();
    SetStartFormat(import, formatSource);

    FileFilter filters[] = {
        { messages::filedialog::cFilterWave, "*.wav" }
    };

    const bool isDropped = !sDroppedAssetPath.isEmpty();

    if (isDropped)
    {
        import->info.path = sDroppedAssetPath;
        sDroppedAssetPath.clear();
    }

    if ((!isDropped && !OpenFileDialog(&import->info.path, nullptr, IM_ARRAYSIZE(filters), filters)) || !WaveFile::readRiffWavInfo(&import->info, acceptsAnyWav))
        return false;

    fileio::GetFileStem(&import->fileName, import->info.path);
    return true;
}

static AudioAsset* CommitAudioImport(AssetAudioImport* import, AudioAsset* asset, Item* popupItem)
{
    const aal::AmtaSourceAudio source = { import->info.sampleRate, import->info.sampleCount };
    const std::string tempPath = FinalizeImportInfoForCommit(&import->info);

    if (!import->info.isLoop)
        import->info.loopStartFrame = 0;

    AudioResource& resource = ActiveAudioResource();
    const char* error = nullptr;
    AudioAsset* result = nullptr;

    AudioAssetEncoding encoding;
    encoding.fwavEncoding = import->encoding;
    encoding.bwavCodec = static_cast<aal::BwavCodec>(import->bwavCodec);
    encoding.opusChannelBitrate = import->opusBitrate;

    if (!asset)
        result = resource.createNewAsset(import->info, encoding, import->isStream, &error);
    else if (resource.replaceAudio(asset, import->info, source, encoding, import->cleared, &error))
        result = asset;

    if (!tempPath.empty())
        fileio::RemoveFile(tempPath.c_str());

    if (!result)
        MessageLog::instance()->addPopup({ error, popupItem });

    return result;
}

void RequestAudioAssetReplace(AudioAsset* asset)
{
    sReplaceImport.target = nullptr;

    if (!AskForWav(&sReplaceImport, AcceptsAnyWav(asset->getInfo().kind), asset))
        return;

    sReplaceImport.target = asset;
    sReplaceImport.oldData = ActiveAudioResource().findOldAudioData(*asset);
    sReplaceImport.cleared = OldAudioClear();
}

bool IsAudioAssetReplacePending(const AudioAsset* asset)
{
    return asset && sReplaceImport.target == asset;
}

static void ImportAssetFile(AudioAsset* asset)
{
    const AudioAssetKind kind = asset->getCompleteFileKind();
    const sead::FormatFixedSafeString<64> filterName(messages::filedialog::cFilterFileFormat, GetAudioAssetKindName(kind), GetAudioAssetKindExtension(kind));

    sead::FixedSafeString<512> path;

    if (!OpenFileDialogWithExtension(&path, filterName.cstr(), GetAudioAssetKindExtension(kind)))
        return;

    std::vector<u8> bytes;

    if (!fileio::ReadWholeFile(path.cstr(), &bytes))
    {
        MessageLog::instance()->addPopup({ messages::resource::cImportReadFailed, asset });
        return;
    }

    const char* error = nullptr;

    if (!ActiveAudioResource().replaceFile(asset, std::move(bytes), &error))
    {
        MessageLog::instance()->addPopup({ error, asset });
        return;
    }

    SetUnsavedChanges(true);
}

static void ExportPendingWavs()
{
    ExportItemsToFiles(&sPendingWavExports, { cWavExtension, messages::filedialog::cFilterWave, messages::common::cAudioAsset, messages::exporting::cSelectWavFolder },
        [](AudioAsset* asset, const sead::SafeString& path)
        {
            ActiveAudioResource().loadStreamFile(asset);
            return asset->writeWavFile(path);
        },
        &ShowWavExportResult, &ShowWavBatchExportResult);
}

static void ExportPendingFiles()
{
    if (sPendingFileExports.empty())
        return;

    const AudioAssetKind kind = sPendingFileExports.front()->getCompleteFileKind();
    const char* formatName = GetAudioAssetKindName(kind);
    const sead::FormatFixedSafeString<64> filterName(messages::filedialog::cFilterFileFormat, formatName, GetAudioAssetKindExtension(kind));

    ExportItemsToFiles(&sPendingFileExports, { GetAudioAssetKindExtension(kind), filterName.cstr(), messages::common::cAudioAsset, messages::exporting::cSelectFolder },
        [kind](AudioAsset* asset, const sead::SafeString& path)
        {
            ActiveAudioResource().loadStreamFile(asset);

            const AudioAssetFile& file = asset->getCompleteFile();

            return file && asset->getCompleteFileKind() == kind && fileio::WriteWholeFileAtomic(path.cstr(), file->data(), file->size());
        },
        [formatName](bool exported)
        {
            ShowExportResult(sead::FormatFixedSafeString<128>(exported ? messages::exporting::cFileExportedFormat : messages::exporting::cFileFailedFormat, formatName).cstr());
        },
        [formatName](s32 total, s32 failureCount)
        {
            ShowBatchExportResult(total, failureCount, formatName);
        });
}

bool CanExportAudioAssetFiles(const std::vector<AudioAsset*>& assets, const char** outReason)
{
    const AudioAssetKind kind = assets.front()->getCompleteFileKind();

    for (const AudioAsset* asset : assets)
    {
        if (!GetAudioAssetKindExtension(asset->getCompleteFileKind()))
        {
            *outReason = asset->getNoAudioReason();
            return false;
        }

        if (asset->getCompleteFileKind() != kind)
        {
            *outReason = messages::resource::cExportMixedFormats;
            return false;
        }
    }

    return true;
}

bool CanExportAudioAssetWavs(const std::vector<AudioAsset*>& assets, sead::BufferedSafeString* outReason)
{
    for (const AudioAsset* asset : assets)
    {
        if (asset->getPlaybackError(outReason))
            return false;
    }

    return true;
}

void RequestExportAudioAssetFiles(const std::vector<AudioAsset*>& assets)
{
    sPendingFileExports = assets;
}

void RequestExportAudioAssetWavs(const std::vector<AudioAsset*>& assets)
{
    sPendingWavExports = assets;
}

void PlayAudioAsset(AudioAsset* asset)
{
    ActiveAudioResource().loadStreamFile(asset);

    const WaveFile* wave = asset->getPreviewWave();

    if (wave)
    {
        sSoundPlayer.playWaveFile(*wave, -1, nullptr, 0, false);
        return;
    }

    sead::FixedSafeString<SoundPlayer::cPlaybackErrorLength> playbackError;

    if (!asset->getPlaybackError(&playbackError))
        playbackError.copy(messages::resource::cAudioDamaged);

    MessageLog::instance()->addPopup({ playbackError, asset });
}

static void AudioAssetRowActionFunc(Item* item)
{
    AudioAsset* asset = static_cast<AudioAsset*>(item);

    sead::FixedSafeString<SoundPlayer::cPlaybackErrorLength> playbackError;
    const bool canPlay = !asset->getPlaybackError(&playbackError);

    if (DrawRowPlayButton(asset->getId(), canPlay ? nullptr : playbackError.cstr(), asset->isPlaying()))
        PlayAudioAsset(asset);
}

static void AudioAssetContextMenuFunc(Item* item)
{
    if (!item)
        return;

    ContextMenuSeparator();

    AudioAsset* asset = static_cast<AudioAsset*>(item);
    const std::vector<AudioAsset*> assets = CollectForAction<AudioAsset>(item);
    const bool single = assets.size() == 1;

    const char* replaceReason = messages::resource::cSelectOneAudioAsset;
    const bool canReplace = single && ActiveAudioResource().canReplaceAudio(*asset, &replaceReason);

    if (ContextMenuItem(ICON_LC_ARROW_DOWN_UP, messages::common::cReplace, canReplace, replaceReason))
        RequestAudioAssetReplace(asset);

    if (ContextMenuItem(ICON_LC_FILE_INPUT, messages::resource::cImportAudioFile, canReplace, replaceReason))
        ImportAssetFile(asset);

    ContextMenuSeparator();

    const char* exportReason = nullptr;

    if (ContextMenuItem(ICON_LC_FILE_OUTPUT, messages::resource::cExportAudioFile, CanExportAudioAssetFiles(assets, &exportReason), exportReason))
        RequestExportAudioAssetFiles(assets);

    sead::FixedSafeString<SoundPlayer::cPlaybackErrorLength> wavReason;

    if (ContextMenuItem(ICON_LC_FILE_AUDIO, messages::menu::cExportWav, CanExportAudioAssetWavs(assets, &wavReason), wavReason.cstr()))
        RequestExportAudioAssetWavs(assets);
}

static Item* InstanciateAudioAsset()
{
    return CommitAudioImport(&sCreateImport, nullptr, nullptr);
}

static bool NeedsStream(const WaveFile::RiffWaveInfo& info)
{
    return ActiveAudioResource().canCreateStreamAsset() && GetCreateKind() == AudioAssetKind::Fwav && !IsPlainPcm(info);
}

static bool StartsAsStream(const WaveFile::RiffWaveInfo& info)
{
    const bool isLong = info.sampleRate != 0 && info.sampleCount > cStreamStartSeconds * info.sampleRate;

    return NeedsStream(info) || (ActiveAudioResource().canCreateStreamAsset() && isLong);
}

static void SetCreateStream(AssetAudioImport* import, bool isStream)
{
    import->isStream = isStream;

    if (!isStream && import->bwavCodec == static_cast<s32>(aal::BwavCodec::Opus))
        import->bwavCodec = static_cast<s32>(aal::BwavCodec::DspAdpcm);
}

static void DrawCreateFormatRow(AssetAudioImport* import)
{
    AudioResource& resource = ActiveAudioResource();

    if (!resource.canChooseNewAssetKind())
        return;

    AudioAssetKind kind = resource.getNewAssetKind();
    const char* bfwavReason = IsPlainPcm(import->info) ? nullptr : messages::resource::cAudioFormatBfwavWavReason;

    if (!DrawBarsAudioFormatRow(&kind, nullptr, bfwavReason))
        return;

    resource.setNewAssetKind(kind);
    SetCreateStream(import, StartsAsStream(import->info));
}

static void DrawStreamRow(AssetAudioImport* import)
{
    if (!ActiveAudioResource().canCreateStreamAsset())
        return;

    const bool needsStream = NeedsStream(import->info);

    DialogRow(messages::common::cStream);
    DetailTooltip(messages::resource::cNewStreamDetail);
    ImGui::BeginDisabled(needsStream);

    if (ImGui::Checkbox("##Stream", &import->isStream))
        SetCreateStream(import, import->isStream);

    ImGui::EndDisabled();

    if (needsStream)
        ImGui::SetItemTooltip("%s", messages::resource::cAudioFormatBfwavWavReason);
    else
        DetailTooltip(messages::resource::cNewStreamDetail);
}

static void AudioAssetCreateProperties(bool clear, Item* item, bool* validate)
{
    if (item || clear)
        return;

    if (validate)
    {
        *validate = sCreateImport.info.isValid;
        return;
    }

    DrawCreateFormatRow(&sCreateImport);
    DrawStreamRow(&sCreateImport);
    DrawWaveImportFormInfo(GetImportEncoding(sCreateImport, GetCreateKind(), nullptr), &sCreateImport.info);
}

static InstanciateItemCallback CreateAudioAssetFunc(bool clear)
{
    if (clear)
    {
        ResetWaveImport();
        sCreateNeedsFile = true;
    }

    const bool pickedFile = sCreateNeedsFile;

    if (sCreateNeedsFile)
    {
        sCreateNeedsFile = false;

        AudioResource& resource = ActiveAudioResource();
        const bool canChooseKind = resource.canChooseNewAssetKind();

        if (!resource.canAddAssets() || !AskForWav(&sCreateImport, canChooseKind || resource.canCreateStreamAsset() || AcceptsAnyWav(GetCreateKind()), nullptr))
            return nullptr;

        if (canChooseKind && !IsPlainPcm(sCreateImport.info))
            resource.setNewAssetKind(AudioAssetKind::Bwav);

        sCreateImport.isStream = StartsAsStream(sCreateImport.info);
    }

    if (!sCreateImport.info.isValid)
        return nullptr;

    const InstanciateItemCallback instanciate = CreateItemFunc(clear, &InstanciateAudioAsset, &AudioAssetCreateProperties);

    if (pickedFile)
        SuggestCreateItemName(sCreateImport.fileName);

    return instanciate;
}

static void DrawAssetReplacePopup()
{
    if (sReplaceImport.target && !IsResourceAsset(sReplaceImport.target))
        sReplaceImport.target = nullptr;

    if (sReplaceImport.target)
        ImGui::OpenPopup("###AssetWavImport");

    CenterNextDialog();
    ImGui::SetNextWindowContentSize(ImVec2(CalcWaveImportWidth(), 0.0f));

    if (!BeginDialog(sead::FormatFixedSafeString<64>(ICON_LC_ARROW_DOWN_UP " %s###AssetWavImport", messages::common::cReplaceAudio).cstr(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    AudioAsset* asset = sReplaceImport.target;
    WaveImportResult result = WaveImportResult::Cancel;

    if (asset)
    {
        const sead::FormatFixedSafeString<560> header(messages::import::cWaveReplaceHeaderFormat, sReplaceImport.fileName.cstr());

        SetNextWaveImportOldData(&sReplaceImport.oldData, &sReplaceImport.cleared);
        result = DrawWaveImportBody(header.cstr(), GetImportEncoding(sReplaceImport, asset->getInfo().kind, asset), &sReplaceImport.info, messages::common::cReplace);
    }

    if (result == WaveImportResult::Commit && CommitAudioImport(&sReplaceImport, asset, asset))
        SetUnsavedChanges(true);

    if (result != WaveImportResult::None)
    {
        sReplaceImport.target = nullptr;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

static bool DropAudioAssetFile(const FileDrag& drag, bool isDropped)
{
    if (!ActiveAudioResource().canAddAssets())
        return false;

    for (const std::string& path : drag.paths)
    {
        if (!IsWavPath(path))
            continue;

        if (isDropped)
            sDroppedAssetPath = path.c_str();

        return true;
    }

    return false;
}

void DrawAudioAssetsUI()
{
    AudioResource& resource = ActiveAudioResource();

    static const sead::FormatFixedSafeString<64> sAddAction(messages::item::cAddDialogTitleFormat, messages::common::cAudioAsset);

    SearchFieldExtra searchExtra;
    searchExtra.addReason = resource.canAddAssets() ? nullptr : messages::resource::cAddUnsupported;

    DrawTabFilterBar(&searchExtra);

    u32 columnCount = 0;
    const ItemColumn* columns = GetItemColumns(Item::ItemType::AudioAsset, &columnCount);

    SetNextItemListColumns(columns, columnCount);
    SetNextItemListAction(&AudioAssetRowActionFunc);
    SetNextItemListAddDialogWidth(CalcWaveImportWidth());
    SetNextItemListFixedOrder();
    SetNextItemListRemovable(resource.canRemoveAssets());
    SetNextItemListDeselectable();
    SetNextItemListFileDrop(&DropAudioAssetFile, sAddAction.cstr(), true);

    DrawAllItemsUI("Audio Asset", messages::common::cAudioAsset, resource.getAssetList(),
        resource.canAddAssets() ? &CreateAudioAssetFunc : nullptr, nullptr, &AudioAssetContextMenuFunc, GetItemFilterCallback()
    );
}

void DrawAudioResourceDialogs()
{
    DrawAssetReplacePopup();
    ExportPendingFiles();
    ExportPendingWavs();
}

void CancelAudioResourceRequests()
{
    sReplaceImport.target = nullptr;
    sPendingWavExports.clear();
    sPendingFileExports.clear();
}
