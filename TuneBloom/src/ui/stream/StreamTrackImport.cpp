#include <ui/stream/StreamTrackImport.h>

#include <ui/stream/StreamTrackImportInternal.h>

#include <ui/widgets/Dialogs.h>
#include <ui/file/File.h>
#include <ui/file/FileDialogs.h>
#include <ui/items/ItemList.h>
#include <messages/Messages.h>
#include <bfsar/model/MessageLog.h>
#include <ui/stream/OpusBitrate.h>
#include <ui/stream/StreamRegions.h>
#include <ui/stream/StreamTrackAdd.h>
#include <ui/file/UnsavedChanges.h>
#include <ui/wave/WaveImportPanel.h>

#include <aal/Bwav.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_internal.h>

#include <bfsar/audio/AudioProcessing.h>
#include <bfsar/audio/DecodedPcm.h>
#include <bfsar/io/FileIo.h>
#include <bfsar/model/Sound.h>
#include <bfsar/model/StreamSource.h>
#include <bfsar/audio/TempWavWriter.h>
#include <bfsar/files/WaveFile.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace trackimport
{

const WaveFile* FindMainWave(const Sound::StreamSoundInfo& stream, s32* trackNo)
{
    const Sound::StreamSoundInfo::Track::List& tracks = stream.getTrackList();

    for (s32 i = 0; i < tracks.size(); i++)
    {
        const Sound::StreamSoundInfo::Track& track = *static_cast<const Sound::StreamSoundInfo::Track*>(tracks.nth(i)->val());

        if (!track.getWaveFileRef().isAttached())
            continue;

        if (trackNo)
            *trackNo = i;

        return static_cast<const WaveFile*>(track.getWaveFileRef().getItem());
    }

    return nullptr;
}

static u32 CountEmptyTracks(const Sound::StreamSoundInfo& stream)
{
    return FindMainWave(stream) ? 0 : static_cast<u32>(stream.getTrackList().size());
}

static u32 sEditSerial = 0;

static std::string MakeTrackTempPath(const char* stem, s32 number = -1)
{
    char path[700];

    const u32 processId = fileio::GetOwnProcessId();

    if (number < 0)
        snprintf(path, sizeof(path), "%s/tunebloom_track_%u_%s.wav", fileio::GetTempFolder(), processId, stem);
    else
        snprintf(path, sizeof(path), "%s/tunebloom_track_%u_%s%d.wav", fileio::GetTempFolder(), processId, stem, number);

    return path;
}

static bool ReadSourceFile(SourceFile* file, const std::string& path)
{
    file->info.clear();
    file->info.path.copy(path.c_str());
    file->editedPath.clear();

    if (!WaveFile::readRiffWavInfo(&file->info, true) || file->info.sampleRate == 0)
    {
        file->info.clear();
        return false;
    }

    fileio::GetFileStem(&file->name, file->info.path);
    file->sourceInfo = file->info;

    return true;
}

static bool ReadMainSourceFile(SourceFile* file, const std::string& path)
{
    if (!ReadSourceFile(file, path))
        return false;

    if (file->info.numChannels <= snd::cWaveChannelMax)
        return true;

    MessageLog::instance()->addPopup({ messages::wavefile::cWavOnlyMonoStereo });
    file->info.clear();

    return false;
}

static size_t ReadFirstMainFile(SourceFile* file, const std::vector<std::string>& paths)
{
    size_t first = 0;

    while (first < paths.size() && !ReadMainSourceFile(file, paths[first]))
        first++;

    return first;
}

static bool WritePlainPcmCopy(const WaveFile::RiffWaveInfo& info, const std::string& tempPath, WaveFile::RiffWaveInfo* out)
{
    ChannelData channels;

    if (!decodeRiffChannels(info, &channels) || !TempWavWriter::writeInfo(channels, info.sampleRate, tempPath, out))
        return false;

    out->isLoop = info.isLoop;
    out->loopStartFrame = info.loopStartFrame;
    out->loopEndFrame = info.loopEndFrame;

    return true;
}

void RemoveEditedFile(SourceFile* file)
{
    if (!file->editedPath.empty())
        fileio::RemoveFile(file->editedPath.c_str());

    file->editedPath.clear();
}

void StreamTrackImport::reset()
{
    stopPreview();

    for (u32 i = 0; i < fileCount; i++)
        RemoveEditedFile(&files[i]);

    RemoveEditedFile(&mainFile);
    fileCount = 0;
    targetSource = {};
    openRequested = false;
    anchorTrack = nullptr;
    layout = TrackLayout::PerFile;

    delete mainWave;
    mainWave = nullptr;
    mainFile.info.clear();
    mainFile.sourceInfo.clear();
    mainFile.name.clear();
    mainEncoding = WaveFile::Encoding::DspAdpcm;

    editIndex = cEditMain;
    panelOwner = cEditNone;
    editorOpenRequested = false;
    editorShown = false;
    directImport = false;

    if (hasWaveSource)
        fileio::RemoveFile(MakeTrackTempPath("wave").c_str());

    hasWaveSource = false;
}

u32 StreamTrackImport::findFirstNewTrack() const
{
    if (!targetSource.stream || !FindMainWave(*targetSource.stream))
        return 0;

    const Sound::StreamSoundInfo::Track::List& tracks = targetSource.stream->getTrackList();

    for (s32 i = 0; i < tracks.size(); i++)
    {
        if (tracks.nth(i)->val() != anchorTrack)
            continue;

        const bool isFilled = static_cast<const Sound::StreamSoundInfo::Track*>(anchorTrack)->getWaveFileRef().isAttached();

        return static_cast<u32>(i) + (isFilled ? 1 : 0);
    }

    return static_cast<u32>(tracks.size());
}

void StreamTrackImport::stopPreview()
{
    if (previewWave && sSoundPlayer.getPlayingWaveFile() == previewWave)
        sSoundPlayer.stopAllPlayers();

    if (mainWave && sSoundPlayer.getPlayingWaveFile() == mainWave)
        sSoundPlayer.stopAllPlayers();

    previewIndex = -1;
}

void StreamTrackImport::togglePreview(u32 index)
{
    const bool wasPlaying = isPreviewing(index);

    stopPreview();

    if (wasPlaying)
        return;

    DecodedPcm pcm;

    if (!decodeRiffChannels(files[index].info, &pcm.channels))
        return;

    pcm.sampleRate = files[index].info.sampleRate;
    pcm.sampleCount = static_cast<u32>(pcm.channels[0].size());

    if (!previewWave)
        previewWave = new WaveFile;

    if (!previewWave->setupPreviewPcm16(pcm, false, 0, pcm.sampleCount))
        return;

    sSoundPlayer.playWaveFile(*previewWave, -1, nullptr, 0, false);
    previewIndex = static_cast<s32>(index);
}

void StreamTrackImport::toggleMainPreview()
{
    const bool wasPlaying = isMainPreviewing();

    stopPreview();

    if (!wasPlaying)
        sSoundPlayer.playWaveFile(*mainWave, -1, nullptr, 0, false);
}

std::vector<TrackPlan> StreamTrackImport::buildPlan() const
{
    std::vector<TrackPlan> plans;
    const u32 step = layout == TrackLayout::SplitMono ? 1 : snd::cWaveChannelMax;

    for (u32 i = 0; i < fileCount; i++)
    {
        const u32 channels = files[i].info.numChannels;

        for (u32 ch = 0; ch < channels; ch += step)
        {
            TrackPlan plan;

            for (u32 j = 0; j < step && ch + j < channels; j++)
                plan.sources[plan.count++] = { i, ch + j };

            plans.push_back(plan);
        }

        const bool canPair = layout == TrackLayout::PairMono && channels == 1 &&
                             i + 1 < fileCount && files[i + 1].info.numChannels == 1;

        if (!canPair)
            continue;

        TrackPlan& plan = plans.back();
        plan.sources[plan.count++] = { i + 1, 0 };
        i++;
    }

    return plans;
}

ImportTarget StreamTrackImport::computeTarget(const WaveFile& leadWave) const
{
    ImportTarget target;
    target.encoding = leadWave.getEncoding();
    target.sampleRate = leadWave.getSampleRate();
    target.frameCount = leadWave.getOriginalLoopEndFrame();
    target.isLoop = leadWave.getIsLoop();
    target.loopStart = leadWave.getOriginalLoopStartFrame();
    target.loopEnd = target.frameCount;

    return target;
}

bool StreamTrackImport::needsProcessing(const TrackPlan& plan, const ImportTarget& target) const
{
    const WaveFile::RiffWaveInfo& info = files[plan.sources[0].file].info;

    if (!IsPlainPcm(info) || info.numChannels != plan.count)
        return true;

    for (u32 i = 1; i < plan.count; i++)
        if (plan.sources[i].file != plan.sources[0].file)
            return true;

    return info.sampleRate != target.sampleRate || info.sampleCount != target.frameCount;
}

WaveFile* StreamTrackImport::createWave(const TrackPlan& plan, const ImportTarget& target, const sead::SafeString& name, u32 tempIndex, std::vector<ChannelData>& decoded) const
{
    WaveFile::RiffWaveInfo info = files[plan.sources[0].file].info;
    std::string tempPath;

    if (needsProcessing(plan, target))
    {
        ChannelData channels;

        for (u32 i = 0; i < plan.count; i++)
        {
            const ChannelSource& source = plan.sources[i];
            const WaveFile::RiffWaveInfo& sourceInfo = files[source.file].info;

            if (decoded[source.file].empty() && !decodeRiffChannels(sourceInfo, &decoded[source.file]))
                return nullptr;

            std::vector<float> channel = decoded[source.file][source.channel];

            if (sourceInfo.sampleRate != target.sampleRate)
                channel = AudioProcessing::resample(channel, sourceInfo.sampleRate, target.sampleRate);

            channel.resize(target.frameCount, 0.0f);
            channels.push_back(std::move(channel));
        }

        tempPath = MakeTrackTempPath("", static_cast<s32>(tempIndex));

        if (!TempWavWriter::writeInfo(channels, target.sampleRate, tempPath, &info))
            return nullptr;
    }

    info.isLoop = target.isLoop;
    info.loopStartFrame = target.loopStart;
    info.loopEndFrame = target.loopEnd;

    return readStreamWave(info, target.encoding, name, tempPath);
}

WaveFile* StreamTrackImport::readStreamWave(const WaveFile::RiffWaveInfo& info, WaveFile::Encoding encoding, const sead::SafeString& name, const std::string& tempPath) const
{
    WaveFile* wave = new WaveFile();
    waveOwner().prepareStreamWave(wave);
    wave->setEnableName(true);
    wave->getName() = name;

    const bool success = wave->readWavFile(info, encoding);

    if (!tempPath.empty())
        fileio::RemoveFile(tempPath.c_str());

    if (success)
        return wave;

    delete wave;

    return nullptr;
}

bool StreamTrackImport::createWaves(const std::vector<TrackPlan>& plans, const ImportTarget& target, std::vector<WaveFile*>* waves, sead::BufferedSafeString* failedName)
{
    stopPreview();
    sSoundPlayer.stopAllPlayers();

    u32 plansPerFile[cMaxFiles] = {};

    for (const TrackPlan& plan : plans)
        plansPerFile[plan.sources[0].file]++;

    std::vector<ChannelData> decoded(fileCount);
    u32 ordinal = 0;

    for (u32 i = 0; i < plans.size(); i++)
    {
        const u32 file = plans[i].sources[0].file;

        ordinal = (i > 0 && plans[i - 1].sources[0].file == file) ? ordinal + 1 : 1;

        for (u32 f = 0; f < file; f++)
            ChannelData().swap(decoded[f]);

        sead::FixedSafeString<512> name(files[file].name);

        if (plansPerFile[file] > 1)
            name.appendWithFormat("_%u", ordinal);

        WaveFile* wave = createWave(plans[i], target, name, i, decoded);

        if (!wave)
        {
            for (WaveFile* created : *waves)
                delete created;

            waves->clear();
            failedName->copy(name);

            return false;
        }

        waves->push_back(wave);
    }

    return true;
}

StreamWaveOwner& StreamTrackImport::waveOwner() const
{
    if (targetSource.waveOwner)
        return *targetSource.waveOwner;

    return ActiveSoundArchive();
}

void StreamTrackImport::attachWaves(Sound::StreamSoundInfo& stream, WaveFile* const* waves, u32 waveCount) const
{
    StreamWaveOwner& owner = waveOwner();
    Sound::StreamSoundInfo::Track::List& tracks = stream.getTrackList();
    const u32 emptyTracks = CountEmptyTracks(stream);
    Sound::StreamSoundInfo::Track* insertAfter = emptyTracks == 0 ? static_cast<Sound::StreamSoundInfo::Track*>(anchorTrack) : nullptr;

    for (u32 i = 0; i < waveCount; i++)
    {
        owner.addStreamWave(waves[i]);

        if (i < emptyTracks)
        {
            static_cast<Sound::StreamSoundInfo::Track*>(tracks.nth(i)->val())->getWaveFileRef().attach(waves[i]);
            continue;
        }

        if (insertAfter && !insertAfter->getWaveFileRef().isAttached())
        {
            insertAfter->getWaveFileRef().attach(waves[i]);
            continue;
        }

        Sound::StreamSoundInfo::Track* track = new Sound::StreamSoundInfo::Track();
        track->setDefaultName();
        track->getWaveFileRef().attach(waves[i]);

        if (insertAfter)
            insertAfter->insertBack(track);
        else
            tracks.pushBack(track);

        insertAfter = insertAfter ? track : nullptr;
    }

    Bfsar::updateList(owner.getStreamWaveList());
    Bfsar::updateList(tracks);
    SetUnsavedChanges(true);
}

bool StreamTrackImport::commit()
{
    const WaveFile* leadWave = findLeadWave();

    if (!leadWave)
        return false;

    const std::vector<TrackPlan> plans = buildPlan();
    sead::FixedSafeString<1024> error;

    if (findLimitError(plans, &error))
    {
        MessageLog::instance()->addPopup({error, targetSource.sound});
        return false;
    }

    std::vector<WaveFile*> waves;
    sead::FixedSafeString<512> failedName;

    if (!createWaves(plans, computeTarget(*leadWave), &waves, &failedName))
    {
        error.format(messages::import::cTrackImportFailedFormat, failedName.cstr());
        MessageLog::instance()->addPopup({error, targetSource.sound});

        return false;
    }

    const bool hasMain = mainWave != nullptr;

    if (hasMain)
    {
        waves.insert(waves.begin(), mainWave);
        mainWave = nullptr;
    }

    if (waves.empty())
        return false;

    const SourceFile& firstFile = hasMain ? mainFile : files[plans[0].sources[0].file];

    attachWaves(*targetSource.stream, waves.data(), static_cast<u32>(waves.size()));

    if (hasMain && isOpusTarget())
        SetOpusStreamBitrate(targetSource, mainOpusBitrate);

    if (firstFile.editedPath.empty() && targetSource.stream->getMainWave() == waves[0])
        ImportTrackWavRegions(waves[0], firstFile.sourceInfo.path);

    return true;
}

WaveImportEncoding StreamTrackImport::makeMainEncoding()
{
    if (!isOpusTarget())
        return MakeWaveImportEncoding(&mainEncoding);

    return MakeOpusStreamImportEncoding(&mainOpusCodec, &mainOpusBitrate, GetOpusOriginalChoice(*targetSource.stream));
}

WaveFile* StreamTrackImport::importMainWave()
{
    if (!storeEdit(&mainFile))
        return nullptr;

    WaveFile::RiffWaveInfo info = mainFile.info;
    std::string tempPath;

    if (!IsPlainPcm(info))
    {
        tempPath = MakeTrackTempPath("main");

        if (!WritePlainPcmCopy(mainFile.info, tempPath, &info))
            return nullptr;
    }

    if (!info.isLoop)
        info.loopStartFrame = 0;

    return readStreamWave(info, isOpusTarget() ? WaveFile::Encoding::Pcm16 : mainEncoding, mainFile.name, tempPath);
}

void StreamTrackImport::acceptMainWave(WaveFile* wave)
{
    stopPreview();

    delete mainWave;
    mainWave = wave;

    if (!targetSource.stream)
        return;

    if (fileCount != 0)
    {
        openRequested = !trackDialogOpen;
        return;
    }

    commit();
    reset();
}

WaveImportEncoding StreamTrackImport::makeTrackEncoding()
{
    const bool isOpus = isOpusTarget();
    s32 leadTrackNo = 0;

    if (!mainWave && targetSource.stream)
        FindMainWave(*targetSource.stream, &leadTrackNo);

    if (isOpus)
    {
        editOpusCodec = static_cast<s32>(aal::BwavCodec::Opus);
        editOpusBitrate = GetOpusBitrateChoice(*targetSource.stream);
    }

    return MakeFollowTrackEncoding(*findLeadWave(), leadTrackNo, isOpus ? targetSource.stream : nullptr, &editEncoding, &editOpusCodec, &editOpusBitrate, &followsText);
}

bool StreamTrackImport::storeEdit(SourceFile* file)
{
    WaveFile::RiffWaveInfo info = editInfo;
    const std::string processedPath = FinalizeImportInfoForCommit(&info);

    if (processedPath.empty())
    {
        if (!info.isValid)
            return false;

        if (file->editedPath != info.path.cstr())
            RemoveEditedFile(file);

        file->info = info;
        return true;
    }

    const std::string editedPath = MakeTrackTempPath("edit_", static_cast<s32>(sEditSerial++));
    const bool moved = fileio::CopyFileOver(processedPath.c_str(), editedPath.c_str());

    fileio::RemoveFile(processedPath.c_str());

    if (!moved)
        return false;

    RemoveEditedFile(file);
    file->editedPath = editedPath;
    info.path.copy(editedPath.c_str());
    file->info = info;

    return true;
}

void StreamTrackImport::openEditor(s32 index)
{
    stopPreview();

    if (index != panelOwner)
    {
        ResetWaveImport();
        panelOwner = index;
        editInfo = index >= 0 ? files[index].sourceInfo : mainFile.sourceInfo;
    }

    editIndex = index;
    editorOpenRequested = true;
}

bool StreamTrackImport::commitsOnEditorImport() const
{
    if (editIndex == cEditMain)
        return fileCount == 0;

    return directImport;
}

bool StreamTrackImport::findLimitError(const std::vector<TrackPlan>& plans, sead::BufferedSafeString* error) const
{
    const Sound::StreamSoundInfo* stream = targetSource.stream;
    const u32 newTracks = static_cast<u32>(plans.size()) + (mainWave ? 1 : 0);

    u32 trackCount = newTracks;
    u32 channelCount = mainWave ? static_cast<u32>(mainWave->getChannels().size()) : 0;

    if (stream)
    {
        const u32 emptyTracks = CountEmptyTracks(*stream);
        const bool fillsAnchor = emptyTracks == 0 && newTracks > 0 && anchorTrack && !static_cast<const Sound::StreamSoundInfo::Track*>(anchorTrack)->getWaveFileRef().isAttached();

        trackCount = static_cast<u32>(stream->getTrackList().size()) - emptyTracks + std::max(emptyTracks, newTracks) - (fillsAnchor ? 1 : 0);
        channelCount += stream->getAllocateChannelCount();
    }

    for (u32 i = 0; i < fileCount; i++)
        channelCount += files[i].info.numChannels;

    if (trackCount > cStrmTrackNum)
    {
        error->format(messages::stream::cTrackLimitFormat, cStrmTrackNum);
        return true;
    }

    if (channelCount > cStrmChannelNum)
    {
        error->format(messages::import::cTrackImportChannelLimitFormat, cStrmChannelNum);
        return true;
    }

    return false;
}

static bool CanImportTracks(const StreamSource& source)
{
    SEAD_ASSERT(source.isValid());

    if (!source.stream->hasUnloadedTracks())
        return true;

    MessageLog::instance()->addPopup({messages::stream::cFileNotLoaded, source.sound});
    return false;
}

void StreamTrackImport::request(const StreamSource& source, Item* anchor)
{
    std::vector<std::string> paths;

    if (!CanImportTracks(source) || !pickPaths(&paths))
        return;

    reset();
    start(source, anchor, paths);
}

void StreamTrackImport::requestWave(const StreamSource& source, Item* anchor, WaveFile* wave)
{
    if (!CanImportTracks(source))
        return;

    reset();

    const std::string path = MakeTrackTempPath("wave");
    hasWaveSource = true;

    if (wave->getChannels().isEmpty() || !wave->writeWavFile(path.c_str()))
    {
        const sead::FormatFixedSafeString<512> message(messages::import::cTrackImportFailedFormat, wave->getName().cstr());
        MessageLog::instance()->addPopup({message, source.sound});

        reset();
        return;
    }

    start(source, anchor, { path });
    nameSource(path, wave->getName());
}

void StreamTrackImport::nameSource(const std::string& path, const sead::SafeString& name)
{
    if (hasMainSource() && path == mainFile.info.path.cstr())
        mainFile.name.copy(name);

    for (u32 i = 0; i < fileCount; i++)
    {
        if (path == files[i].info.path.cstr())
            files[i].name.copy(name);
    }
}

void StreamTrackImport::start(const StreamSource& source, Item* anchor, const std::vector<std::string>& paths)
{
    targetSource = source;
    anchorTrack = anchor;

    if (FindMainWave(*source.stream))
        appendPaths(paths, source.sound);
    else
    {
        SourceFile main;
        const size_t first = ReadFirstMainFile(&main, paths);

        if (first < paths.size())
            beginMain(main, paths, first, source.sound);
    }

    if (fileCount == 0 && !hasMainSource())
    {
        reset();
        return;
    }

    if (hasMainSource())
        return;

    directImport = fileCount == 1;

    if (!directImport)
    {
        openRequested = true;
        return;
    }

    sead::FixedSafeString<1024> error;

    if (findLimitError(buildPlan(), &error))
    {
        MessageLog::instance()->addPopup({error, source.sound});
        reset();
        return;
    }

    openEditor(0);
}

bool StreamTrackImport::startCreate(const std::vector<std::string>& paths)
{
    SourceFile main;
    const size_t first = ReadFirstMainFile(&main, paths);

    if (first == paths.size())
        return false;

    resetCreate();
    beginMain(main, paths, first, nullptr);

    return true;
}

void StreamTrackImport::beginMain(const SourceFile& main, const std::vector<std::string>& paths, size_t first, Item* owner)
{
    mainFile = main;
    mainEncoding = WaveFile::Encoding::DspAdpcm;

    if (isOpusTarget())
    {
        mainOpusCodec = static_cast<s32>(aal::BwavCodec::Opus);
        mainOpusBitrate = GetOpusBitrateChoice(*targetSource.stream);
    }

    appendPaths(std::vector<std::string>(paths.begin() + first + 1, paths.end()), owner);
    openEditor(cEditMain);
}

void StreamTrackImport::drawEditor()
{
    ImGui::PushOverrideID(ImHashStr(cEditorId));
    drawEditorDialog();
    ImGui::PopID();
}

bool StreamTrackImport::applyEditor()
{
    if (editIndex == cEditMain)
    {
        WaveFile* wave = importMainWave();

        if (!wave)
            return false;

        acceptMainWave(wave);
        return true;
    }

    if (!storeEdit(&files[editIndex]))
        return false;

    if (directImport)
    {
        commit();
        reset();
    }

    return true;
}

void StreamTrackImport::cancelEditor()
{
    const bool keepsImport = editIndex == cEditMain ? mainWave != nullptr : !directImport;

    panelOwner = cEditNone;

    if (!keepsImport)
        reset();
}

void StreamTrackImport::drawEditorDialog()
{
    if (editorShown && !ImGui::IsPopupOpen(cEditorId))
        editorOpenRequested = true;

    if (editorOpenRequested)
    {
        ImGui::OpenPopup(cEditorId);
        editorOpenRequested = false;
    }

    CenterNextDialog();
    ImGui::SetNextWindowContentSize(ImVec2(CalcWaveImportWidth(), 0.0f));

    editorShown = BeginDialog(sead::FormatFixedSafeString<64>(ICON_LC_IMPORT " %s%s", messages::import::cTrackImportTitle, cEditorId).cstr(), nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    if (!editorShown)
        return;

    const bool isMain = editIndex == cEditMain;

    if (isMain ? !hasMainSource() : editIndex >= static_cast<s32>(fileCount))
    {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        editorShown = false;
        return;
    }
    const SourceFile& file = isMain ? mainFile : files[editIndex];
    const char* commitLabel = commitsOnEditorImport() ? messages::common::cImport : messages::dialog::cOk;
    const WaveImportResult result = DrawWaveImportBody(file.name.cstr(), isMain ? makeMainEncoding() : makeTrackEncoding(), &editInfo, commitLabel, nullptr, false);

    if (result == WaveImportResult::Commit && applyEditor())
    {
        ImGui::CloseCurrentPopup();
        editorShown = false;
    }
    else if (result == WaveImportResult::Commit)
    {
        ImGui::OpenPopup(cEditorFailedId);
    }
    else if (result == WaveImportResult::Cancel)
    {
        ImGui::CloseCurrentPopup();
        editorShown = false;
        cancelEditor();
    }

    WarningPopup(cEditorFailedId, messages::import::cWaveReadFailed);

    ImGui::EndPopup();
}

void StreamTrackImport::resetCreate()
{
    reset();

    for (u32 i = 0; i < createdWaveCount; i++)
        delete createdWaves[i];

    createdWaveCount = 0;
}

void StreamTrackImport::drawCreate()
{
    if (mainWave)
        drawBody(true);
}

bool StreamTrackImport::buildCreateWaves(sead::BufferedSafeString* error)
{
    SEAD_ASSERT(mainWave);

    const std::vector<TrackPlan> plans = buildPlan();
    const ImportTarget target = computeTarget(*mainWave);

    if (findLimitError(plans, error))
        return false;

    std::vector<WaveFile*> waves;
    sead::FixedSafeString<512> failedName;

    if (!createWaves(plans, target, &waves, &failedName))
    {
        error->format(messages::import::cTrackImportFailedFormat, failedName.cstr());
        return false;
    }

    createdWaveCount = static_cast<u32>(waves.size());

    for (u32 i = 0; i < createdWaveCount; i++)
        createdWaves[i] = waves[i];

    return true;
}

void StreamTrackImport::attachCreateWaves(Sound* sound)
{
    stopPreview();

    attachWaves(sound->getStreamSoundInfo(), &mainWave, 1);
    attachWaves(sound->getStreamSoundInfo(), createdWaves, createdWaveCount);

    mainWave = nullptr;
    createdWaveCount = 0;
    reset();
}

bool StreamTrackImport::pickPaths(std::vector<std::string>* paths) const
{
    FileFilter filters[1] = {
        { messages::filedialog::cFilterWave, "*.wav" }
    };

    return OpenFilesDialog(paths, nullptr, 1, filters);
}

void StreamTrackImport::appendPaths(const std::vector<std::string>& paths, Item* owner)
{
    for (const std::string& path : paths)
    {
        if (fileCount == cMaxFiles)
        {
            sead::FormatFixedSafeString<128> msg(messages::import::cTrackImportTooManyFilesFormat, cMaxFiles);
            MessageLog::instance()->addPopup({msg, owner});
            break;
        }

        if (ReadSourceFile(&files[fileCount], path))
            fileCount++;
    }
}

}

static trackimport::StreamTrackImport sImport;
static trackimport::StreamTrackImport sCreateImport;

void RequestStreamTrackImport(const StreamSource& source, Item* anchorTrack)
{
    sImport.request(source, anchorTrack);
}

void RequestStreamTrackWaveImport(const StreamSource& source, Item* anchorTrack, WaveFile* wave)
{
    sImport.requestWave(source, anchorTrack, wave);
}

void DrawStreamTrackImportPopup(const StreamSource& source)
{
    DrawTrackWaveBrowser(source);
    sImport.draw(source);
}

bool IsStreamTrackImportPending(const StreamSource& source)
{
    return source.isValid() && sImport.isPendingFor(source.stream);
}

void CancelStreamTrackImports()
{
    sImport.resetCreate();
    sCreateImport.resetCreate();
}

void ResetStreamCreateImport()
{
    sCreateImport.resetCreate();
}

bool StartStreamCreateImport(const std::vector<std::string>& paths)
{
    return sCreateImport.startCreate(paths);
}

void DrawStreamCreateMainEditor()
{
    sCreateImport.drawEditor();
}

bool HasStreamCreateMainWave()
{
    return sCreateImport.hasMainWave();
}

const sead::SafeString& GetStreamCreateMainName()
{
    return sCreateImport.getMainName();
}

u32 GetStreamCreateFileCount()
{
    return sCreateImport.getFileCount();
}

f32 CalcStreamCreateImportWidth()
{
    return ImFloor(ImGui::GetFontSize() * trackimport::cPanelWidthEm);
}

f32 CalcStreamCreateImportHeight()
{
    return sCreateImport.getBodyHeight();
}

void DrawStreamCreateImport()
{
    sCreateImport.drawCreate();
}

bool BuildStreamCreateWaves(sead::BufferedSafeString* error)
{
    return sCreateImport.buildCreateWaves(error);
}

void AttachStreamCreateWaves(Sound* sound)
{
    sCreateImport.attachCreateWaves(sound);
}
