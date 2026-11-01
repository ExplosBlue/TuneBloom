#pragma once

#include <ui/wave/WaveImportPanel.h>

#include <aal/Bwav.h>

#include <bfsar/files/StreamWaveOwner.h>
#include <bfsar/files/WaveFile.h>
#include <bfsar/model/Sound.h>
#include <bfsar/model/StreamSource.h>
#include <bfsar/player/SoundPlayer.h>

#include <string>
#include <vector>

namespace trackimport
{

constexpr f32 cPanelWidthEm = 36.0f;
constexpr s32 cListRowMax   = 8;

constexpr u32 cMaxFiles = cStrmChannelNum;

constexpr const char* cFileDragType     = "TRACK_IMPORT_FILE";
constexpr const char* cEditorId         = "###StreamMainImport";
constexpr const char* cEditorFailedId   = "###StreamMainFailed";
constexpr const char* cTrackDialogId    = "###StreamTrackImport";

constexpr s32 cEditMain = -1;
constexpr s32 cEditNone = -2;

using ChannelData = std::vector<std::vector<float>>;

enum class TrackLayout : s32
{
    PerFile = 0,
    PairMono = 1,
    SplitMono = 2,
};

struct SourceFile
{
    WaveFile::RiffWaveInfo info;
    WaveFile::RiffWaveInfo sourceInfo;
    sead::FixedSafeString<512> name;
    std::string editedPath;
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
    s32 moveFrom = -1;
    s32 moveTo = -1;
    s32 remove = -1;
    s32 preview = -1;
    s32 edit = cEditNone;
    bool previewMain = false;
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

const WaveFile* FindMainWave(const Sound::StreamSoundInfo& stream, s32* trackNo = nullptr);
void RemoveEditedFile(SourceFile* file);

class StreamTrackImport
{
public:
    void request(const StreamSource& source, Item* anchor);
    void requestWave(const StreamSource& source, Item* anchor, WaveFile* wave);
    void draw(const StreamSource& source);

    bool isPendingFor(const Sound::StreamSoundInfo* stream) const
    {
        return targetSource.stream == stream && (openRequested || editorOpenRequested || fileCount > 0 || hasMainSource());
    }

    bool startCreate(const std::vector<std::string>& paths);
    void drawEditor();
    void resetCreate();
    void drawCreate();
    bool buildCreateWaves(sead::BufferedSafeString* error);
    void attachCreateWaves(Sound* sound);

    bool hasMainWave() const
    {
        return mainWave != nullptr;
    }

    const sead::SafeString& getMainName() const
    {
        return mainFile.name;
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

    StreamSource targetSource;
    bool openRequested = false;
    bool trackDialogOpen = false;
    s32 drawnFrame = -1;
    Item* anchorTrack = nullptr;

    TrackLayout layout = TrackLayout::PerFile;

    WaveFile* previewWave = nullptr;
    s32 previewIndex = -1;

    SourceFile mainFile;
    WaveFile::Encoding mainEncoding = WaveFile::Encoding::DspAdpcm;
    s32 mainOpusCodec = static_cast<s32>(aal::BwavCodec::Opus);
    u32 mainOpusBitrate = 0;
    WaveFile* mainWave = nullptr;

    s32 editIndex = cEditMain;
    s32 panelOwner = cEditNone;
    WaveFile::RiffWaveInfo editInfo;
    WaveFile::Encoding editEncoding = WaveFile::Encoding::DspAdpcm;
    s32 editOpusCodec = static_cast<s32>(aal::BwavCodec::Opus);
    u32 editOpusBitrate = 0;
    sead::FixedSafeString<64> followsText;
    bool editorOpenRequested = false;
    bool editorShown = false;
    bool directImport = false;
    bool hasWaveSource = false;

    WaveFile* createdWaves[cStrmTrackNum] = {};
    u32 createdWaveCount = 0;

    f32 bodyHeight = 0.0f;

    void reset();

    bool hasMainSource() const
    {
        return !mainFile.name.isEmpty();
    }

    bool isOpusTarget() const
    {
        return targetSource.stream && targetSource.stream->getStreamType() == Sound::StreamSoundInfo::StreamType::Opus;
    }

    const WaveFile* findLeadWave() const
    {
        if (mainWave || !targetSource.stream)
            return mainWave;

        return FindMainWave(*targetSource.stream);
    }

    u32 findFirstNewTrack() const;

    bool isMainPreviewing() const
    {
        return mainWave && sSoundPlayer.isActive() && sSoundPlayer.getPlayingWaveFile() == mainWave;
    }

    bool isPreviewing(u32 index) const
    {
        return previewIndex == static_cast<s32>(index) && previewWave && sSoundPlayer.isActive() && sSoundPlayer.getPlayingWaveFile() == previewWave;
    }

    void stopPreview();
    void togglePreview(u32 index);
    void toggleMainPreview();

    std::vector<TrackPlan> buildPlan() const;
    ImportTarget computeTarget(const WaveFile& leadWave) const;
    bool needsProcessing(const TrackPlan& plan, const ImportTarget& target) const;
    WaveFile* readStreamWave(const WaveFile::RiffWaveInfo& info, WaveFile::Encoding encoding, const sead::SafeString& name, const std::string& tempPath) const;
    WaveFile* createWave(const TrackPlan& plan, const ImportTarget& target, const sead::SafeString& name, u32 tempIndex, std::vector<ChannelData>& decoded) const;
    bool createWaves(const std::vector<TrackPlan>& plans, const ImportTarget& target, std::vector<WaveFile*>* waves, sead::BufferedSafeString* failedName);
    StreamWaveOwner& waveOwner() const;
    void attachWaves(Sound::StreamSoundInfo& stream, WaveFile* const* waves, u32 waveCount) const;
    bool commit();

    WaveImportEncoding makeMainEncoding();
    WaveImportEncoding makeTrackEncoding();
    WaveFile* importMainWave();
    void acceptMainWave(WaveFile* wave);
    bool storeEdit(SourceFile* file);
    void openEditor(s32 index);
    bool commitsOnEditorImport() const;
    void drawEditorDialog();
    bool applyEditor();
    void cancelEditor();

    bool pickPaths(std::vector<std::string>* paths) const;
    void start(const StreamSource& source, Item* anchor, const std::vector<std::string>& paths);
    void beginMain(const SourceFile& main, const std::vector<std::string>& paths, size_t first, Item* owner);
    void nameSource(const std::string& path, const sead::SafeString& name);
    void appendPaths(const std::vector<std::string>& paths, Item* owner);
    bool findLimitError(const std::vector<TrackPlan>& plans, sead::BufferedSafeString* error) const;
    bool hasLayoutChoice() const;

    void drawTrackDialog(const StreamSource& source);
    void drawHead(const StreamSource& source) const;
    bool drawBody(bool inBox);
    void drawMainRow(FileActions* actions);
    void drawFileTable(const std::vector<TrackPlan>& plans, u32 firstTrackIndex);
    FileTracks findFileTracks(const std::vector<TrackPlan>& plans, u32 index) const;
    void drawFileRow(u32 index, const FileTracks& tracks, u32 firstTrackIndex, FileActions* actions);
    void drawFileDragDrop(u32 index, FileActions* actions) const;
    void applyFileActions(const FileActions& actions);
    void drawLayoutRow();
};

}
