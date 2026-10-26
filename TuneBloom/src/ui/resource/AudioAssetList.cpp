#include <ui/resource/ResourceViews.h>

#include <aal/AudioAsset.h>
#include <aal/AudioResource.h>
#include <ui/widgets/ContextMenu.h>
#include <ui/widgets/Dialogs.h>
#include <ui/export/ExportInternal.h>
#include <ui/file/File.h>
#include <ui/file/FileDialogs.h>
#include <ui/items/ItemList.h>
#include <ui/widgets/ListFilter.h>
#include <ui/Messages.h>
#include <ui/player/PlayerViews.h>
#include <ui/app/PopupMgr.h>
#include <ui/items/Selection.h>
#include <ui/file/UnsavedChanges.h>
#include <ui/wave/WaveImportPanel.h>
#include <ui/widgets/Widgets.h>

#include <libs/IconsLucide.h>

#include <bfsar/io/FileIo.h>

#include <string>
#include <vector>

namespace
{

struct AssetAudioImport
{
    AudioAsset* target = nullptr;
    WaveFile::RiffWaveInfo info;
    WaveFile::Encoding encoding = WaveFile::Encoding::DspAdpcm;
    sead::FixedSafeString<512> fileName;
};

}

static AssetAudioImport sReplaceImport;
static AssetAudioImport sCreateImport;
static const AudioAsset* sCreateTemplate = nullptr;
static bool sCreateNeedsFile = true;

static std::vector<AudioAsset*> sPendingWavExports;
static std::vector<AudioAsset*> sPendingFileExports;

static bool IsResourceAsset(const AudioAsset* asset)
{
    for (const Item* item : sAudioResource.getAssetList())
    {
        if (item == asset)
            return true;
    }

    return false;
}

static WaveFile::Encoding* GetImportEncoding(AssetAudioImport& import, const AudioAsset& asset)
{
    return asset.getInfo().kind == AudioAssetKind::Fwav ? &import.encoding : nullptr;
}

static bool AskForWav(AssetAudioImport* import)
{
    import->info.clear();
    import->fileName.clear();
    import->encoding = WaveFile::Encoding::DspAdpcm;

    FileFilter filters[] = {
        { messages::filedialog::cFilterWave, "*.wav" }
    };

    if (!OpenFileDialog(&import->info.path, nullptr, IM_ARRAYSIZE(filters), filters) || !WaveFile::readRiffWavInfo(&import->info))
        return false;

    GetFileStem(&import->fileName, import->info.path);
    return true;
}

static bool CommitAudioImport(AssetAudioImport* import, AudioAsset* asset, Item* popupItem)
{
    const std::string tempPath = FinalizeImportInfoForCommit(&import->info);

    if (!import->info.isLoop)
        import->info.loopStartFrame = 0;

    const char* error = nullptr;
    const bool replaced = sAudioResource.replaceAudio(asset, import->info, import->encoding, &error);

    if (!tempPath.empty())
        fileio::RemoveFile(tempPath.c_str());

    if (!replaced)
        PopupMgr::instance()->addPopup({ error, popupItem });

    return replaced;
}

static void RequestAssetReplace(AudioAsset* asset)
{
    sReplaceImport.target = nullptr;

    if (AskForWav(&sReplaceImport))
        sReplaceImport.target = asset;
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
        PopupMgr::instance()->addPopup({ messages::resource::cImportReadFailed, asset });
        return;
    }

    const char* error = nullptr;

    if (!sAudioResource.replaceFile(asset, std::move(bytes), &error))
    {
        PopupMgr::instance()->addPopup({ error, asset });
        return;
    }

    SetUnsavedChanges(true);
}

static void ExportPendingWavs()
{
    ExportItemsToFiles(&sPendingWavExports, { cWavExtension, messages::filedialog::cFilterWave, messages::common::cAudioAsset, messages::exporting::cSelectWavFolder },
        [](AudioAsset* asset, const sead::SafeString& path)
        {
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
        [](AudioAsset* asset, const sead::SafeString& path)
        {
            const AudioAssetFile& file = asset->getCompleteFile();

            return file && fileio::WriteWholeFileAtomic(path.cstr(), file->data(), file->size());
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

static bool CanExportFiles(const std::vector<AudioAsset*>& assets, const char** outReason)
{
    const AudioAssetKind kind = assets.front()->getCompleteFileKind();

    for (const AudioAsset* asset : assets)
    {
        if (!GetAudioAssetKindExtension(asset->getCompleteFileKind()) || !asset->getFile())
        {
            *outReason = messages::resource::cNoAudioFile;
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

static bool CanExportWavs(const std::vector<AudioAsset*>& assets, sead::BufferedSafeString* outReason)
{
    for (const AudioAsset* asset : assets)
    {
        if (asset->getPlaybackError(outReason))
            return false;
    }

    return true;
}

static void PlayAsset(AudioAsset* asset)
{
    sAudioResource.loadStreamFile(asset);

    const WaveFile* wave = asset->getPreviewWave();

    if (wave)
    {
        sSoundPlayer.playWaveFile(*wave, -1, nullptr, 0, false);
        return;
    }

    sead::FixedSafeString<SoundPlayer::cPlaybackErrorLength> playbackError;

    if (!asset->getPlaybackError(&playbackError))
        playbackError.copy(messages::resource::cAudioDamaged);

    PopupMgr::instance()->addPopup({ playbackError, asset });
}

static void AudioAssetRowActionFunc(Item* item)
{
    AudioAsset* asset = static_cast<AudioAsset*>(item);

    sead::FixedSafeString<SoundPlayer::cPlaybackErrorLength> playbackError;
    const bool canPlay = !asset->getPlaybackError(&playbackError);

    if (DrawRowPlayButton(asset->getId(), canPlay ? nullptr : playbackError.cstr(), asset->isPlaying()))
        PlayAsset(asset);
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
    const bool canReplace = single && sAudioResource.canReplaceAudio(*asset, &replaceReason);

    if (ContextMenuItem(ICON_LC_REPLACE, messages::common::cReplace, canReplace, replaceReason))
        RequestAssetReplace(asset);

    if (ContextMenuItem(ICON_LC_FILE_INPUT, messages::resource::cImportAudioFile, canReplace, replaceReason))
        ImportAssetFile(asset);

    ContextMenuSeparator();

    const char* exportReason = nullptr;

    if (ContextMenuItem(ICON_LC_FILE_OUTPUT, messages::resource::cExportAudioFile, CanExportFiles(assets, &exportReason), exportReason))
        sPendingFileExports = assets;

    sead::FixedSafeString<SoundPlayer::cPlaybackErrorLength> wavReason;

    if (ContextMenuItem(ICON_LC_FILE_AUDIO, messages::menu::cExportWav, CanExportWavs(assets, &wavReason), wavReason.cstr()))
        sPendingWavExports = assets;
}

static Item* InstanciateAudioAsset()
{
    if (!sCreateTemplate || !IsResourceAsset(sCreateTemplate))
        return nullptr;

    AudioAsset* asset = sAudioResource.createAsset(*sCreateTemplate);

    if (!CommitAudioImport(&sCreateImport, asset, nullptr))
    {
        delete asset;
        return nullptr;
    }

    return asset;
}

static void AudioAssetCreateProperties(bool clear, Item* item, bool* validate)
{
    if (item || clear || !sCreateTemplate)
        return;

    if (validate)
    {
        *validate = sCreateImport.info.isValid;
        return;
    }

    DrawWaveImportInfo(GetImportEncoding(sCreateImport, *sCreateTemplate), &sCreateImport.info, sead::FormatFixedSafeString<560>(messages::import::cWaveHeaderFormat, sCreateImport.fileName.cstr()).cstr());
}

static InstanciateItemCallback CreateAudioAssetFunc(bool clear)
{
    if (clear)
    {
        ResetWaveImport();
        sCreateTemplate = sAudioResource.findTemplateAsset(sSelectedItem);
        sCreateNeedsFile = true;
    }

    const bool pickedFile = sCreateNeedsFile;

    if (sCreateNeedsFile)
    {
        sCreateNeedsFile = false;

        if (!sCreateTemplate || !AskForWav(&sCreateImport))
            return nullptr;
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

    if (!BeginDialog(sead::FormatFixedSafeString<64>(ICON_LC_REPLACE " %s###AssetWavImport", messages::resource::cReplaceAudioTitle).cstr(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    AudioAsset* asset = sReplaceImport.target;
    WaveImportResult result = WaveImportResult::Cancel;

    if (asset)
    {
        const sead::FormatFixedSafeString<560> header(messages::import::cWaveReplaceHeaderFormat, sReplaceImport.fileName.cstr());

        result = DrawWaveImportBody(header.cstr(), GetImportEncoding(sReplaceImport, *asset), &sReplaceImport.info, messages::common::cReplace);
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

void DrawAudioAssetsUI()
{
    SetNextItemListAction(&AudioAssetRowActionFunc);
    SetNextItemListAddDialogWidth(CalcWaveImportWidth());
    SetNextItemListFixedOrder();
    SetNextItemListRemovable(sAudioResource.canRemoveAssets());

    DrawAllItemsUI("Audio Asset", messages::common::cAudioAsset, sAudioResource.getAssetList(),
        sAudioResource.canAddAssets() ? &CreateAudioAssetFunc : nullptr, nullptr, &AudioAssetContextMenuFunc, GetItemFilterCallback()
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
    sCreateTemplate = nullptr;
    sPendingWavExports.clear();
    sPendingFileExports.clear();
}
