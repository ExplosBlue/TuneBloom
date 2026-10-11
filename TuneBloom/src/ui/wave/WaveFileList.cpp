#define IMGUI_DEFINE_MATH_OPERATORS
#include <ui/project/FileLists.h>

#include <ui/bank/BankBundle.h>
#include <ui/widgets/ContextMenu.h>
#include <ui/widgets/Dialogs.h>
#include <ui/export/Export.h>
#include <ui/file/File.h>
#include <ui/file/FileDialogs.h>
#include <ui/items/ItemList.h>
#include <ui/widgets/ListFilter.h>
#include <ui/Messages.h>
#include <ui/app/Notifications.h>
#include <ui/player/PlayerViews.h>
#include <ui/app/PopupMgr.h>
#include <ui/items/Selection.h>
#include <ui/stream/StreamRegions.h>
#include <ui/file/UnsavedChanges.h>
#include <ui/wave/WaveImportPanel.h>
#include <ui/widgets/Widgets.h>

#include <libs/IconsLucide.h>

#include <bfsar/audio/WaveFileEditDecode.h>

#include <filedevice/seadFileDeviceMgr.h>

#include <bfsar/files/BankFile.h>

#include <bfsar/audio/DecodedPcm.h>
#include <bfsar/audio/TempWavWriter.h>
#include <bfsar/io/FileIo.h>

#include <cstdio>
#include <vector>

sead::FixedSafeString<512> sDroppedWavPath;
Item* sWavDropTargetVel = nullptr;

static void ImportNativeWaveFile(const sead::SafeString& path, const sead::SafeString& name)
{
    sead::FileDevice* device = sead::FileDeviceMgr::instance()->findDevice("native");
    if (!device)
    {
        PopupMgr::instance()->addPopup({messages::import::cNativeDeviceUnavailable, nullptr});
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
        PopupMgr::instance()->addPopup({messages::import::cWaveLoadFailed, nullptr});
        return;
    }

    PatchWaveFileHeader(fileData);

    WaveFile* newWave = new WaveFile();
    newWave->setEnableName(true);
    newWave->getName() = name;

    if (!newWave->read(fileData))
    {
        delete newWave;
        device->unload(fileData);
        PopupMgr::instance()->addPopup({messages::import::cWaveReadFailed, nullptr});
        return;
    }

    newWave->setFormat(sBfsar.getFormat());
    newWave->setVersion(sBfsar.getVersionForBfwav());
    newWave->refreshSerializedHash();
    device->unload(fileData);

    AddFileToList(sBfsar.getWaveFileList(), newWave);
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

        if (!OpenFileDialog(&sRiffWaveInfo.path, nullptr, filterCount, filters))
            return nullptr;

        sEncoding = WaveFile::Encoding::DspAdpcm;
        GetFileStem(&sFileName, sRiffWaveInfo.path);

        if (MatchesInnerFileExtension(sRiffWaveInfo.path.cstr(), InnerFileKind::Wave))
        {
            ImportNativeWaveFile(sRiffWaveInfo.path, sFileName);
            return nullptr;
        }

        if (!WaveFile::readRiffWavInfo(&sRiffWaveInfo))
            return nullptr;
    }

    if (!sFileName.isEmpty())
    {
        DrawWaveImportInfo(&sEncoding, &sRiffWaveInfo, sead::FormatFixedSafeString<560>(messages::import::cWaveHeaderFormat, sFileName.cstr()).cstr());
    }

    return []() -> Item*
    {
        WaveFile* waveFile = new WaveFile();
        waveFile->setEnableName(true);
        waveFile->getName() = sFileName;

        std::string tempPath = FinalizeImportInfoForCommit(&sRiffWaveInfo);

        if (!sRiffWaveInfo.isLoop)
        {
            sRiffWaveInfo.loopStartFrame = 0;
        }

        bool success = waveFile->readWavFile(sRiffWaveInfo, sEncoding);

        if (!tempPath.empty())
            ::remove(tempPath.c_str());

        if (!success)
        {
            delete waveFile;
            return nullptr;
        }

        return waveFile;
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
    sImportWaveFile = wave;
    GetFileStem(&sWavFileName, sRiffWaveInfo.path);
}

void RequestWaveFileReimport(WaveFile* wave)
{
    SEAD_ASSERT(wave);

    sImportWaveFile = nullptr;
    sRiffWaveInfo.clear();
    sWavFileName.clear();

    DecodedPcm pcm = decodeWaveFileForEditing(*wave);
    if (!pcm.isValid())
    {
        PopupMgr::instance()->addPopup({messages::import::cReimportDecodeFailed, wave});
        return;
    }

    char tempPath[700];
    snprintf(tempPath, sizeof(tempPath), "%s/tunebloom_reimport_%u.wav", fileio::GetTempFolder(), wave->getId());

    std::string written = TempWavWriter::write(
        pcm.channels, pcm.sampleRate, pcm.isLoop,
        pcm.loopStartFrame, pcm.loopEndFrame, tempPath);

    if (written.empty())
    {
        PopupMgr::instance()->addPopup({messages::import::cReimportWriteFailed, wave});
        return;
    }

    sRiffWaveInfo.path.copy(written.c_str());
    if (!WaveFile::readRiffWavInfo(&sRiffWaveInfo))
        return;

    sEncoding = wave->getEncoding();
    sImportWaveFile = wave;
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
        WaveFile* wave = static_cast<WaveFile*>(item);
        const std::vector<WaveFile*> waves = CollectForAction<WaveFile>(item);
        const bool single = waves.size() == 1;

        if (ContextMenuItem(ICON_LC_REPLACE, messages::import::cReplaceMenuLabel, single, messages::menu::cSelectOneWaveFile))
            RequestWaveFileReplace(wave);

        if (ContextMenuItem(ICON_LC_SLIDERS_HORIZONTAL, messages::import::cReimportMenuLabel, single, messages::menu::cSelectOneWaveFile))
            RequestWaveFileReimport(wave);

        ContextMenuSeparator();

        if (ContextMenuItem(ICON_LC_FILE_OUTPUT, messages::menu::cExport))
            RequestExportWaveFiles(waves);

        if (ContextMenuItem(ICON_LC_FILE_AUDIO, messages::menu::cExportWav))
            RequestExportWaveFilesToWav(waves);

        ContextMenuSeparator();
    }

    DrawMergeDuplicatesMenuItem();
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
        out.format(messages::item::cMergeSizeMbFormat, bytes / (1024.0 * 1024.0));
    else
        out.format(messages::item::cMergeSizeKbFormat, (unsigned long long)(bytes / 1024));
    
    return out;
}

enum class WaveImportResult
{
    None,
    Commit,
    Cancel,
};

static WaveImportResult DrawWaveImportBody(const char* header, WaveFile::Encoding* encoding,
                                           WaveFile::RiffWaveInfo* info, const char* commitLabel)
{
    if (ImGui::IsWindowAppearing())
        ResetWaveImport();

    DrawWaveImportInfo(encoding, info, header);

    BeginDialogFooter({commitLabel, messages::dialog::cCancel}, false);

    const bool commit = DialogButton(commitLabel);
    const bool cancelClicked = DialogButton(messages::dialog::cCancel);

    if (commit)
        return WaveImportResult::Commit;

    if (WaveImportConfirmCancel(cancelClicked))
        return WaveImportResult::Cancel;

    return WaveImportResult::None;
}

static void MergeDuplicateWaves(std::vector<Bfsar::WaveDuplicateGroup>* groups)
{
    sSoundPlayer.reset();

    std::vector<Item*> mergedWaves;

    for (const Bfsar::WaveDuplicateGroup& group : *groups)
        mergedWaves.insert(mergedWaves.end(), group.remove.begin(), group.remove.end());

    ForgetRemovedItems(mergedWaves);

    Bfsar::WaveMergeResult r = sBfsar.mergeDuplicateWaves(*groups);
    groups->clear();

    if (r.duplicatesRemoved > 0)
    {
        SetUnsavedChanges(true);

        sead::FixedSafeString<128> msg;
        msg.format(messages::item::cMergeResultFormat,r.duplicatesRemoved, r.groups, FormatByteSize(r.bytesSaved).cstr());
        notify::Add(notify::Kind::Success, msg.cstr());
    }
}

static void DrawDuplicateGroupList(const std::vector<Bfsar::WaveDuplicateGroup>& groups)
{
    ImGui::BeginChild("DedupList", ImVec2(0.0f, -CalcDialogFooterHeight()), true);

    const ImVec4 keepColor(0.45f, 0.85f, 0.45f, 1.0f);
    const ImVec4 removeColor(0.90f, 0.45f, 0.45f, 1.0f);

    for (u32 gi = 0; gi < groups.size(); gi++)
    {
        const Bfsar::WaveDuplicateGroup &g = groups[gi];
        ImGui::TextColored(keepColor, "[%u] %s", g.keep->getId(), g.keep->getNameOrNull().cstr());
        ImGui::Indent(16.0f);

        for (WaveFile *d : g.remove)
            ImGui::TextColored(removeColor, messages::item::cMergeRemovedFormat,d->getId(), d->getNameOrNull().cstr());

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

    ImGui::Text(messages::item::cMergeQuestionFormat, removeCount);

    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled(messages::item::cMergeDetailFormat,FormatByteSize(saved).cstr());
    ImGui::PopTextWrapPos();

    ImGui::Separator();

    DrawDuplicateGroupList(*groups);

    BeginDialogFooter({messages::dialog::cMerge, messages::dialog::cCancel}, false);

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
        SetDisabledTooltip(messages::exporting::cNothingToMerge);
    }

    if (DialogButton(messages::dialog::cCancel))
    {
        groups->clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void DrawWaveFilesUI()
{
    static std::vector<Bfsar::WaveDuplicateGroup> sPendingDedup;

    if (BeginListOptions(!sWaveFilesIncludeStream))
    {
        if (ContextMenuCheckItem(messages::item::cShowStreamWaves, sWaveFilesIncludeStream))
            sWaveFilesIncludeStream = !sWaveFilesIncludeStream;

        ContextMenuSeparator();
        DrawMergeDuplicatesMenuItem();
        EndContextMenu();
    }

    if (sMergeDuplicatesRequest)
    {
        sMergeDuplicatesRequest = false;
        sPendingDedup = sBfsar.findDuplicateWaves();

        if (sPendingDedup.empty())
            notify::Add(notify::Kind::Info, messages::item::cNoDuplicates);
        else
            ImGui::OpenPopup(messages::item::cMergeTitle);
    }

    DrawMergeDuplicatesDialog(&sPendingDedup);

    if (!sWaveFilesIncludeStream)
        SetNextItemListNote(messages::item::cStreamWavesHidden);

    SetNextItemListAction(&WaveFileRowActionFunc);

    DrawAllItemsUI("Wave File", messages::item::cTypeWaveFile, sBfsar.getWaveFileList(),
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

    if (BeginDialog(sead::FormatFixedSafeString<64>(ICON_LC_REPLACE " %s###WavImport", messages::import::cReplaceWaveFileMenuLabel).cstr(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        const WaveImportResult result = DrawWaveImportBody(
            sead::FormatFixedSafeString<560>(messages::import::cWaveReplaceHeaderFormat, sWavFileName.cstr()).cstr(),
            &sEncoding, &sRiffWaveInfo, messages::import::cReplaceButton);

        if (result == WaveImportResult::Commit)
        {
            std::string tempPath = FinalizeImportInfoForCommit(&sRiffWaveInfo);

            if (!sRiffWaveInfo.isLoop)
                sRiffWaveInfo.loopStartFrame = 0;

            bool success = sImportWaveFile->readWavFile(sRiffWaveInfo, sEncoding);

            if (!tempPath.empty())
                ::remove(tempPath.c_str());

            if (success)
            {
                sImportWaveFile->getName() = sWavFileName;
                TrimStreamRegions(sImportWaveFile);
                SetUnsavedChanges(true);
            }
        }

        if (result != WaveImportResult::None)
        {
            sImportWaveFile = nullptr;
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

        GetFileStem(&sDropName, sDropInfo.path);

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

    if (BeginDialog(sead::FormatFixedSafeString<64>(ICON_LC_PLUS " %s###WavRegionImport", messages::import::cWaveRegionTitle).cstr(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        const WaveImportResult result = DrawWaveImportBody(
            sead::FormatFixedSafeString<560>(messages::import::cWaveRegionHeaderFormat, sDropName.cstr()).cstr(),
            &sDropEncoding, &sDropInfo, messages::dialog::cImport);

        if (result == WaveImportResult::Commit)
        {
            sSoundPlayer.stopAllPlayers();

            std::string tempPath = FinalizeImportInfoForCommit(&sDropInfo);

            if (!sDropInfo.isLoop)
                sDropInfo.loopStartFrame = 0;

            WaveFile* wave = new WaveFile();
            wave->setEnableName(true);
            wave->getName() = sDropName;

            const bool success = wave->readWavFile(sDropInfo, sDropEncoding);

            if (!tempPath.empty())
                ::remove(tempPath.c_str());

            if (success)
            {
                sBfsar.getWaveFileList().pushBack(wave);
                sBfsar.updateList(sBfsar.getWaveFileList());

                if (sDropTarget)
                    sDropTarget->getWaveFileRef().attach(wave);

                SetUnsavedChanges(true);
            }
            else
            {
                delete wave;
                PopupMgr::instance()->addPopup({ messages::import::cWaveImportFailed });
            }
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
        PopupMgr::instance()->addPopup({ messages::import::cWaveDropNeedsRegion });
    }
}
