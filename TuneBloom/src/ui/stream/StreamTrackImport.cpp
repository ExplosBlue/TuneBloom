#include <ui/stream/StreamTrackImport.h>

#include <ui/widgets/Dialogs.h>
#include <ui/file/File.h>
#include <ui/file/FileDialogs.h>
#include <ui/widgets/Knob.h>
#include <ui/Messages.h>
#include <ui/player/PlayerViews.h>
#include <ui/app/PopupMgr.h>
#include <ui/widgets/Properties.h>
#include <ui/stream/StreamRegions.h>
#include <ui/file/UnsavedChanges.h>
#include <ui/wave/WaveFormatUI.h>
#include <ui/widgets/Widgets.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_internal.h>

#include <bfsar/audio/AudioProcessing.h>
#include <bfsar/audio/DecodedPcm.h>
#include <bfsar/model/Sound.h>
#include <bfsar/audio/TempWavWriter.h>
#include <bfsar/files/WaveFile.h>
#include <bfsar/io/FileIo.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace
{

constexpr f32 cPanelWidthEm    = 36.0f;
constexpr f32 cLoopDragRange   = 2000.0f;

constexpr u32 cMaxFiles = cStrmChannelNum;

const ImVec4 cErrorColor(0.90f, 0.45f, 0.45f, 1.0f);

using ChannelData = std::vector<std::vector<float>>;

enum class TrackLayout : s32
{
    PerFile = 0,
    PairMono = 1,
    SplitMono = 2,
};

enum class LoopSource : s32
{
    FirstFile = 0,
    Disabled = 1,
    Manual = 2,
};

struct SourceFile
{
    WaveFile::RiffWaveInfo info;
    sead::FixedSafeString<512> name;
};

struct ChannelSource
{
    u32 file = 0;
    u32 channel = 0;
};

struct TrackPlan
{
    ChannelSource sources[snd::cWaveChannelMax];
    u32 count = 0;
};

struct FileTracks
{
    s32 first = -1;
    s32 last = -1;
    const char* side = nullptr;
};

struct FileActions
{
    s32 moveUp = -1;
    s32 moveDown = -1;
    s32 remove = -1;
    s32 preview = -1;
};

struct ImportTarget
{
    WaveFile::Encoding encoding = WaveFile::Encoding::DspAdpcm;
    u32 sampleRate = 0;
    u32 frameCount = 0;
    bool isLoop = false;
    u32 loopStart = 0;
    u32 loopEnd = 0;
};

u32 ScaleFrames(u32 frames, u32 fromRate, u32 toRate)
{
    if (fromRate == toRate || fromRate == 0)
        return frames;

    return static_cast<u32>(std::llround(static_cast<double>(frames) * toRate / fromRate));
}

const WaveFile* FindMainWave(const Sound& sound)
{
    const Sound::StreamSoundInfo::Track::List& tracks = sound.getStreamSoundInfo().getTrackList();

    for (s32 i = 0; i < tracks.size(); i++)
    {
        const Sound::StreamSoundInfo::Track& track = *static_cast<const Sound::StreamSoundInfo::Track*>(tracks.nth(i)->val());

        if (track.getWaveFileRef().isAttached())
            return static_cast<const WaveFile*>(track.getWaveFileRef().getItem());
    }

    return nullptr;
}

bool IsPlainPcm(const WaveFile::RiffWaveInfo& info)
{
    return info.format == cRiffFormatPcm && info.bitsPerSample <= 16 && info.numChannels <= snd::cWaveChannelMax;
}

std::string MakeTempPath(u32 index)
{
    char path[700];
    snprintf(path, sizeof(path), "%s/tunebloom_track_%u.wav", fileio::GetTempFolder(), index);

    return path;
}

class StreamTrackImport
{
public:
    void request(Sound* sound);
    void draw(Sound* sound);

    void resetCreate();
    void setCreateMainWave(WaveFile* wave, const sead::SafeString& name);
    void setCreateFiles(const std::vector<std::string>& paths);
    void drawCreate();
    bool buildCreateWaves(sead::BufferedSafeString* error);
    void attachCreateWaves(Sound* sound);

    bool hasCreateMainWave() const
    {
        return createMainWave != nullptr;
    }

    bool takeEditRequest()
    {
        const bool requested = editRequested;
        editRequested = false;

        return requested;
    }

    u32 getFileCount() const
    {
        return fileCount;
    }

    f32 getBodyHeight() const
    {
        return bodyHeight;
    }

private:
    SourceFile files[cMaxFiles];
    u32 fileCount = 0;

    Sound* targetSound = nullptr;
    bool openRequested = false;

    WaveFile::Encoding encoding = WaveFile::Encoding::DspAdpcm;
    u32 sampleRate = 0;
    TrackLayout layout = TrackLayout::PerFile;
    LoopSource loopSource = LoopSource::FirstFile;
    u32 manualLoopStart = 0;
    u32 manualLoopEnd = 0;

    WaveFile* previewWave = nullptr;
    s32 previewIndex = -1;

    WaveFile* createMainWave = nullptr;
    sead::FixedSafeString<512> createMainName;
    bool editRequested = false;

    WaveFile* createdWaves[cStrmTrackNum] = {};
    u32 createdWaveCount = 0;

    f32 bodyHeight = 0.0f;

    void reset()
    {
        stopPreview();

        fileCount = 0;
        targetSound = nullptr;
        openRequested = false;
        encoding = WaveFile::Encoding::DspAdpcm;
        sampleRate = 0;
        layout = TrackLayout::PerFile;
        loopSource = LoopSource::FirstFile;
        manualLoopStart = 0;
        manualLoopEnd = 0;
    }

    bool hasSampleRate(u32 rate) const
    {
        for (u32 i = 0; i < fileCount; i++)
            if (files[i].info.sampleRate == rate)
                return true;

        return false;
    }

    bool isOfferedSampleRate(u32 rate) const
    {
        for (u32 preset : cImportSampleRates)
            if (preset == rate)
                return true;

        return hasSampleRate(rate);
    }

    bool isMainPreviewing() const
    {
        return createMainWave && sSoundPlayer.isActive() && sSoundPlayer.getPlayingWaveFile() == createMainWave;
    }

    u32 scaledLength(const SourceFile& file, u32 rate) const
    {
        return ScaleFrames(file.info.sampleCount, file.info.sampleRate, rate);
    }

    u32 longestLength(u32 rate) const
    {
        u32 longest = 0;

        for (u32 i = 0; i < fileCount; i++)
            longest = std::max(longest, scaledLength(files[i], rate));

        return longest;
    }

    bool isPreviewing(u32 index) const
    {
        return previewIndex == static_cast<s32>(index) && previewWave && sSoundPlayer.isActive() && sSoundPlayer.getPlayingWaveFile() == previewWave;
    }

    void stopPreview()
    {
        if (previewWave && sSoundPlayer.getPlayingWaveFile() == previewWave)
            sSoundPlayer.stopAllPlayers();

        if (createMainWave && sSoundPlayer.getPlayingWaveFile() == createMainWave)
            sSoundPlayer.stopAllPlayers();

        previewIndex = -1;
    }

    void togglePreview(u32 index);

    std::vector<TrackPlan> buildPlan() const;
    ImportTarget computeTarget(const WaveFile* mainWave) const;
    bool needsProcessing(const TrackPlan& plan, const ImportTarget& target) const;
    WaveFile* createWave(const TrackPlan& plan, const ImportTarget& target, const sead::SafeString& name, u32 tempIndex, std::vector<ChannelData>& decoded) const;
    bool createWaves(const std::vector<TrackPlan>& plans, const ImportTarget& target, std::vector<WaveFile*>* waves, sead::BufferedSafeString* failedName);
    void attachWaves(Sound* sound, WaveFile* const* waves, u32 waveCount) const;
    bool commit(const std::vector<TrackPlan>& plans, const ImportTarget& target);

    bool pickPaths(std::vector<std::string>* paths) const;
    void appendPaths(const std::vector<std::string>& paths, Item* owner);
    bool loadFiles(Item* owner);
    bool findLimitError(const Sound* sound, const std::vector<TrackPlan>& plans, sead::BufferedSafeString* error) const;

    bool drawBody(const Sound* sound, std::vector<TrackPlan>* plans, ImportTarget* target);
    void drawMainRow();
    void drawFileTable(const std::vector<TrackPlan>& plans, u32 firstTrackIndex, bool hasMainRow);
    FileTracks findFileTracks(const std::vector<TrackPlan>& plans, u32 index) const;
    void drawFileRow(u32 index, const FileTracks& tracks, u32 firstTrackIndex, FileActions* actions);
    void applyFileActions(const FileActions& actions);
    void drawSettings(const ImportTarget& target, bool locked);
    void drawSampleRateRow(const ImportTarget& target, bool locked);
    void drawLoopRows(const ImportTarget& target, bool locked);
    void drawLayoutRow();
    void drawNotes(const ImportTarget& target, bool locked, bool isCreate) const;
};

void StreamTrackImport::togglePreview(u32 index)
{
    const bool wasPlaying = isPreviewing(index);

    stopPreview();

    if (wasPlaying)
        return;

    DecodedPcm pcm;

    if (!decodeRiffChannels(files[index].info, &pcm.channels))
        return;

    if (pcm.channels.size() > snd::cWaveChannelMax)
        pcm.channels.resize(snd::cWaveChannelMax);

    pcm.sampleRate = files[index].info.sampleRate;
    pcm.sampleCount = static_cast<u32>(pcm.channels[0].size());

    if (!previewWave)
        previewWave = new WaveFile;

    if (!previewWave->setupPreviewPcm16(pcm, false, 0, pcm.sampleCount))
        return;

    sSoundPlayer.playWaveFile(*previewWave, -1, nullptr, 0, false);
    previewIndex = static_cast<s32>(index);
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

ImportTarget StreamTrackImport::computeTarget(const WaveFile* mainWave) const
{
    ImportTarget target;

    if (mainWave)
    {
        target.encoding = mainWave->getEncoding();
        target.sampleRate = mainWave->getSampleRate();
        target.frameCount = mainWave->getOriginalLoopEndFrame();
        target.isLoop = mainWave->getIsLoop();
        target.loopStart = mainWave->getOriginalLoopStartFrame();
        target.loopEnd = target.frameCount;

        return target;
    }

    target.encoding = encoding;
    target.sampleRate = sampleRate;
    target.frameCount = longestLength(sampleRate);

    const WaveFile::RiffWaveInfo& first = files[0].info;

    bool wantLoop = false;
    u32 loopStart = 0;
    u32 loopEnd = 0;

    if (loopSource == LoopSource::FirstFile && first.isLoop)
    {
        wantLoop = true;
        loopStart = ScaleFrames(first.loopStartFrame, first.sampleRate, sampleRate);
        loopEnd = ScaleFrames(first.loopEndFrame, first.sampleRate, sampleRate);
    }
    else if (loopSource == LoopSource::Manual)
    {
        wantLoop = true;
        loopStart = manualLoopStart;
        loopEnd = manualLoopEnd;
    }

    loopEnd = std::min(loopEnd, target.frameCount);

    if (wantLoop && loopStart < loopEnd)
    {
        target.isLoop = true;
        target.loopStart = loopStart;
        target.frameCount = loopEnd;
    }

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

        tempPath = TempWavWriter::write(channels, target.sampleRate, false, 0, 0, MakeTempPath(tempIndex));

        if (tempPath.empty())
            return nullptr;

        info.clear();
        info.path.copy(tempPath.c_str());

        if (!WaveFile::readRiffWavInfo(&info))
        {
            ::remove(tempPath.c_str());
            return nullptr;
        }
    }

    info.isLoop = target.isLoop;
    info.loopStartFrame = target.loopStart;
    info.loopEndFrame = target.loopEnd;

    WaveFile* wave = new WaveFile();
    wave->setEnableName(true);
    wave->getName() = name;

    const bool success = wave->readWavFile(info, target.encoding);

    if (!tempPath.empty())
        ::remove(tempPath.c_str());

    if (!success)
    {
        delete wave;
        return nullptr;
    }

    return wave;
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

void StreamTrackImport::attachWaves(Sound* sound, WaveFile* const* waves, u32 waveCount) const
{
    Sound::StreamSoundInfo::Track::List& tracks = sound->getStreamSoundInfo().getTrackList();

    for (u32 i = 0; i < waveCount; i++)
    {
        sBfsar.getWaveFileList().pushBack(waves[i]);

        Sound::StreamSoundInfo::Track* track = new Sound::StreamSoundInfo::Track();
        track->setEnableName(true);
        track->getName() = "Track";
        track->getWaveFileRef().attach(waves[i]);

        tracks.pushBack(track);
    }

    sBfsar.updateList(sBfsar.getWaveFileList());
    sBfsar.updateList(tracks);
    SetUnsavedChanges(true);
}

bool StreamTrackImport::commit(const std::vector<TrackPlan>& plans, const ImportTarget& target)
{
    std::vector<WaveFile*> waves;
    sead::FixedSafeString<512> failedName;

    if (!createWaves(plans, target, &waves, &failedName))
    {
        sead::FormatFixedSafeString<1024> msg(messages::import::cTrackImportFailedFormat, failedName.cstr());
        PopupMgr::instance()->addPopup({msg, targetSound});

        return false;
    }

    attachWaves(targetSound, waves.data(), static_cast<u32>(waves.size()));

    if (targetSound->getStreamSoundInfo().getMainWave() == waves[0])
        ImportTrackWavRegions(waves[0], files[plans[0].sources[0].file].info.path);

    return true;
}

bool StreamTrackImport::findLimitError(const Sound* sound, const std::vector<TrackPlan>& plans, sead::BufferedSafeString* error) const
{
    u32 trackCount = static_cast<u32>(plans.size());
    u32 channelCount = 0;

    if (sound)
    {
        trackCount += static_cast<u32>(sound->getStreamSoundInfo().getTrackList().size());
        channelCount = sound->getStreamSoundInfo().getAllocateChannelCount();
    }
    else if (createMainWave)
    {
        trackCount++;
        channelCount = static_cast<u32>(createMainWave->getChannels().size());
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

void StreamTrackImport::request(Sound* sound)
{
    SEAD_ASSERT(sound);

    if (!sound->getStreamSoundInfo().getTrackList().isEmpty() && !FindMainWave(*sound))
    {
        PopupMgr::instance()->addPopup({messages::stream::cFileNotLoaded, sound});
        return;
    }

    if (!loadFiles(sound))
        return;

    targetSound = sound;
    openRequested = true;
}

void StreamTrackImport::resetCreate()
{
    reset();

    for (u32 i = 0; i < createdWaveCount; i++)
        delete createdWaves[i];

    createdWaveCount = 0;

    delete createMainWave;
    createMainWave = nullptr;
    createMainName.clear();
    editRequested = false;
}

void StreamTrackImport::setCreateMainWave(WaveFile* wave, const sead::SafeString& name)
{
    stopPreview();

    delete createMainWave;
    createMainWave = wave;
    createMainName.copy(name);
}

void StreamTrackImport::setCreateFiles(const std::vector<std::string>& paths)
{
    reset();
    appendPaths(paths, nullptr);
}

void StreamTrackImport::drawCreate()
{
    if (!createMainWave)
        return;

    std::vector<TrackPlan> plans;
    ImportTarget target;

    drawBody(nullptr, &plans, &target);
}

bool StreamTrackImport::buildCreateWaves(sead::BufferedSafeString* error)
{
    SEAD_ASSERT(createMainWave);

    const std::vector<TrackPlan> plans = buildPlan();
    const ImportTarget target = computeTarget(createMainWave);

    if (findLimitError(nullptr, plans, error))
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

    attachWaves(sound, &createMainWave, 1);
    attachWaves(sound, createdWaves, createdWaveCount);

    createMainWave = nullptr;
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

bool StreamTrackImport::loadFiles(Item* owner)
{
    std::vector<std::string> paths;

    if (!pickPaths(&paths))
        return false;

    reset();
    appendPaths(paths, owner);

    return fileCount != 0;
}

void StreamTrackImport::appendPaths(const std::vector<std::string>& paths, Item* owner)
{
    for (const std::string& path : paths)
    {
        if (fileCount == cMaxFiles)
        {
            sead::FormatFixedSafeString<128> msg(messages::import::cTrackImportTooManyFilesFormat, cMaxFiles);
            PopupMgr::instance()->addPopup({msg, owner});
            break;
        }

        SourceFile& file = files[fileCount];
        file.info.clear();
        file.info.path.copy(path.c_str());

        if (!WaveFile::readRiffWavInfo(&file.info, true) || file.info.sampleRate == 0)
            continue;

        GetFileStem(&file.name, file.info.path);

        fileCount++;
    }

    if (fileCount != 0 && !isOfferedSampleRate(sampleRate))
        sampleRate = files[0].info.sampleRate;
}

void StreamTrackImport::drawMainRow()
{
    const u32 channelCount = static_cast<u32>(createMainWave->getChannels().size());
    const u32 rate = createMainWave->getSampleRate();
    const double seconds = rate ? static_cast<double>(createMainWave->getOriginalLoopEndFrame()) / rate : 0.0;

    ImGui::PushID("main");
    ImGui::TableNextRow();

    ImGui::TableNextColumn();

    if (ImGui::Button(isMainPreviewing() ? ICON_LC_SQUARE : ICON_LC_PLAY))
    {
        const bool wasPlaying = isMainPreviewing();

        stopPreview();

        if (!wasPlaying)
            sSoundPlayer.playWaveFile(*createMainWave, -1, nullptr, 0, false);
    }

    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(createMainName.cstr());

    ImGui::TableNextColumn();
    ImGui::TextUnformatted(channelCount == 1 ? messages::item::cChannelsMono : messages::item::cChannelsStereo);

    ImGui::TableNextColumn();
    ImGui::TextUnformatted(WaveFile::sEncodingTypes[static_cast<s32>(createMainWave->getEncoding())]);

    ImGui::TableNextColumn();
    ImGui::Text(messages::unit::cHertzFormat, rate);

    ImGui::TableNextColumn();
    ImGui::Text(messages::import::cTrackImportLengthFormat, seconds);

    if (createMainWave->getIsLoop())
    {
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::TextDisabled(ICON_LC_REPEAT);
    }

    ImGui::TableNextColumn();
    ImGui::Text(messages::import::cTrackImportTrackFormat, 0);

    ImGui::TableNextColumn();

    if (ImGui::Button(ICON_LC_PENCIL))
        editRequested = true;

    ImGui::SetItemTooltip("%s", messages::import::cTrackImportEditTip);

    ImGui::PopID();
}

FileTracks StreamTrackImport::findFileTracks(const std::vector<TrackPlan>& plans, u32 index) const
{
    FileTracks tracks;

    for (u32 p = 0; p < plans.size(); p++)
    {
        for (u32 s = 0; s < plans[p].count; s++)
        {
            if (plans[p].sources[s].file != index)
                continue;

            if (tracks.first < 0)
                tracks.first = static_cast<s32>(p);

            tracks.last = static_cast<s32>(p);

            if (files[index].info.numChannels == 1 && plans[p].count > 1)
                tracks.side = s == 0 ? messages::wavefile::cLeft : messages::wavefile::cRight;
        }
    }

    return tracks;
}

void StreamTrackImport::drawFileRow(u32 index, const FileTracks& tracks, u32 firstTrackIndex, FileActions* actions)
{
    const WaveFile::RiffWaveInfo& info = files[index].info;
    const double seconds = info.sampleRate ? static_cast<double>(info.sampleCount) / info.sampleRate : 0.0;

    ImGui::PushID(static_cast<int>(index));
    ImGui::TableNextRow();

    ImGui::TableNextColumn();

    if (ImGui::Button(isPreviewing(index) ? ICON_LC_SQUARE : ICON_LC_PLAY))
        actions->preview = static_cast<s32>(index);

    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(files[index].name.cstr());

    ImGui::TableNextColumn();

    if (info.numChannels <= 2)
        ImGui::TextUnformatted(info.numChannels == 1 ? messages::item::cChannelsMono : messages::item::cChannelsStereo);
    else
        ImGui::Text(messages::stream::cTrackChannelsFormat, info.numChannels);

    ImGui::TableNextColumn();
    ImGui::Text(info.format == cRiffFormatFloat ? messages::import::cTrackImportBitsFloatFormat : messages::import::cTrackImportBitsFormat, info.bitsPerSample);

    ImGui::TableNextColumn();
    ImGui::Text(messages::unit::cHertzFormat, info.sampleRate);

    ImGui::TableNextColumn();
    ImGui::Text(messages::import::cTrackImportLengthFormat, seconds);

    ImGui::TableNextColumn();

    if (tracks.first != tracks.last)
        ImGui::Text(messages::import::cTrackImportTrackRangeFormat, firstTrackIndex + tracks.first, firstTrackIndex + tracks.last);
    else if (tracks.side)
        ImGui::Text(messages::import::cTrackImportTrackSideFormat, firstTrackIndex + tracks.first, tracks.side);
    else
        ImGui::Text(messages::import::cTrackImportTrackFormat, firstTrackIndex + tracks.first);

    ImGui::TableNextColumn();
    ImGui::BeginDisabled(index == 0);

    if (ImGui::ArrowButton("##up", ImGuiDir_Up))
        actions->moveUp = static_cast<s32>(index);

    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(index + 1 == fileCount);

    if (ImGui::ArrowButton("##down", ImGuiDir_Down))
        actions->moveDown = static_cast<s32>(index);

    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button(ICON_LC_X))
        actions->remove = static_cast<s32>(index);

    ImGui::SetItemTooltip("%s", messages::import::cTrackImportRemoveTip);

    ImGui::PopID();
}

void StreamTrackImport::applyFileActions(const FileActions& actions)
{
    if (actions.preview >= 0)
        togglePreview(static_cast<u32>(actions.preview));

    if (actions.moveUp > 0 || actions.moveDown >= 0 || actions.remove >= 0)
        stopPreview();

    if (actions.moveUp > 0)
        std::swap(files[actions.moveUp], files[actions.moveUp - 1]);

    if (actions.moveDown >= 0)
        std::swap(files[actions.moveDown], files[actions.moveDown + 1]);

    if (actions.remove >= 0)
    {
        for (u32 i = static_cast<u32>(actions.remove); i + 1 < fileCount; i++)
            files[i] = files[i + 1];

        fileCount--;
    }
}

void StreamTrackImport::drawFileTable(const std::vector<TrackPlan>& plans, u32 firstTrackIndex, bool hasMainRow)
{
    FileActions actions;

    if (ImGui::BeginTable("##files", 8, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit))
    {
        ImGui::TableSetupColumn("##preview");
        ImGui::TableSetupColumn(messages::import::cTrackImportColumnFile, ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(messages::import::cChannelsLabel);
        ImGui::TableSetupColumn(messages::import::cTrackImportColumnFormat);
        ImGui::TableSetupColumn(messages::import::cTrackImportSampleRate);
        ImGui::TableSetupColumn(messages::import::cTrackImportColumnLength);
        ImGui::TableSetupColumn(messages::import::cTrackImportColumnTrack);
        ImGui::TableSetupColumn("##actions");
        ImGui::TableHeadersRow();

        if (hasMainRow)
            drawMainRow();

        for (u32 index = 0; index < fileCount; index++)
            drawFileRow(index, findFileTracks(plans, index), firstTrackIndex, &actions);

        ImGui::EndTable();
    }

    applyFileActions(actions);
}

void StreamTrackImport::drawSampleRateRow(const ImportTarget& target, bool locked)
{
    char preview[32];
    snprintf(preview, sizeof(preview), messages::unit::cHertzFormat, locked ? target.sampleRate : sampleRate);

    DialogRow(messages::import::cTrackImportSampleRate);

    if (!ImGui::BeginCombo("##rate", preview))
        return;

    for (u32 i = 0; i < fileCount; i++)
    {
        const u32 rate = files[i].info.sampleRate;
        bool listed = false;

        for (u32 j = 0; j < i; j++)
            listed = listed || files[j].info.sampleRate == rate;

        if (listed)
            continue;

        char label[40];
        snprintf(label, sizeof(label), messages::import::cWaveRateSourceFormat, rate);

        if (ImGui::Selectable(label, rate == sampleRate))
            sampleRate = rate;
    }

    for (u32 rate : cImportSampleRates)
    {
        if (hasSampleRate(rate))
            continue;

        char label[40];
        snprintf(label, sizeof(label), messages::unit::cHertzFormat, rate);

        if (ImGui::Selectable(label, rate == sampleRate))
            sampleRate = rate;
    }

    ImGui::EndCombo();
}

void DrawLoopSampleRow(const char* label, const char* id, u32* value, f32 dragSpeed, u32 min, u32 max)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 unitWidth = ImGui::CalcTextSize(messages::import::cTrackImportSamples).x + style.ItemInnerSpacing.x;

    DialogRow(label);
    ImGui::SetNextItemWidth(-unitWidth);
    DragField(id, ImGuiDataType_U32, value, dragSpeed, &min, &max);
    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
    ImGui::TextDisabled("%s", messages::import::cTrackImportSamples);
}

void StreamTrackImport::drawLoopRows(const ImportTarget& target, bool locked)
{
    const char* loopSourceNames[] = { messages::import::cTrackImportLoopFromFirst, messages::import::cLoopDisabled, messages::import::cLoopManual };
    const u32 longest = longestLength(sampleRate);

    DialogRow(messages::import::cTrackImportLoop);

    if (ComboScroll("##loop", reinterpret_cast<s32*>(&loopSource), loopSourceNames, IM_ARRAYSIZE(loopSourceNames)) && manualLoopEnd == 0)
        manualLoopEnd = longest;

    const bool isManual = loopSource == LoopSource::Manual && !locked;

    u32 shownStart = isManual ? manualLoopStart : (target.isLoop ? target.loopStart : 0);
    u32 shownEnd = isManual ? manualLoopEnd : (target.isLoop ? target.loopEnd : 0);

    const f32 dragSpeed = std::max(1.0f, static_cast<f32>(longest) / cLoopDragRange);

    ImGui::BeginDisabled(!isManual && !locked);

    DrawLoopSampleRow(messages::stream::cLoopStartLabel, "##loopstart", &shownStart, dragSpeed, 0, longest);
    DrawLoopSampleRow(messages::stream::cLoopEndLabel, "##loopend", &shownEnd, dragSpeed, 1, longest);

    ImGui::EndDisabled();

    if (isManual)
    {
        manualLoopEnd = std::max<u32>(1, std::min(shownEnd, longest));
        manualLoopStart = std::min(shownStart, manualLoopEnd - 1);
    }
}

void StreamTrackImport::drawSettings(const ImportTarget& target, bool locked)
{
    ImGui::BeginDisabled(locked);

    WaveFile::Encoding shownEncoding = locked ? target.encoding : encoding;

    DialogRow(messages::wavefile::cEncoding);
    ComboScroll("##encoding", reinterpret_cast<s32*>(&shownEncoding), WaveFile::sEncodingTypes, IM_ARRAYSIZE(WaveFile::sEncodingTypes));

    if (!locked)
        encoding = shownEncoding;

    drawSampleRateRow(target, locked);
    drawLoopRows(target, locked);

    ImGui::EndDisabled();

    drawLayoutRow();
}

void StreamTrackImport::drawLayoutRow()
{
    const char* layoutNames[] = { messages::import::cTrackImportLayoutPerFile, messages::import::cTrackImportLayoutPairMono, messages::import::cTrackImportLayoutPerChannel };
    const char* layoutDetails[] = { messages::import::cTrackImportLayoutPerFileDetail, messages::import::cTrackImportLayoutPairMonoDetail, messages::import::cTrackImportLayoutPerChannelDetail };

    DialogRow(messages::import::cTrackImportLayout);
    DetailTooltip(messages::import::cTrackImportLayoutDetail);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ComboScroll("##layout", reinterpret_cast<s32*>(&layout), layoutNames, layoutDetails, IM_ARRAYSIZE(layoutNames), messages::import::cTrackImportLayoutDetail);
}

void StreamTrackImport::drawNotes(const ImportTarget& target, bool locked, bool isCreate) const
{
    if (fileCount == 0)
        return;

    u32 resampled = 0;
    u32 padded = 0;
    u32 trimmed = 0;

    for (u32 i = 0; i < fileCount; i++)
    {
        const u32 length = scaledLength(files[i], target.sampleRate);

        if (files[i].info.sampleRate != target.sampleRate)
            resampled++;

        if (length < target.frameCount)
            padded++;
        else if (length > target.frameCount)
            trimmed++;
    }

    const double seconds = target.sampleRate ? static_cast<double>(target.frameCount) / target.sampleRate : 0.0;

    if (locked)
        ImGui::TextDisabled("%s", isCreate ? messages::import::cTrackImportFollowsFirst : messages::import::cTrackImportFollowsExisting);

    if (resampled)
        ImGui::TextDisabled(resampled == 1 ? messages::import::cTrackImportResampleOneFormat : messages::import::cTrackImportResampleFormat, resampled, target.sampleRate);

    if (padded)
        ImGui::TextDisabled(padded == 1 ? messages::import::cTrackImportPadOneFormat : messages::import::cTrackImportPadFormat, padded, seconds);

    if (trimmed)
        ImGui::TextDisabled(trimmed == 1 ? messages::import::cTrackImportTrimOneFormat : messages::import::cTrackImportTrimFormat, trimmed, seconds);
}

bool StreamTrackImport::drawBody(const Sound* sound, std::vector<TrackPlan>* plans, ImportTarget* target)
{
    const bool isCreate = sound == nullptr;

    if (fileCount != 0 && !isOfferedSampleRate(sampleRate))
        sampleRate = files[0].info.sampleRate;

    const WaveFile* mainWave = isCreate ? createMainWave : FindMainWave(*sound);
    const bool locked = mainWave != nullptr;
    const u32 existingTracks = isCreate ? 1 : static_cast<u32>(sound->getStreamSoundInfo().getTrackList().size());

    *plans = buildPlan();
    *target = computeTarget(mainWave);

    sead::FixedSafeString<128> error;
    findLimitError(sound, *plans, &error);

    const f32 startY = ImGui::GetCursorPosY();

    ImGui::PushID(this);
    ImGui::Dummy(ImVec2(ImFloor(ImGui::GetFontSize() * cPanelWidthEm), 0.0f));

    const sead::FormatFixedSafeString<64> addLabel("%s %s", ICON_LC_PLUS, messages::import::cTrackImportAddFiles);
    bool addPressed = false;

    if (BeginGroupBox(isCreate ? messages::import::cTrackImportTracksTitle : messages::import::cTrackImportFilesTitle, isCreate ? addLabel.cstr() : nullptr, &addPressed))
    {
        DialogRowGap();
        drawFileTable(*plans, existingTracks, isCreate);

        if (isCreate && fileCount != 0)
            drawLayoutRow();

        EndGroupBox();
    }

    if (!isCreate && BeginGroupBox(messages::import::cTrackImportFormatTitle))
    {
        drawSettings(*target, locked);
        EndGroupBox();
    }

    drawNotes(*target, locked, isCreate);

    if (!error.isEmpty())
        ImGui::TextColored(cErrorColor, "%s", error.cstr());

    ImGui::PopID();

    bodyHeight = ImGui::GetCursorPosY() - startY;

    if (addPressed)
    {
        std::vector<std::string> paths;

        if (pickPaths(&paths))
            appendPaths(paths, nullptr);
    }

    return error.isEmpty();
}

void StreamTrackImport::draw(Sound* sound)
{
    if (openRequested)
    {
        ImGui::OpenPopup("###StreamTrackImport");
        openRequested = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (!BeginDialog(sead::FormatFixedSafeString<64>(ICON_LC_PLUS " %s###StreamTrackImport", messages::import::cTrackImportTitle).cstr(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    if (sound != targetSound || fileCount == 0)
    {
        reset();
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    ImGui::Text(fileCount == 1 ? messages::import::cTrackImportHeaderOneFormat : messages::import::cTrackImportHeaderFormat, fileCount, sound->getFormattedName().cstr());
    DialogRowGap();

    std::vector<TrackPlan> plans;
    ImportTarget target;

    const bool canImport = drawBody(sound, &plans, &target);

    BeginDialogFooter({messages::dialog::cImport, messages::dialog::cCancel});

    ImGui::BeginDisabled(!canImport);
    const bool importClicked = DialogButton(messages::dialog::cImport);
    ImGui::EndDisabled();

    const bool cancelClicked = DialogButton(messages::dialog::cCancel);

    if (importClicked)
        commit(plans, target);

    if (importClicked || cancelClicked)
    {
        reset();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

StreamTrackImport sImport;
StreamTrackImport sCreateImport;

}

void RequestStreamTrackImport(Sound* sound)
{
    sImport.request(sound);
}

void DrawStreamTrackImportPopup(Sound* sound)
{
    sImport.draw(sound);
}

void ResetStreamCreateImport()
{
    sCreateImport.resetCreate();
}

void SetStreamCreateMainWave(WaveFile* wave, const sead::SafeString& name)
{
    sCreateImport.setCreateMainWave(wave, name);
}

bool HasStreamCreateMainWave()
{
    return sCreateImport.hasCreateMainWave();
}

void SetStreamCreateFiles(const std::vector<std::string>& paths)
{
    sCreateImport.setCreateFiles(paths);
}

u32 GetStreamCreateFileCount()
{
    return sCreateImport.getFileCount();
}

bool TakeStreamCreateEditRequest()
{
    return sCreateImport.takeEditRequest();
}

f32 CalcStreamCreateImportWidth()
{
    return ImFloor(ImGui::GetFontSize() * cPanelWidthEm);
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
