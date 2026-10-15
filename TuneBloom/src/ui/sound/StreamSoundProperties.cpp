#include <ui/sound/SoundProperties.h>

#include <ui/Messages.h>
#include <ui/app/PopupMgr.h>
#include <ui/app/UI.h>

#include <filedevice/seadPath.h>

#include <filesystem>

static Sound* sStreamReloadSound = nullptr;
static Bfsar::StreamReloadResult sStreamReloadResult;

bool ReloadStreamFile(Sound* sound)
{
    sStreamReloadSound = sound;
    sStreamReloadResult = sBfsar.reloadStreamSound(sound);

    if (sStreamReloadResult.succeeded)
        SetUnsavedChanges(true);

    return sStreamReloadResult.succeeded;
}

bool PickStreamFilePath(sead::BufferedSafeString* outPath, Item* owner)
{
    const InnerFileFormatInfo& streamFormat = GetInnerFileFormat(sBfsar.getFormat(), InnerFileKind::Stream);

    sead::FixedSafeString<64> filterName;
    filterName.format(messages::filedialog::cFilterFileFormat,streamFormat.displayName, streamFormat.extension);

    sead::FixedSafeString<32> filterPattern;
    filterPattern.format("*.%s", streamFormat.extension);

    FileFilter filters[1] = {
        { filterName.cstr(), filterPattern.cstr() }
    };

    sead::FixedSafeString<1024> pickedPath;

    if (!OpenFileDialog(&pickedPath, nullptr, 1, filters))
        return false;

    sead::FixedSafeString<512> archiveDir;

    if (!sead::Path::getDirectoryName(&archiveDir, sBfsar.getFilePath()) &&
        !sead::Path::getDirectoryName(&archiveDir, sBfsar.getLoadedArchivePath()))
    {
        PopupMgr::instance()->addPopup({messages::stream::cReloadNoArchiveFolder, owner});
        return false;
    }

    std::error_code error;
    std::filesystem::path relativePath = std::filesystem::relative(pickedPath.cstr(), archiveDir.cstr(), error);

    if (error || relativePath.empty())
    {
        sead::FormatFixedSafeString<1024> msg(messages::stream::cReplaceOutsideArchiveFolderFormat, pickedPath.cstr());
        PopupMgr::instance()->addPopup({msg, owner});
        return false;
    }

    std::string newPath = relativePath.generic_string();

    if (newPath.rfind("..", 0) == 0)
    {
        sead::FormatFixedSafeString<1024> msg(messages::stream::cReplaceOutsideArchiveFolderFormat, newPath.c_str());
        PopupMgr::instance()->addPopup({msg, owner});
    }

    outPath->copy(newPath.c_str());

    return true;
}

static void ReplaceStreamFile(Sound* sound, Sound::StreamSoundInfo& strmSoundInfo)
{
    sead::FixedSafeString<512> newPath;

    if (!PickStreamFilePath(&newPath, sound))
        return;

    const sead::FixedSafeString<512> previousPath(strmSoundInfo.getPath());
    strmSoundInfo.getPath() = newPath;

    if (!ReloadStreamFile(sound))
        strmSoundInfo.getPath() = previousPath;
}

static void SetStreamReloadTooltip(const Sound* sound, const Sound::StreamSoundInfo& strmSoundInfo, bool hasPath, bool hasArchiveDir)
{
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        return;

    if (!hasPath)
    {
        ImGui::SetTooltip("%s", messages::stream::cNoPath.text);
        return;
    }

    if (!hasArchiveDir)
    {
        ImGui::SetTooltip("%s", messages::stream::cReloadNoArchiveFolder);
        return;
    }

    sead::FixedSafeString<1024> resolvedPath;

    if (!sBfsar.resolveStreamFilePath(*sound, &resolvedPath))
    {
        ImGui::SetTooltip(messages::stream::cReloadNotFoundFormat, strmSoundInfo.getPath().cstr());
        return;
    }

    ImGui::SetTooltip("%s", messages::stream::cReloadTooltip);
}

static void DrawStreamReloadSummary(const Sound* sound, const Sound::StreamSoundInfo& strmSoundInfo)
{
    if (sStreamReloadSound != sound || !sStreamReloadResult.succeeded)
        return;

    const u32 trackCount = sStreamReloadResult.trackCount;
    const u32 channelCount = sStreamReloadResult.channelCount;

    const sead::FormatFixedSafeString<32> tracks(trackCount == 1 ? messages::stream::cReloadedTracksOneFormat : messages::stream::cReloadedTracksFormat, trackCount);
    const sead::FormatFixedSafeString<32> channels(channelCount == 1 ? messages::stream::cReloadedChannelsOneFormat : messages::stream::cReloadedChannelsFormat, channelCount);

    ImGui::TextDisabled(messages::stream::cReloadedSummaryFormat, tracks.cstr(), channels.cstr(), sStreamReloadResult.sampleRate);

    if (sStreamReloadResult.layoutChanged)
        ImGui::TextDisabled("%s", messages::stream::cReloadLayoutChanged);

    if (sStreamReloadResult.tracksReplaced)
        ImGui::TextDisabled("%s", messages::stream::cReloadTracksReplaced);

    if (sStreamReloadResult.streamTypeChanged)
    {
        const char* typeName = strmSoundInfo.getStreamType() == Sound::StreamSoundInfo::StreamType::Opus
                                   ? messages::stream::cTypeOpus
                                   : GetInnerFileDisplayName(sBfsar.getFormat(), InnerFileKind::Stream);

        ImGui::TextDisabled(messages::stream::cReloadTypeChangedFormat, typeName);
    }

    if (sStreamReloadResult.loopChanged)
        ImGui::TextDisabled("%s", messages::stream::cReloadLoopChanged);

    if (sStreamReloadResult.sharedSoundCount != 0)
        ImGui::TextDisabled(sStreamReloadResult.sharedSoundCount == 1 ? messages::stream::cReloadSharedOneFormat : messages::stream::cReloadSharedFormat, sStreamReloadResult.sharedSoundCount);
}

static void DrawStreamPathUI(Sound* sound, Sound::StreamSoundInfo& strmSoundInfo)
{
    const bool hasArchiveDir = sBfsar.hasArchiveDirectory();
    const bool hasPath = !strmSoundInfo.getPath().isEmpty();
    const bool canReload = hasPath && hasArchiveDir;

    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 buttonWidth = ImGui::GetFrameHeight();

    const sead::FormatFixedSafeString<64> pathDetail(messages::stream::cPathRelativeFormat, GetInnerFileExtension(sBfsar.getFormat(), InnerFileKind::SoundArchive));

    PropertyRow(messages::stream::cPathLabel);
    DetailTooltip(pathDetail.cstr());

    ImGui::SetNextItemWidth(-(buttonWidth + style.ItemInnerSpacing.x) * 2.0f);

    sead::FixedSafeString<512> path(strmSoundInfo.getPath());

    if (ImGui::InputText("###Path", path.getBuffer(), path.getBufferSize(), ImGuiInputTextFlags_EnterReturnsTrue) || ImGui::IsItemDeactivatedAfterEdit())
    {
        if (path != strmSoundInfo.getPath())
        {
            strmSoundInfo.getPath() = path;
            SetUnsavedChanges(true);
        }
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        const Sound::StreamSoundInfo::Track::List& tracks = strmSoundInfo.getTrackList();

        for (s32 i = 0; i < tracks.size(); i++)
        {
            const auto& track = *static_cast<const Sound::StreamSoundInfo::Track*>(tracks.nth(i)->val());
            if (!track.getWaveFileRef().isAttached())
                continue;

            ImGui::SetTooltip(messages::stream::cSampleRateFormat, static_cast<const WaveFile*>(track.getWaveFileRef().getItem())->getSampleRate());
            break;
        }
    }

    DetailTooltip(pathDetail.cstr());

    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);

    if (!canReload)
        ImGui::BeginDisabled();

    if (ImGui::Button(ICON_LC_REFRESH_CW "###ReloadStream", ImVec2(buttonWidth, buttonWidth)))
        ReloadStreamFile(sound);

    if (!canReload)
        ImGui::EndDisabled();

    SetStreamReloadTooltip(sound, strmSoundInfo, hasPath, hasArchiveDir);

    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);

    if (!hasArchiveDir)
        ImGui::BeginDisabled();

    if (ImGui::Button(ICON_LC_FOLDER_OPEN "###ReplaceStream", ImVec2(buttonWidth, buttonWidth)))
        ReplaceStreamFile(sound, strmSoundInfo);

    if (!hasArchiveDir)
        ImGui::EndDisabled();

    ImGui::SetItemTooltip("%s", hasArchiveDir ? messages::stream::cReplaceTooltip : messages::stream::cReloadNoArchiveFolder);

    DrawStreamReloadSummary(sound, strmSoundInfo);
}

static void LoopRowTooltip(bool fromWaveFile)
{
    if (fromWaveFile)
        ImGui::SetItemTooltip("%s", messages::stream::cLoopFromWaveFile);
}

static void DrawStreamLoopRows(Sound::StreamSoundInfo& strmSoundInfo)
{
    const bool fromWaveFile = strmSoundInfo.getStreamType() != Sound::StreamSoundInfo::StreamType::Adts;
    const WaveFile* mainWave = fromWaveFile ? strmSoundInfo.getMainWave() : nullptr;

    bool isLoop = mainWave ? mainWave->getIsLoop() : strmSoundInfo.getIsLoop();
    u32 loopStartFrame = mainWave ? mainWave->getOriginalLoopStartFrame() : strmSoundInfo.getLoopStartFrame();
    u32 loopEndFrame = mainWave ? mainWave->getOriginalLoopEndFrame() : strmSoundInfo.getLoopEndFrame();

    ImGui::BeginDisabled(fromWaveFile);

    PropertyRow(messages::common::cLoop);

    if (ImGui::Checkbox("##isLoop", &isLoop))
    {
        strmSoundInfo.setIsLoop(isLoop);
        SetUnsavedChanges(true);
    }

    LoopRowTooltip(fromWaveFile);

    PropertyRow(messages::stream::cLoopStartLabel);

    if (ImGui::InputScalar("##loopStart", ImGuiDataType_U32, &loopStartFrame))
    {
        strmSoundInfo.setLoopStartFrame(loopStartFrame);
        SetUnsavedChanges(true);
    }

    LoopRowTooltip(fromWaveFile);

    PropertyRow(messages::stream::cLoopEndLabel);

    if (ImGui::InputScalar("##loopEnd", ImGuiDataType_U32, &loopEndFrame))
    {
        strmSoundInfo.setLoopEndFrame(loopEndFrame);
        SetUnsavedChanges(true);
    }

    LoopRowTooltip(fromWaveFile);

    ImGui::EndDisabled();
}

void DrawStreamExtensionUI(Sound::StreamSoundInfo& strmSoundInfo)
{
    bool enableStreamSoundExtension = strmSoundInfo.isEnableStreamSoundExtension();
    bool changed = false;
    const bool open = BeginSoundSection(sead::FormatFixedSafeString<64>("%s###Extension", messages::stream::cExtensionTitle).cstr(), &enableStreamSoundExtension, &changed);

    if (changed)
    {
        strmSoundInfo.setEnableStreamSoundExtension(enableStreamSoundExtension);
        SetUnsavedChanges(true);
    }

    if (!open)
        return;

    {
        const char* streamTypeLabels[] = { GetInnerFileDisplayName(sBfsar.getFormat(), InnerFileKind::Stream), messages::stream::cTypeAdts, messages::stream::cTypeOpus };
        const char* streamTypeDetails[] = { messages::stream::cTypeNativeDetail, nullptr, nullptr };

        u32 streamType = strmSoundInfo.getStreamType() - 1;
        u32 streamTypeCount = sBfsar.isV3Bfsar() || streamType + 1 == Sound::StreamSoundInfo::StreamType::Opus ? IM_ARRAYSIZE(streamTypeLabels) : IM_ARRAYSIZE(streamTypeLabels) - 1;

        PropertyRow(messages::stream::cTypeLabel);

        if (ComboScroll("##streamType", (s32 *)&streamType, streamTypeLabels, streamTypeDetails, streamTypeCount))
        {
            strmSoundInfo.setStreamType(static_cast<Sound::StreamSoundInfo::StreamType>(streamType + 1));
            SetUnsavedChanges(true);
        }
    }

    DrawStreamLoopRows(strmSoundInfo);

    if (sBfsar.isV3Bfsar())
    {
        PropertyRow(messages::common::cUnknown);
        DetailTooltip(messages::stream::cTypeInfoDetail);

        u16 typeInfoUpper = strmSoundInfo.getStreamTypeInfoUpper();
        if (ImGui::InputScalar("##typeInfoUpper", ImGuiDataType_U16, &typeInfoUpper, nullptr, nullptr, "%04X", ImGuiInputTextFlags_CharsHexadecimal))
        {
            strmSoundInfo.setStreamTypeInfoUpper(typeInfoUpper);
            SetUnsavedChanges(true);
        }

        DetailTooltip(messages::stream::cTypeInfoDetail);
    }

    EndPropertySection();
}

static void DrawStreamPrefetchUI(Sound::StreamSoundInfo& strmSoundInfo)
{
    const bool isNative = strmSoundInfo.getStreamType() == Sound::StreamSoundInfo::StreamType::NwStreamBinary;

    if (!strmSoundInfo.hasPrefetch() && !(isNative && sBfsar.isStreamPrefetchBuildable()))
        return;

    PropertyRow(messages::stream::cPrefetchLabel);
    DetailTooltip(messages::stream::cPrefetchDetail);

    bool hasPrefetch = strmSoundInfo.hasPrefetch();
    if (ImGui::Checkbox("##prefetch", &hasPrefetch))
    {
        strmSoundInfo.setHasPrefetch(hasPrefetch);
        SetUnsavedChanges(true);
    }

    DetailTooltip(messages::stream::cPrefetchDetail);
}

void DrawStreamSoundUI(Sound* sound, Sound::StreamSoundInfo& strmSoundInfo)
{
    DrawStreamPathUI(sound, strmSoundInfo);

    DrawStreamPrefetchUI(strmSoundInfo);
}
