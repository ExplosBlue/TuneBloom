#include <ui/project/FileLists.h>

#include <ui/bank/BankBundle.h>
#include <ui/widgets/ContextMenu.h>
#include <ui/widgets/Dialogs.h>
#include <ui/export/Export.h>
#include <ui/file/File.h>
#include <ui/file/FileDrag.h>
#include <ui/file/FileDialogs.h>
#include <ui/items/ItemList.h>
#include <ui/widgets/ListFilter.h>
#include <messages/Messages.h>
#include <ui/app/Notifications.h>
#include <ui/player/PlayerViews.h>
#include <bfsar/model/MessageLog.h>
#include <ui/items/Selection.h>
#include <ui/stream/OpusBitrate.h>
#include <ui/stream/StreamRegions.h>
#include <ui/settings/UiColors.h>
#include <ui/file/UnsavedChanges.h>
#include <ui/wave/WaveImportPanel.h>
#include <ui/widgets/Widgets.h>

#include <libs/IconsLucide.h>

#include <aal/Bwav.h>
#include <bfsar/audio/WaveFileEditDecode.h>

#include <filedevice/seadFileDeviceMgr.h>

#include <bfsar/files/BankFile.h>
#include <bfsar/files/BfstmFile.h>

#include <bfsar/audio/DecodedPcm.h>
#include <bfsar/io/FileIo.h>

#include <vector>

sead::FixedSafeString<512> sDroppedWavPath;
Item* sWavDropTargetVel = nullptr;

static sead::FixedSafeString<512> sDroppedAddWavePath;

static const std::string* FindDroppedWave(const FileDrag& drag)
{
    for (const std::string& path : drag.paths)
    {
        if (IsWavPath(path) || MatchesInnerFileExtension(path.c_str(), InnerFileKind::Wave))
            return &path;
    }

    return nullptr;
}

static bool DropWaveFile(const FileDrag& drag, bool isDropped)
{
    const std::string* path = FindDroppedWave(drag);

    if (path && isDropped)
        sDroppedAddWavePath = path->c_str();

    return path != nullptr;
}

static void ImportNativeWaveFile(const sead::SafeString& path, const sead::SafeString& name)
{
    Bfsar& archive = ActiveSoundArchive();

    sead::FileDevice* device = sead::FileDeviceMgr::instance()->findDevice("native");
    if (!device)
    {
        MessageLog::instance()->addPopup({messages::import::cNativeDeviceUnavailable, nullptr});
        return;
    }

    sead::FileDevice::LoadArg arg;
    arg.path = path;
    u8* fileData = device->tryLoad(arg);

    if (fileData && arg.read_size < sizeof(nw::ut::BinaryFileHeader))
    {
        device->unload(fileData);
        fileData = nullptr;
    }

    if (!fileData)
    {
        MessageLog::instance()->addPopup({messages::import::cWaveLoadFailed, nullptr});
        return;
    }

    PatchWaveFileHeader(fileData);

    WaveFile* newWave = new WaveFile();
    newWave->setArchive(&archive);
    newWave->setEnableName(true);
    newWave->getName() = name;

    if (!newWave->read(fileData))
    {
        delete newWave;
        device->unload(fileData);
        MessageLog::instance()->addPopup({messages::import::cWaveReadFailed, nullptr});
        return;
    }

    newWave->setFormat(archive.getFormat());
    newWave->setVersion(archive.getVersionForBfwav());
    newWave->refreshSerializedHash();
    device->unload(fileData);

    AddFileToList(archive.getWaveFileList(), newWave);
}

InstanciateItemCallback CreateWaveFileFunc(bool clear)
{
    static WaveFile::RiffWaveInfo sRiffWaveInfo;
    static sead::FixedSafeString<512> sFileName;
    static bool sAskForPath = true;
    static WaveFile::Encoding sEncoding = WaveFile::Encoding::DspAdpcm;

    if (clear)
    {
        sRiffWaveInfo.clear();
        sFileName.clear();
        sAskForPath = true;
        sEncoding = WaveFile::Encoding::DspAdpcm;
        ResetWaveImport();
    }

    if (sAskForPath)
    {
        const u32 filterCount = 2;
        FileFilter filters[filterCount] = {
            { messages::filedialog::cFilterAllAudio,"*.wav *.bcwav *.bfwav" },
            { messages::filedialog::cFilterWave, "*.wav" }
        };

        sAskForPath = false;

        if (!sDroppedAddWavePath.isEmpty())
        {
            sRiffWaveInfo.path = sDroppedAddWavePath;
            sDroppedAddWavePath.clear();
        }
        else if (!OpenFileDialog(&sRiffWaveInfo.path, nullptr, filterCount, filters))
        {
            return nullptr;
        }

        sEncoding = WaveFile::Encoding::DspAdpcm;
        fileio::GetFileStem(&sFileName, sRiffWaveInfo.path);

        if (MatchesInnerFileExtension(sRiffWaveInfo.path.cstr(), InnerFileKind::Wave))
        {
            ImportNativeWaveFile(sRiffWaveInfo.path, sFileName);
            return nullptr;
        }

        if (!WaveFile::readRiffWavInfo(&sRiffWaveInfo))
            return nullptr;
    }

    if (!sFileName.isEmpty())
        DrawWaveImportInfo(MakeWaveImportEncoding(&sEncoding), &sRiffWaveInfo);

    return []() -> Item*
    {
        return CreateImportedArchiveWave(&sRiffWaveInfo, sEncoding, sFileName);
    };
}

static void WaveFileRowActionFunc(Item* item)
{
    WaveFile* wave = static_cast<WaveFile*>(item);

    sead::FixedSafeString<SoundPlayer::cPlaybackErrorLength> playbackError;
    const bool canPlay = !SoundPlayer::GetWaveFilePlaybackError(*wave, &playbackError);

    const bool playing = sSoundPlayer.isActive() && !sSoundPlayer.getPlayingSound() && sSoundPlayer.getPlayingWaveFile() == wave;

    if (DrawRowPlayButton(wave->getId(), canPlay ? nullptr : playbackError.cstr(), playing))
        sSoundPlayer.playWaveFile(*wave);
}

static WaveFile::Encoding sEncoding = WaveFile::Encoding::DspAdpcm;
static WaveFile* sImportWaveFile = nullptr;
static WaveFile::RiffWaveInfo sRiffWaveInfo;
static sead::FixedSafeString<512> sWavFileName;
static OldAudioData sImportOldData;
static OldAudioClear sImportCleared;
static StreamSource sImportOpusStream;
static s32 sImportOpusCodec = static_cast<s32>(aal::BwavCodec::Opus);
static u32 sImportOpusBitrate = 0;
static WaveImportMode sImportMode = WaveImportMode::Replace;
static sead::FixedSafeString<64> sImportFollowsText;

static void StartWaveImport(WaveFile* wave, WaveImportMode mode)
{
    sImportWaveFile = wave;
    sImportMode = mode;
    sImportOpusStream = FindOpusStreamOfWave(wave);

    if (sImportOpusStream.isValid())
        sImportOpusBitrate = GetOpusBitrateChoice(*sImportOpusStream.stream);
}

static WaveImportEncoding GetReplaceEncoding()
{
    const bool isOpus = sImportOpusStream.isValid();
    s32 trackNo = 0;
    const WaveFile* trackWave = sImportWaveFile ? FindOtherStreamTrackWave(*sImportWaveFile, &trackNo) : nullptr;

    if (trackWave)
        return MakeFollowTrackEncoding(*trackWave, trackNo, isOpus ? sImportOpusStream.stream : nullptr, &sEncoding, &sImportOpusCodec, &sImportOpusBitrate, &sImportFollowsText);

    if (isOpus)
        return MakeOpusStreamImportEncoding(&sImportOpusCodec, &sImportOpusBitrate, GetOpusOriginalChoice(*sImportOpusStream.stream));

    return MakeWaveImportEncoding(&sEncoding);
}

static OldAudioData FindWaveOldData(const WaveFile& wave)
{
    OldAudioData data;

    for (const StreamRegion& region : wave.getStreamFileLayout().regions)
        data.regionEnds.push_back(region.end);

    return data;
}

void RequestWaveFileReplace(WaveFile* wave)
{
    SEAD_ASSERT(wave);

    sImportWaveFile = nullptr;
    sRiffWaveInfo.clear();
    sWavFileName.clear();

    FileFilter filters[] = {
        { messages::filedialog::cFilterWave, "*.wav" }
    };

    if (!OpenFileDialog(&sRiffWaveInfo.path, nullptr, 1, filters))
        return;

    if (!WaveFile::readRiffWavInfo(&sRiffWaveInfo))
        return;

    sEncoding = WaveFile::Encoding::DspAdpcm;
    StartWaveImport(wave, wave->getChannels().isEmpty() ? WaveImportMode::Import : WaveImportMode::Replace);
    sImportOldData = FindWaveOldData(*wave);
    sImportCleared = OldAudioClear();
    fileio::GetFileStem(&sWavFileName, sRiffWaveInfo.path);
}

bool IsWaveFileReplacePending(const WaveFile* wave)
{
    return wave && sImportWaveFile == wave;
}

void RequestWaveFileReimport(WaveFile* wave)
{
    SEAD_ASSERT(wave);

    sImportWaveFile = nullptr;
    sRiffWaveInfo.clear();
    sWavFileName.clear();

    if (!WriteReimportSource(decodeWaveFileForEditing(*wave), wave, &sRiffWaveInfo))
        return;

    sEncoding = wave->getEncoding();
    StartWaveImport(wave, WaveImportMode::Reimport);
    sImportOldData = OldAudioData();
    sImportCleared = OldAudioClear();
    sWavFileName.copy(wave->getName().cstr());
}

static bool sMergeDuplicatesRequest = false;

static void DrawMergeDuplicatesMenuItem()
{
    if (ContextMenuItem(ICON_LC_MERGE, messages::item::cMergeDuplicates))
        sMergeDuplicatesRequest = true;

    ImGui::SetItemTooltip("%s", messages::item::cMergeDuplicatesDetail);
}

void WaveFileContextMenuFunc(Item* item)
{
    ContextMenuSeparator();

    if (item)
    {
        DrawWaveFileMenuItems(static_cast<WaveFile*>(item), CollectForAction<WaveFile>(item), true);
        ContextMenuSeparator();
    }

    DrawMergeDuplicatesMenuItem();
}

void DrawWaveFileMenuItems(WaveFile* wave, const std::vector<WaveFile*>& waves, bool canExportFile)
{
    const bool single = waves.size() == 1;

    if (ContextMenuItem(ICON_LC_ARROW_DOWN_UP, messages::common::cReplace, single, messages::menu::cSelectOneWaveFile))
        RequestWaveFileReplace(wave);

    if (single)
        ImGui::SetItemTooltip("%s", messages::import::cReplaceWaveFileTip);

    if (ContextMenuItem(ICON_LC_SLIDERS_HORIZONTAL, messages::import::cReimportMenuLabel, single, messages::menu::cSelectOneWaveFile))
        RequestWaveFileReimport(wave);

    if (single)
        ImGui::SetItemTooltip("%s", messages::import::cReimportWaveFileTip);

    ContextMenuSeparator();

    if (canExportFile && ContextMenuItem(ICON_LC_FILE_OUTPUT, messages::common::cExport))
        RequestExportWaveFiles(waves);

    if (ContextMenuItem(ICON_LC_FILE_AUDIO, messages::menu::cExportWav))
        RequestExportWaveFilesToWav(waves);
}

static bool sWaveFilesIncludeStream = true;

static bool WaveFilesFilter(const Item* item)
{
    if (!sWaveFilesIncludeStream && item->getItemType() == Item::ItemType::WaveFile)
    {
        const WaveFile* wave = static_cast<const WaveFile*>(item);
        if (wave->getIsStreamExtended())
            return false;
    }
    ItemFilterCallback textFilter = GetItemFilterCallback();
    return textFilter ? textFilter(item) : true;
}

static sead::FixedSafeString<32> FormatByteSize(u64 bytes)
{
    sead::FixedSafeString<32> out;

    if (bytes >= 1024 * 1024)
        out.format(messages::common::cMegabytesFormat, bytes / (1024.0 * 1024.0));
    else
        out.format(messages::item::cMergeSizeKbFormat, (unsigned long long)(bytes / 1024));
    
    return out;
}

static void MergeDuplicateWaves(std::vector<Bfsar::WaveDuplicateGroup>* groups)
{
    sSoundPlayer.reset();

    std::vector<Item*> mergedWaves;

    for (const Bfsar::WaveDuplicateGroup& group : *groups)
        mergedWaves.insert(mergedWaves.end(), group.remove.begin(), group.remove.end());

    ForgetRemovedItems(mergedWaves);

    Bfsar::WaveMergeResult r = ActiveSoundArchive().mergeDuplicateWaves(*groups);
    groups->clear();

    if (r.duplicatesRemoved > 0)
    {
        SetUnsavedChanges(true);

        sead::FixedSafeString<128> msg;
        msg.format(r.duplicatesRemoved == 1 ? messages::item::cMergeResultOneFormat : messages::item::cMergeResultFormat, r.duplicatesRemoved, FormatByteSize(r.bytesSaved).cstr());
        notify::Add(notify::Kind::Success, msg.cstr());
    }
}

static void DrawDuplicateGroupList(const std::vector<Bfsar::WaveDuplicateGroup>& groups)
{
    ImGui::BeginChild("DedupList", ImVec2(0.0f, -CalcDialogFooterHeight()), true);

    for (u32 gi = 0; gi < groups.size(); gi++)
    {
        const Bfsar::WaveDuplicateGroup &g = groups[gi];
        ImGui::TextColored(uicolors::Success(), "[%u] %s", g.keep->getId(), g.keep->getNameOrNull().cstr());
        ImGui::Indent(16.0f);

        for (WaveFile *d : g.remove)
            ImGui::TextColored(uicolors::Marker(), messages::item::cMergeRemovedFormat,d->getId(), d->getNameOrNull().cstr());

        ImGui::Unindent(16.0f);

        if (gi + 1 < groups.size())
            ImGui::Separator();
    }

    ImGui::EndChild();
}

static void DrawMergeDuplicatesDialog(std::vector<Bfsar::WaveDuplicateGroup>* groups)
{
    CenterNextDialog();
    ImGui::SetNextWindowSize(ImVec2(520.0f, 480.0f), ImGuiCond_Appearing);

    if (!BeginDialog(messages::item::cMergeTitle, nullptr, 0))
        return;

    u32 removeCount = 0;
    u64 saved = 0;

    for (const Bfsar::WaveDuplicateGroup &g : *groups)
    {
        removeCount += static_cast<u32>(g.remove.size());
        for (WaveFile *d : g.remove)
            saved += d->getFileSize();
    }

    ImGui::Text(removeCount == 1 ? messages::item::cMergeQuestionOneFormat : messages::item::cMergeQuestionFormat, removeCount);

    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled(messages::item::cMergeDetailFormat,FormatByteSize(saved).cstr());
    ImGui::PopTextWrapPos();

    ImGui::Separator();

    DrawDuplicateGroupList(*groups);

    BeginDialogFooter({messages::dialog::cMerge, messages::common::cCancel}, false);

    bool nothingToMerge = groups->empty();

    ImGui::BeginDisabled(nothingToMerge);

    if (DialogButton(messages::dialog::cMerge))
    {
        MergeDuplicateWaves(groups);
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndDisabled();

    if (nothingToMerge)
    {
        ImGui::SetItemTooltip("%s", messages::exporting::cNothingToMerge);
    }

    if (DialogButton(messages::common::cCancel))
    {
        groups->clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void DrawWaveFilesUI()
{
    Bfsar& archive = ActiveSoundArchive();

    static std::vector<Bfsar::WaveDuplicateGroup> sPendingDedup;

    if (BeginListOptions(!sWaveFilesIncludeStream))
    {
        if (ContextMenuToggleItem(messages::item::cShowStreamWaves, sWaveFilesIncludeStream))
            sWaveFilesIncludeStream = !sWaveFilesIncludeStream;

        ContextMenuSeparator();
        DrawMergeDuplicatesMenuItem();
        EndContextMenu();
    }

    if (sMergeDuplicatesRequest)
    {
        sMergeDuplicatesRequest = false;
        sPendingDedup = archive.findDuplicateWaves();

        if (sPendingDedup.empty())
            notify::Add(notify::Kind::Info, messages::item::cNoDuplicates);
        else
            ImGui::OpenPopup(messages::item::cMergeTitle);
    }

    DrawMergeDuplicatesDialog(&sPendingDedup);

    if (!sWaveFilesIncludeStream)
        SetNextItemListNote(messages::item::cStreamWavesHidden);

    static const sead::FormatFixedSafeString<64> sAddAction(messages::item::cAddDialogTitleFormat, messages::common::cWaveFile);

    SetNextItemListAction(&WaveFileRowActionFunc);
    SetNextItemListAddDialogWidth(CalcWaveImportWidth());
    SetNextItemListFileDrop(&DropWaveFile, sAddAction.cstr(), true);

    DrawAllItemsUI("Wave File", messages::common::cWaveFile, archive.getWaveFileList(),
        &CreateWaveFileFunc, nullptr, &WaveFileContextMenuFunc, &WaveFilesFilter
    );
}

void DrawWaveFileReplacePopup()
{
    if (sImportWaveFile && !sRiffWaveInfo.path.isEmpty())
    {
        ImGui::OpenPopup("###WavImport");
    }

    CenterNextDialog();
    ImGui::SetNextWindowContentSize(ImVec2(CalcWaveImportWidth(), 0.0f));

    const char* const titles[] = { messages::import::cImportWaveFileTitle, messages::import::cReplaceWaveFileMenuLabel, messages::import::cReimportWaveFileMenuLabel };
    const WaveImportModeTexts texts = GetWaveImportModeTexts(sImportMode);

    if (BeginDialog(sead::FormatFixedSafeString<64>("%s %s###WavImport", texts.icon, titles[static_cast<s32>(sImportMode)]).cstr(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        SetNextWaveImportOldData(&sImportOldData, &sImportCleared);

        const char* sourceName = sImportMode == WaveImportMode::Reimport ? sWavFileName.cstr() : nullptr;
        const WaveImportResult result = DrawWaveImportBody(sourceName, GetReplaceEncoding(), &sRiffWaveInfo, texts.commitLabel);

        if (result == WaveImportResult::Commit)
        {
            const bool isOpus = sImportOpusStream.isValid();
            const bool success = ReadImportedWave(sImportWaveFile, &sRiffWaveInfo, isOpus ? WaveFile::Encoding::Pcm16 : sEncoding);

            if (success && isOpus)
                SetOpusStreamBitrate(sImportOpusStream, sImportOpusBitrate);

            if (success)
            {
                if (const WaveFile* trackWave = FindOtherStreamTrackWave(*sImportWaveFile, nullptr))
                    sImportWaveFile->copyLoopFrom(*trackWave);

                if (CanRenameItem(sImportWaveFile))
                    sImportWaveFile->getName() = sWavFileName;

                if (sImportCleared.regions)
                    ClearStreamRegions(sImportWaveFile);
                else
                    RemoveStreamRegionsPastEnd(sImportWaveFile);

                SetUnsavedChanges(true);
            }
        }

        if (result != WaveImportResult::None)
        {
            RemoveReimportSource();

            sImportWaveFile = nullptr;
            sImportOpusStream = StreamSource();
            sRiffWaveInfo.clear();
            sWavFileName.clear();

            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void DrawWavRegionDropImport()
{
    static WaveFile::RiffWaveInfo sDropInfo;
    static WaveFile::Encoding sDropEncoding = WaveFile::Encoding::DspAdpcm;
    static sead::FixedSafeString<512> sDropName;
    static BankFile::VelocityRegion* sDropTarget = nullptr;

    if (sWavDropTargetVel && !sDroppedWavPath.isEmpty())
    {
        sDropInfo.clear();
        sDropInfo.path = sDroppedWavPath;
        sDropEncoding = WaveFile::Encoding::DspAdpcm;
        sDropTarget = static_cast<BankFile::VelocityRegion*>(sWavDropTargetVel);

        fileio::GetFileStem(&sDropName, sDropInfo.path);

        const bool ok = WaveFile::readRiffWavInfo(&sDropInfo);

        sDroppedWavPath.clear();
        sWavDropTargetVel = nullptr;

        if (ok)
            ImGui::OpenPopup("###WavRegionImport");
        else
            sDropTarget = nullptr;
    }

    CenterNextDialog();
    ImGui::SetNextWindowContentSize(ImVec2(CalcWaveImportWidth(), 0.0f));

    if (BeginDialog(sead::FormatFixedSafeString<64>(ICON_LC_PLUS " %s###WavRegionImport", messages::import::cImportWaveFileTitle).cstr(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        const WaveImportResult result = DrawWaveImportBody(nullptr, &sDropEncoding, &sDropInfo, messages::common::cImport);

        if (result == WaveImportResult::Commit)
        {
            sSoundPlayer.stopAllPlayers();

            if (WaveFile* wave = CreateImportedArchiveWave(&sDropInfo, sDropEncoding, sDropName))
            {
                Bfsar& archive = ActiveSoundArchive();

                archive.getWaveFileList().pushBack(wave);
                archive.updateList(archive.getWaveFileList());

                if (sDropTarget)
                    sDropTarget->getWaveFileRef().attach(wave);

                SetUnsavedChanges(true);
            }
            else
                MessageLog::instance()->addPopup({ messages::import::cWaveImportFailed });
        }

        if (result != WaveImportResult::None)
        {
            sSoundPlayer.stopAllPlayers();

            sDropTarget = nullptr;
            sDropInfo.clear();
            sDropName.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
    else if (!sDroppedWavPath.isEmpty() && !sWavDropTargetVel)
    {
        sDroppedWavPath.clear();
        MessageLog::instance()->addPopup({ messages::import::cWaveDropNeedsRegion });
    }
}
