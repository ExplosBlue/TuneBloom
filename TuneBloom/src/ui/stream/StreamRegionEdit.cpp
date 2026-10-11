#include <ui/stream/StreamRegions.h>

#include <ui/widgets/Dialogs.h>
#include <ui/file/FileDialogs.h>
#include <ui/Messages.h>
#include <ui/app/Notifications.h>
#include <ui/player/PlayerViews.h>
#include <ui/app/PopupMgr.h>
#include <ui/file/UnsavedChanges.h>

#include <bfsar/model/Sound.h>
#include <bfsar/files/StreamRegion.h>
#include <bfsar/files/WaveFile.h>

#include <libs/IconsLucide.h>

#include <filedevice/seadPath.h>

#include <utility>
#include <vector>

namespace
{

struct RegionImport
{
    WaveFile* wave = nullptr;
    std::vector<StreamRegion> regions;
    u32 outsideCount = 0;
    sead::FixedSafeString<256> fileName;
    bool openDialog = false;
};

}

static const WaveFile* sSelectedRegionWave = nullptr;
static s32 sSelectedRegionNo = -1;
static RegionImport* sRegionImport = nullptr;

s32 GetSelectedStreamRegion(const WaveFile* wave)
{
    return sSelectedRegionWave == wave ? sSelectedRegionNo : -1;
}

void SelectStreamRegion(const WaveFile* wave, s32 regionNo)
{
    sSelectedRegionWave = wave;
    sSelectedRegionNo = regionNo;
}

void GetPlayingStreamRegions(const WaveFile* wave, s32* playingNo, s32* nextNo)
{
    *playingNo = -1;
    *nextNo = -1;

    const Sound* sound = sSoundPlayer.getPlayingSound();

    if (!sound || sound->getSoundType() != Sound::SoundType::Strm || sound->getStreamSoundInfo().getMainWave() != wave)
        return;

    *playingNo = sSoundPlayer.getPlayingStreamRegion();
    *nextNo = sSoundPlayer.getNextStreamRegion();
}

void ClickStreamRegion(const WaveFile* wave, s32 regionNo)
{
    s32 playingNo = -1;
    s32 nextNo = -1;
    GetPlayingStreamRegions(wave, &playingNo, &nextNo);

    SelectStreamRegion(wave, regionNo);

    if (playingNo != -1)
        sSoundPlayer.setNextStreamRegion(regionNo == playingNo ? -1 : regionNo);
}

static void ApplyRegionNumbers(const WaveFile* wave, const std::vector<s32>& newNos)
{
    const s32 count = static_cast<s32>(newNos.size());

    s32 playingNo = -1;
    s32 nextNo = -1;
    GetPlayingStreamRegions(wave, &playingNo, &nextNo);

    if (nextNo >= 0 && (nextNo >= count || newNos[nextNo] == -1))
        sSoundPlayer.setNextStreamRegion(-1);

    if (playingNo != -1)
        sSoundPlayer.renumberStreamRegions(newNos.data(), count);

    if (sSelectedRegionWave == wave && sSelectedRegionNo >= 0)
        sSelectedRegionNo = sSelectedRegionNo < count ? newNos[sSelectedRegionNo] : -1;

    SetUnsavedChanges(true);
}

s32 AddStreamRegion(WaveFile* wave, s32 afterNo)
{
    StreamFileLayout& layout = wave->getStreamFileLayout();
    const s32 oldCount = static_cast<s32>(layout.regions.size());
    const s32 regionNo = layout.addRegion(afterNo, wave->getSampleCount());

    if (regionNo == -1)
        return -1;

    std::vector<s32> newNos(oldCount);

    for (s32 i = 0; i < oldCount; i++)
        newNos[i] = i < regionNo ? i : i + 1;

    ApplyRegionNumbers(wave, newNos);
    SelectStreamRegion(wave, regionNo);

    return regionNo;
}

void DeleteStreamRegion(WaveFile* wave, s32 regionNo)
{
    StreamFileLayout& layout = wave->getStreamFileLayout();
    const s32 oldCount = static_cast<s32>(layout.regions.size());

    if (regionNo < 0 || regionNo >= oldCount)
        return;

    layout.removeRegion(regionNo);

    std::vector<s32> newNos(oldCount);

    for (s32 i = 0; i < oldCount; i++)
        newNos[i] = i < regionNo ? i : i - 1;

    newNos[regionNo] = -1;

    const bool wasSelected = GetSelectedStreamRegion(wave) == regionNo;

    ApplyRegionNumbers(wave, newNos);

    if (wasSelected)
        SelectStreamRegion(wave, regionNo < oldCount - 1 ? regionNo : oldCount - 2);
}

void MoveStreamRegion(WaveFile* wave, s32 fromNo, s32 toNo)
{
    StreamFileLayout& layout = wave->getStreamFileLayout();
    const s32 count = static_cast<s32>(layout.regions.size());

    if (fromNo < 0 || fromNo >= count || toNo < 0 || toNo >= count || fromNo == toNo)
        return;

    layout.moveRegion(fromNo, toNo);

    std::vector<s32> newNos(count);

    for (s32 i = 0; i < count; i++)
    {
        if (i == fromNo)
            newNos[i] = toNo;
        else if (fromNo < toNo)
            newNos[i] = i > fromNo && i <= toNo ? i - 1 : i;
        else
            newNos[i] = i >= toNo && i < fromNo ? i + 1 : i;
    }

    ApplyRegionNumbers(wave, newNos);
}

bool RenameStreamRegion(WaveFile* wave, s32 regionNo, const sead::SafeString& name)
{
    if (!StreamFileLayout::IsRegionNameValid(name))
        return false;

    if (wave->getStreamFileLayout().setRegionName(regionNo, name))
        SetUnsavedChanges(true);

    return true;
}

void SetStreamRegionRange(WaveFile* wave, s32 regionNo, u32 start, u32 end)
{
    if (wave->getStreamFileLayout().setRegionRange(regionNo, start, end, wave->getSampleCount()))
        SetUnsavedChanges(true);
}

void CommitStreamRegionRange(const WaveFile* wave, s32 regionNo)
{
    s32 playingNo = -1;
    s32 nextNo = -1;
    GetPlayingStreamRegions(wave, &playingNo, &nextNo);

    const bool repeats = regionNo == playingNo && nextNo == -1 && sSoundPlayer.isLoopEnabled();

    if (regionNo == nextNo || repeats)
        sSoundPlayer.setNextStreamRegion(regionNo);
}

static void SetImportedRegions(WaveFile* wave, RegionImport& import)
{
    const u32 regionCount = static_cast<u32>(import.regions.size());

    wave->getStreamFileLayout().setRegions(std::move(import.regions));

    std::vector<s32> newNos;
    ApplyRegionNumbers(wave, newNos);
    SelectStreamRegion(wave, -1);

    if (import.outsideCount == 0)
        notify::Add(notify::Kind::Success, messages::stream::cRegionImportedTitle, sead::FormatFixedSafeString<512>(messages::stream::cRegionImportedFormat, import.fileName.cstr(), regionCount).cstr());
    else
        notify::Add(notify::Kind::Warning, messages::stream::cRegionImportedTitle, sead::FormatFixedSafeString<512>(messages::stream::cRegionImportOutsideFormat, import.fileName.cstr(), regionCount, import.outsideCount).cstr());
}

static void FitImportedRegions(RegionImport* import, const WavRegions& wav, const WaveFile& wave)
{
    const StreamFileLayout& layout = wave.getStreamFileLayout();
    const u64 rate = wave.getSampleRate();
    const u64 wavRate = wav.sampleRate != 0 ? wav.sampleRate : rate;

    for (StreamRegion region : wav.regions)
    {
        const u64 start = region.start * rate / wavRate;
        const u64 end = region.end * rate / wavRate;

        if (start >= end || end > wave.getSampleCount())
        {
            import->outsideCount++;
            continue;
        }

        region.start = static_cast<u32>(start);
        region.end = static_cast<u32>(end);

        if (!layout.canStoreRegionNames())
            region.name.clear();

        import->regions.push_back(region);
    }
}

void ImportTrackWavRegions(WaveFile* wave, const sead::SafeString& wavPath)
{
    WavRegions wav;

    if (!wave->getStreamFileLayout().regions.empty() || !ReadWavRegions(&wav, wavPath))
        return;

    RegionImport import;
    import.wave = wave;
    sead::Path::getFileName(&import.fileName, wavPath);

    FitImportedRegions(&import, wav, *wave);

    if (!import.regions.empty())
        SetImportedRegions(wave, import);
}

void RequestStreamRegionImport(WaveFile* wave)
{
    sead::FixedSafeString<1024> path;

    FileFilter filters[] = {
        { messages::filedialog::cFilterWave, "*.wav" }
    };

    if (!OpenFileDialog(&path, nullptr, 1, filters))
        return;

    WavRegions wav;

    if (!ReadWavRegions(&wav, path))
        return;

    if (!sRegionImport)
        sRegionImport = new RegionImport();

    RegionImport& import = *sRegionImport;
    import = RegionImport();
    import.wave = wave;

    sead::Path::getFileName(&import.fileName, path);

    FitImportedRegions(&import, wav, *wave);

    if (import.regions.empty())
    {
        sead::FormatFixedSafeString<512> msg(messages::stream::cRegionImportNoneFormat, import.fileName.cstr());
        PopupMgr::instance()->addPopup({ msg });
        import.wave = nullptr;
        return;
    }

    if (wave->getStreamFileLayout().regions.empty())
    {
        SetImportedRegions(wave, import);
        import.wave = nullptr;
        return;
    }

    import.openDialog = true;
}

void DrawStreamRegionImportDialog()
{
    if (!sRegionImport || !sRegionImport->wave)
        return;

    RegionImport& import = *sRegionImport;

    if (import.openDialog)
    {
        ImGui::OpenPopup("###RegionReplace");
        import.openDialog = false;
    }

    CenterNextDialog();

    if (!BeginDialog(sead::FormatFixedSafeString<64>(ICON_LC_FLAG " %s###RegionReplace", messages::stream::cRegionReplaceTitle).cstr(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::Text(messages::stream::cRegionReplaceFormat, import.fileName.cstr(), static_cast<u32>(import.regions.size()), static_cast<u32>(import.wave->getStreamFileLayout().regions.size()));

    BeginDialogFooter({ messages::stream::cRegionReplace, messages::dialog::cCancel });

    if (DialogButton(messages::stream::cRegionReplace))
    {
        SetImportedRegions(import.wave, import);
        import.wave = nullptr;
        ImGui::CloseCurrentPopup();
    }

    if (DialogButton(messages::dialog::cCancel))
    {
        import.wave = nullptr;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void TrimStreamRegions(WaveFile* wave)
{
    const u32 removedCount = wave->getStreamFileLayout().removeRegionsAfter(wave->getSampleCount());

    if (removedCount == 0)
        return;

    if (sSelectedRegionWave == wave)
        sSelectedRegionNo = -1;

    notify::Add(notify::Kind::Warning, messages::stream::cRegionsRemovedTitle, sead::FormatFixedSafeString<128>(messages::stream::cRegionsRemovedFormat, removedCount).cstr());
}

void KeepStreamRegions(Sound* sound, WaveFile* oldMainWave)
{
    WaveFile* mainWave = sound->getStreamSoundInfo().getMainWave();

    if (!oldMainWave || !mainWave || mainWave == oldMainWave)
        return;

    StreamFileLayout& oldLayout = oldMainWave->getStreamFileLayout();
    StreamFileLayout& layout = mainWave->getStreamFileLayout();

    if (oldLayout.regions.empty())
        return;

    if (!layout.regions.empty())
    {
        notify::Add(notify::Kind::Warning, messages::stream::cRegionsReplacedTitle, sead::FormatFixedSafeString<128>(messages::stream::cRegionsReplacedFormat, static_cast<u32>(layout.regions.size())).cstr());
        return;
    }

    if (layout.version == 0)
    {
        std::swap(layout, oldLayout);
    }
    else
    {
        layout.regions = std::move(oldLayout.regions);
        layout.regionRecordBytes = oldLayout.regionRecordBytes;
        oldLayout.regions.clear();
    }

    if (sSelectedRegionWave == oldMainWave)
        sSelectedRegionWave = mainWave;

    TrimStreamRegions(mainWave);
}
