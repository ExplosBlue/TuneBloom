#include <bfsar/archive/Bfsar.h>

#include <core/Debug.h>
#include <core/TestHooks.h>

#include <bfsar/archive/BfsarInternal.h>
#include <bfsar/files/BfstmFile.h>
#include <bfsar/files/BfstpFile.h>
#include <bfsar/io/FileIo.h>
#include <bfsar/files/OpusStream.h>
#include <bfsar/archive/SaveReport.h>

#include <ui/Messages.h>
#include <ui/app/PopupMgr.h>
#include <ui/app/UI.h>

#include <filedevice/seadFileDeviceMgr.h>
#include <filedevice/seadPath.h>

#include <snd/snd_StreamSoundFileReader.h>

#include <snd/StreamSoundPlayer.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

bool Bfsar::hasArchiveDirectory() const
{
    sead::FixedSafeString<512> dir;

    return sead::Path::getDirectoryName(&dir, getFilePath()) ||
           sead::Path::getDirectoryName(&dir, getLoadedArchivePath());
}

bool Bfsar::resolveStreamFilePath(const Sound& sound, sead::BufferedSafeString* out) const
{
    const sead::SafeString& path = sound.getStreamSoundInfo().getPath();

    if (path.isEmpty())
        return false;

    const sead::SafeString* archivePaths[2] = {&getFilePath(), &getLoadedArchivePath()};

    for (const sead::SafeString* archivePath : archivePaths)
    {
        sead::FixedSafeString<512> dir;

        if (!sead::Path::getDirectoryName(&dir, *archivePath))
            continue;

        sead::FixedSafeString<1024> candidate;
        candidate.format("%s/%s", dir.cstr(), path.cstr());

        std::error_code error;
        if (!std::filesystem::exists(candidate.cstr(), error) || error)
            continue;

        out->copy(candidate);
        return true;
    }

    return false;
}

void Bfsar::detachStreamWaves(Sound* sound)
{
    std::vector<WaveFile*> orphans;

    Sound::StreamSoundInfo::Track::List& tracks = sound->getStreamSoundInfo().getTrackList();

    for (s32 i = 0; i < tracks.size(); i++)
    {
        Sound::StreamSoundInfo::Track& track = *static_cast<Sound::StreamSoundInfo::Track*>(tracks.nth(i)->val());

        if (!track.getWaveFileRef().isAttached())
            continue;

        WaveFile* wave = static_cast<WaveFile*>(track.getWaveFileRef().getItem());
        track.getWaveFileRef().detach();

        if (wave->getIsFromStreamFile() && wave->getReferences().isEmpty())
            orphans.push_back(wave);
    }

    ForgetRemovedItems(std::vector<Item*>(orphans.begin(), orphans.end()));
    removeUnusedWaveFiles(orphans);
}

void Bfsar::applyStreamTrackLayout(Sound* sound, u32 trackCount, u32 channelCount) const
{
    Sound::StreamSoundInfo::Track::List& tracks = sound->getStreamSoundInfo().getTrackList();

    std::vector<Item*> removedTracks;

    while (static_cast<u32>(tracks.size()) > trackCount)
    {
        Item* last = tracks.nth(tracks.size() - 1)->val();

        tracks.erase(last);
        removedTracks.push_back(last);
    }

    ForgetRemovedItems(removedTracks);

    for (Item* track : removedTracks)
        delete track;

    while (static_cast<u32>(tracks.size()) < trackCount)
    {
        Sound::StreamSoundInfo::Track* track = new Sound::StreamSoundInfo::Track();

        track->mEnableName = true;
        track->mName = "Track";

        tracks.pushBack(track);
    }

    u32 nextChannel = 0;

    for (u32 i = 0; i < trackCount; i++)
    {
        Sound::StreamSoundInfo::Track& track = *static_cast<Sound::StreamSoundInfo::Track*>(tracks.nth(i)->val());

        track.mId = i;
        track.mChannels.clear();

        u32 trackChannelCount = channelCount / trackCount + (i < channelCount % trackCount ? 1 : 0);

        for (u32 j = 0; j < trackChannelCount && j < snd::cWaveChannelMax; j++)
            *track.mChannels.birthBack() = static_cast<u8>(nextChannel++);
    }
}

static bool StreamChannelsFitInTracks(u32 trackCount, u32 channelCount)
{
    return trackCount != 0 && trackCount <= channelCount && channelCount <= trackCount * snd::cWaveChannelMax;
}

static u32 ChooseStreamTrackCount(u32 currentTrackCount, u32 channelCount)
{
    if (StreamChannelsFitInTracks(currentTrackCount, channelCount))
        return currentTrackCount;

    return (channelCount + snd::cWaveChannelMax - 1) / snd::cWaveChannelMax;
}

static bool StreamTrackLayoutFits(const Sound::StreamSoundInfo& strmSoundInfo, u32 channelCount)
{
    const Sound::StreamSoundInfo::Track::List& tracks = strmSoundInfo.getTrackList();

    if (tracks.isEmpty())
        return false;

    u32 total = 0;

    for (s32 i = 0; i < tracks.size(); i++)
    {
        const Sound::StreamSoundInfo::Track& track = *static_cast<const Sound::StreamSoundInfo::Track*>(tracks.nth(i)->val());
        const sead::ObjList<u8>& channels = track.getChannels_();

        for (s32 j = 0; j < channels.size(); j++)
        {
            if (*channels.nth(j) >= channelCount)
                return false;
        }

        total += channels.size();
    }

    return total == channelCount;
}

bool Bfsar::readEmbeddedStreamTracks(nw::snd::internal::StreamSoundFileReader& reader, Sound::StreamSoundInfo& strmSoundInfo, sead::Heap* heap) const
{
    u32 trackCount = reader.GetTrackCount();

    if (trackCount > cStrmTrackNum)
        trackCount = cStrmTrackNum;

    bool readAllTracks = true;

    for (u32 j = 0; j < trackCount; j++)
    {
        Sound::StreamSoundInfo::Track* track = new(heap) Sound::StreamSoundInfo::Track();
        track->mId = j;

        track->mEnableName = true;
        track->mName = "Track";

        nw::snd::internal::StreamSoundFileReader::TrackInfo trackInfo;

        if (reader.ReadStreamTrackInfo(&trackInfo, j) && trackInfo.channelCount <= snd::cWaveChannelMax)
        {
            track->mVolume = trackInfo.volume;
            track->mPan = trackInfo.pan;
            track->mSPan = trackInfo.span;
            track->mFlags = trackInfo.flags;

            for (u8 k = 0; k < trackInfo.channelCount; k++)
                *track->mChannels.birthBack() = trackInfo.globalChannelIndex[k];
        }
        else
        {
            sead::FormatFixedSafeString<128> msg(messages::stream::cTrackUnreadableFormat, j);
            PopupMgr::instance()->pushCurrentItemWarning(msg, messages::stream::cAudioUnreadableDetail);

            readAllTracks = false;
        }

        strmSoundInfo.mTrackList.pushBack(track);
    }

    return readAllTracks;
}

bool StreamWavesCoverLayout(const Sound::StreamSoundInfo& strmSoundInfo)
{
    const Sound::StreamSoundInfo::Track::List& tracks = strmSoundInfo.getTrackList();

    if (tracks.isEmpty())
        return false;

    for (s32 i = 0; i < tracks.size(); i++)
    {
        const Sound::StreamSoundInfo::Track& track = *static_cast<const Sound::StreamSoundInfo::Track*>(tracks.nth(i)->val());

        if (!track.getWaveFileRef().isAttached())
            return false;

        const WaveFile& wave = *static_cast<const WaveFile*>(track.getWaveFileRef().getItem());

        if (track.getChannels_().isEmpty() || wave.getChannels().size() != track.getChannels_().size())
            return false;
    }

    return true;
}

static void CopyStreamChannelLayout(const Sound::StreamSoundInfo& source, Sound::StreamSoundInfo* destination)
{
    const Sound::StreamSoundInfo::Track::List& sourceTracks = source.getTrackList();
    Sound::StreamSoundInfo::Track::List& destinationTracks = destination->getTrackList();

    for (s32 i = 0; i < sourceTracks.size() && i < destinationTracks.size(); i++)
    {
        const Sound::StreamSoundInfo::Track& sourceTrack = *static_cast<const Sound::StreamSoundInfo::Track*>(sourceTracks.nth(i)->val());
        Sound::StreamSoundInfo::Track& destinationTrack = *static_cast<Sound::StreamSoundInfo::Track*>(destinationTracks.nth(i)->val());

        destinationTrack.copyChannelsFrom(sourceTrack);
    }
}

static bool StreamTracksMatch(const Sound::StreamSoundInfo& first, const Sound::StreamSoundInfo& second)
{
    const Sound::StreamSoundInfo::Track::List& firstTracks = first.getTrackList();
    const Sound::StreamSoundInfo::Track::List& secondTracks = second.getTrackList();

    if (firstTracks.size() != secondTracks.size())
        return false;

    for (s32 i = 0; i < firstTracks.size(); i++)
    {
        const Sound::StreamSoundInfo::Track& firstTrack = *static_cast<const Sound::StreamSoundInfo::Track*>(firstTracks.nth(i)->val());
        const Sound::StreamSoundInfo::Track& secondTrack = *static_cast<const Sound::StreamSoundInfo::Track*>(secondTracks.nth(i)->val());

        if (firstTrack.getVolume() != secondTrack.getVolume() || firstTrack.getPan() != secondTrack.getPan() ||
            firstTrack.getSPan() != secondTrack.getSPan() || firstTrack.getFlags() != secondTrack.getFlags())
            return false;

        const sead::ObjList<u8>& firstChannels = firstTrack.getChannels_();
        const sead::ObjList<u8>& secondChannels = secondTrack.getChannels_();

        if (firstChannels.size() != secondChannels.size())
            return false;

        for (s32 j = 0; j < firstChannels.size(); j++)
        {
            if (*firstChannels.nth(j) != *secondChannels.nth(j))
                return false;
        }
    }

    return true;
}

static void CopyEmbeddedTrackInfo(const Sound::StreamSoundInfo& source, Sound::StreamSoundInfo* destination)
{
    const Sound::StreamSoundInfo::Track::List& sourceTracks = source.getTrackList();
    Sound::StreamSoundInfo::Track::List& destinationTracks = destination->getTrackList();

    for (s32 i = 0; i < sourceTracks.size() && i < destinationTracks.size(); i++)
    {
        const Sound::StreamSoundInfo::Track& sourceTrack = *static_cast<const Sound::StreamSoundInfo::Track*>(sourceTracks.nth(i)->val());
        Sound::StreamSoundInfo::Track& destinationTrack = *static_cast<Sound::StreamSoundInfo::Track*>(destinationTracks.nth(i)->val());

        destinationTrack.copyChannelsFrom(sourceTrack);
        destinationTrack.setVolume(sourceTrack.getVolume());
        destinationTrack.setPan(sourceTrack.getPan());
        destinationTrack.setSPan(sourceTrack.getSPan());
        destinationTrack.setFlags(sourceTrack.getFlags());
    }
}

static void MoveStreamWaves(Sound::StreamSoundInfo* source, Sound::StreamSoundInfo* destination)
{
    Sound::StreamSoundInfo::Track::List& sourceTracks = source->getTrackList();
    Sound::StreamSoundInfo::Track::List& destinationTracks = destination->getTrackList();

    for (s32 i = 0; i < sourceTracks.size() && i < destinationTracks.size(); i++)
    {
        Sound::StreamSoundInfo::Track& sourceTrack = *static_cast<Sound::StreamSoundInfo::Track*>(sourceTracks.nth(i)->val());
        Sound::StreamSoundInfo::Track& destinationTrack = *static_cast<Sound::StreamSoundInfo::Track*>(destinationTracks.nth(i)->val());

        destinationTrack.getWaveFileRef().attach(sourceTrack.getWaveFileRef().getItem());
        sourceTrack.getWaveFileRef().detach();
    }
}

static void DiscardStagedSound(Bfsar& archive, Sound* staged)
{
    std::vector<WaveFile*> stagedWaves;

    Sound::StreamSoundInfo::Track::List& tracks = staged->getStreamSoundInfo().getTrackList();

    for (s32 i = 0; i < tracks.size(); i++)
    {
        Sound::StreamSoundInfo::Track& track = *static_cast<Sound::StreamSoundInfo::Track*>(tracks.nth(i)->val());

        if (track.getWaveFileRef().isAttached())
            stagedWaves.push_back(static_cast<WaveFile*>(track.getWaveFileRef().getItem()));
    }

    ForgetRemovedItems({staged});
    delete staged;

    std::sort(stagedWaves.begin(), stagedWaves.end());
    stagedWaves.erase(std::unique(stagedWaves.begin(), stagedWaves.end()), stagedWaves.end());

    ForgetRemovedItems(std::vector<Item*>(stagedWaves.begin(), stagedWaves.end()));
    archive.removeUnusedWaveFiles(stagedWaves);
}

static bool OpusStreamDecodes(const void* data, u32 size)
{
    opusstream::Info info;
    std::vector<s16*> channelData;

    const bool decoded = opusstream::DecodeToPcm16(data, size, info, channelData, nullptr);

    for (s16* channel : channelData)
        delete[] reinterpret_cast<u8*>(channel);

    return decoded;
}

struct StreamReloadContext
{
    Sound* sound = nullptr;
    const sead::SafeString* filePathOverride = nullptr;

    sead::FileDevice* device = nullptr;
    u8* fileData = nullptr;
    u32 fileSize = 0;

    bool isOpus = false;
    nw::snd::internal::StreamSoundFileReader reader;
    nw::snd::internal::StreamSoundFile::StreamSoundInfo fileInfo;
    Sound::StreamSoundInfo::StreamType streamType = Sound::StreamSoundInfo::StreamType::NwStreamBinary;

    bool tracksFromFile = false;
    bool keepLayout = false;
    u32 trackCount = 0;

    Sound* staged = nullptr;

    void unloadFile()
    {
        if (fileData)
            device->unload(fileData);

        fileData = nullptr;
    }
};

static void ReportStreamReloadError(StreamReloadContext& ctx, const sead::SafeString& text)
{
    ctx.unloadFile();

    PopupMgr::instance()->addPopup({text, ctx.sound});
}

bool Bfsar::loadStreamFileForReload_(StreamReloadContext& ctx)
{
    const Sound::StreamSoundInfo& strmSoundInfo = ctx.sound->getStreamSoundInfo();
    const char* path = strmSoundInfo.getPath().cstr();

    if (strmSoundInfo.getPath().isEmpty())
    {
        ReportStreamReloadError(ctx, messages::stream::cNoPath.text);
        return false;
    }

    if (!ctx.filePathOverride && !hasArchiveDirectory())
    {
        ReportStreamReloadError(ctx, messages::stream::cReloadNoArchiveFolder);
        return false;
    }

    const sead::FormatFixedSafeString<1024> notFound(messages::stream::cReloadNotFoundFormat, path);
    sead::FixedSafeString<1024> filePath;

    if (ctx.filePathOverride)
    {
        filePath.cutOffCopy(*ctx.filePathOverride);
    }
    else if (!resolveStreamFilePath(*ctx.sound, &filePath))
    {
        ReportStreamReloadError(ctx, notFound);
        return false;
    }

    if (sead::FileDeviceMgr::instance() != nullptr)
        ctx.device = sead::FileDeviceMgr::instance()->findDevice("native");

    if (!ctx.device)
    {
        ReportStreamReloadError(ctx, messages::import::cNativeDeviceUnavailable);
        return false;
    }

    sead::FileDevice::LoadArg loadArg;
    loadArg.path = filePath;

    if (filePath.calcLength() <= fileio::cPathLengthMax)
        ctx.fileData = ctx.device->tryLoad(loadArg);

    if (!ctx.fileData)
    {
        ReportStreamReloadError(ctx, notFound);
        return false;
    }

    ctx.fileSize = static_cast<u32>(loadArg.read_size);

    return true;
}

static bool ReadStreamReloadHeader(StreamReloadContext& ctx, Bfsar::StreamReloadResult* result)
{
    ctx.isOpus = opusstream::IsOpusStream(ctx.fileData, ctx.fileSize);

    if (ctx.isOpus)
    {
        opusstream::Info opusInfo;

        if (!opusstream::ReadInfoHeader(ctx.fileData, ctx.fileSize, opusInfo))
            return false;

        result->channelCount = opusInfo.channelCount;
        result->sampleRate = opusInfo.sampleRate;

        return true;
    }

    if (ctx.fileSize < cInnerFileMagicLength || !MatchesInnerFileKind(ctx.fileData, InnerFileKind::Stream) || !BfstmFile::IsComplete(ctx.fileData, ctx.fileSize))
        return false;

    ctx.reader.Initialize(ctx.fileData);

    if (!ctx.reader.IsAvailable() || !ctx.reader.ReadStreamSoundInfo(&ctx.fileInfo))
        return false;

    result->channelCount = ctx.fileInfo.channelCount;
    result->sampleRate = ctx.fileInfo.sampleRate;

    return true;
}

bool Bfsar::checkStreamFileForReload_(StreamReloadContext& ctx, StreamReloadResult* result) const
{
    const Sound::StreamSoundInfo& strmSoundInfo = ctx.sound->getStreamSoundInfo();
    const char* path = strmSoundInfo.getPath().cstr();

    const sead::FormatFixedSafeString<1024> unreadable(messages::stream::cReloadUnreadableFormat, path,
                                                       GetInnerFileDisplayName(mFormat, InnerFileKind::Stream));

    if (!ReadStreamReloadHeader(ctx, result))
    {
        ReportStreamReloadError(ctx, unreadable);
        return false;
    }

    if (result->channelCount == 0)
    {
        ReportStreamReloadError(ctx, sead::FormatFixedSafeString<1024>(messages::stream::cReloadNoChannelsFormat, path));
        return false;
    }

    if (result->channelCount > cStrmChannelNum)
    {
        ReportStreamReloadError(ctx, sead::FormatFixedSafeString<1024>(messages::stream::cReloadTooManyChannelsFormat, path,
                                                                       result->channelCount, cStrmChannelNum));
        return false;
    }

    ctx.streamType = ctx.isOpus ? Sound::StreamSoundInfo::StreamType::Opus
                                : Sound::StreamSoundInfo::StreamType::NwStreamBinary;

    if (strmSoundInfo.getStreamType() != ctx.streamType && !strmSoundInfo.isEnableStreamSoundExtension())
    {
        ReportStreamReloadError(ctx, sead::FormatFixedSafeString<1024>(messages::stream::cReloadTypeUnsupportedFormat, path,
                                                                       GetInnerFileDisplayName(mFormat, InnerFileKind::SoundArchive)));
        return false;
    }

    if (ctx.isOpus && !OpusStreamDecodes(ctx.fileData, ctx.fileSize))
    {
        ReportStreamReloadError(ctx, unreadable);
        return false;
    }

    return true;
}

bool Bfsar::stageStreamReload_(StreamReloadContext& ctx, const StreamReloadResult& result)
{
    const Sound::StreamSoundInfo& strmSoundInfo = ctx.sound->getStreamSoundInfo();
    const Sound::StreamSoundInfo::Track::List& tracks = strmSoundInfo.getTrackList();

    ctx.tracksFromFile = !ctx.isOpus && ctx.reader.IsTrackInfoAvailable();
    ctx.keepLayout = StreamTrackLayoutFits(strmSoundInfo, result.channelCount);
    ctx.trackCount = ctx.keepLayout ? static_cast<u32>(tracks.size()) : ChooseStreamTrackCount(static_cast<u32>(tracks.size()), result.channelCount);

    Sound* staged = new Sound();
    staged->setSoundType(Sound::SoundType::Strm);
    staged->setEnableName(ctx.sound->isEnableName());
    staged->getName() = ctx.sound->getName();

    ctx.staged = staged;

    Sound::StreamSoundInfo& stagedInfo = staged->getStreamSoundInfo();
    stagedInfo.getPath() = strmSoundInfo.getPath();
    stagedInfo.setEnableStreamSoundExtension(strmSoundInfo.isEnableStreamSoundExtension());
    stagedInfo.setStreamType(ctx.streamType);
    stagedInfo.setIsLoop(strmSoundInfo.getIsLoop());
    stagedInfo.setLoopStartFrame(strmSoundInfo.getLoopStartFrame());
    stagedInfo.setLoopEndFrame(strmSoundInfo.getLoopEndFrame());

    if (!ctx.isOpus && stagedInfo.isEnableStreamSoundExtension())
    {
        stagedInfo.setIsLoop(ctx.fileInfo.isLoop);
        stagedInfo.setLoopStartFrame(ctx.fileInfo.originalLoopStart);
        stagedInfo.setLoopEndFrame(ctx.fileInfo.originalLoopEnd);
    }

    bool readTracks = true;

    if (ctx.tracksFromFile)
    {
        readTracks = readEmbeddedStreamTracks(ctx.reader, stagedInfo, nullptr);
    }
    else
    {
        applyStreamTrackLayout(staged, ctx.trackCount, result.channelCount);

        if (ctx.keepLayout)
            CopyStreamChannelLayout(strmSoundInfo, &stagedInfo);
    }

    bool readWaves = false;

    if (ctx.isOpus)
    {
        ctx.unloadFile();
        readWaves = opusstream::AttachStreamWaves(staged, ctx.filePathOverride);
    }
    else
    {
        readWaves = ReadStreamWaves(staged, ctx.fileData, ctx.fileSize, nullptr);
        ctx.unloadFile();
    }

    if (readTracks && readWaves && StreamWavesCoverLayout(stagedInfo))
        return true;

    DiscardStagedSound(*this, staged);
    updateList(mWaveFileList);

    ReportStreamReloadError(ctx, sead::FormatFixedSafeString<1024>(messages::stream::cReloadUnreadableFormat, strmSoundInfo.getPath().cstr(),
                                                                   GetInnerFileDisplayName(mFormat, InnerFileKind::Stream)));
    return false;
}

void Bfsar::commitStreamReload_(StreamReloadContext& ctx, StreamReloadResult* result)
{
    Sound* sound = ctx.sound;
    Sound* staged = ctx.staged;

    Sound::StreamSoundInfo& strmSoundInfo = sound->getStreamSoundInfo();
    Sound::StreamSoundInfo& stagedInfo = staged->getStreamSoundInfo();

    sSoundPlayer.reset();

    detachStreamWaves(sound);

    const u32 stagedTrackCount = static_cast<u32>(stagedInfo.getTrackList().size());

    if (ctx.tracksFromFile)
    {
        result->tracksReplaced = !StreamTracksMatch(strmSoundInfo, stagedInfo);

        applyStreamTrackLayout(sound, stagedTrackCount, result->channelCount);
        CopyEmbeddedTrackInfo(stagedInfo, &strmSoundInfo);
    }
    else
    {
        if (!ctx.keepLayout)
        {
            applyStreamTrackLayout(sound, ctx.trackCount, result->channelCount);
            result->layoutChanged = true;
        }

        CopyStreamChannelLayout(stagedInfo, &strmSoundInfo);
    }

    MoveStreamWaves(&stagedInfo, &strmSoundInfo);

    ForgetRemovedItems({staged});
    delete staged;

    if (strmSoundInfo.getStreamType() != ctx.streamType)
    {
        strmSoundInfo.setStreamType(ctx.streamType);
        result->streamTypeChanged = true;
    }

    if (!ctx.isOpus && strmSoundInfo.isEnableStreamSoundExtension() &&
        (strmSoundInfo.getIsLoop() != ctx.fileInfo.isLoop ||
         strmSoundInfo.getLoopStartFrame() != ctx.fileInfo.originalLoopStart ||
         strmSoundInfo.getLoopEndFrame() != ctx.fileInfo.originalLoopEnd))
    {
        strmSoundInfo.setIsLoop(ctx.fileInfo.isLoop);
        strmSoundInfo.setLoopStartFrame(ctx.fileInfo.originalLoopStart);
        strmSoundInfo.setLoopEndFrame(ctx.fileInfo.originalLoopEnd);

        result->loopChanged = true;
    }

    result->trackCount = stagedTrackCount;

    strmSoundInfo.mAllocateTrackFlags = strmSoundInfo.getAllocateTrackFlags();
    strmSoundInfo.mAllocateChannelCount = ctx.isOpus ? static_cast<u16>(result->channelCount) : strmSoundInfo.getAllocateChannelCount();

    if (ctx.isOpus)
        sound->mStreamFileSignature = opusstream::ComputeContentSignature(*sound);
    else
        sound->mStreamFileSignature = BfstmFile::ComputeContentSignature(strmSoundInfo, getVersionForBfstm(), mEndian, mFormat);

    sound->mHasStreamFileBaseline = true;

    result->sharedSoundCount = shareStreamWavesWithSiblings(sound, ctx.tracksFromFile);
    result->succeeded = true;

    updateList(mWaveFileList);
}

Bfsar::StreamReloadResult Bfsar::reloadStreamSound(Sound* sound, const sead::SafeString* filePathOverride)
{
    StreamReloadResult result;

    if (!sound || sound->getSoundType() != Sound::SoundType::Strm)
        return result;

    StreamReloadContext ctx;
    ctx.sound = sound;
    ctx.filePathOverride = filePathOverride;

    if (!loadStreamFileForReload_(ctx) || !checkStreamFileForReload_(ctx, &result) || !stageStreamReload_(ctx, result))
        return result;

    commitStreamReload_(ctx, &result);

    return result;
}

u32 Bfsar::shareStreamWavesWithSiblings(Sound* source, bool tracksFromFile)
{
    const Sound::StreamSoundInfo& sourceInfo = source->getStreamSoundInfo();
    const Sound::StreamSoundInfo::Track::List& sourceTracks = sourceInfo.getTrackList();

    u32 sourceChannelCount = 0;

    for (s32 i = 0; i < sourceTracks.size(); i++)
    {
        const Sound::StreamSoundInfo::Track& sourceTrack = *static_cast<const Sound::StreamSoundInfo::Track*>(sourceTracks.nth(i)->val());
        sourceChannelCount += sourceTrack.getChannels_().size();
    }

    u32 sharedSoundCount = 0;

    for (Item* item : mSoundList)
    {
        Sound* sibling = static_cast<Sound*>(item);

        if (sibling == source || sibling->getSoundType() != Sound::SoundType::Strm)
            continue;

        Sound::StreamSoundInfo& siblingInfo = sibling->getStreamSoundInfo();

        if (siblingInfo.getPath() != sourceInfo.getPath())
            continue;

        detachStreamWaves(sibling);
        applyStreamTrackLayout(sibling, static_cast<u32>(sourceTracks.size()), sourceChannelCount);

        if (tracksFromFile)
            CopyEmbeddedTrackInfo(sourceInfo, &siblingInfo);
        else
            CopyStreamChannelLayout(sourceInfo, &siblingInfo);

        ReadStreamWaves(sibling, nullptr, 0, source);

        siblingInfo.copyAllocationFrom(sourceInfo);

        if (siblingInfo.isEnableStreamSoundExtension() && sourceInfo.isEnableStreamSoundExtension())
        {
            siblingInfo.setStreamType(sourceInfo.getStreamType());
            siblingInfo.setIsLoop(sourceInfo.getIsLoop());
            siblingInfo.setLoopStartFrame(sourceInfo.getLoopStartFrame());
            siblingInfo.setLoopEndFrame(sourceInfo.getLoopEndFrame());
        }

        sibling->copyStreamFileBaselineFrom(*source);
        sharedSoundCount++;
    }

    return sharedSoundCount;
}

static bool CopyStreamFileRaw_(const sead::SafeString &srcPath, const sead::SafeString &destPath)
{
    std::error_code ec;

    if (std::filesystem::equivalent(srcPath.cstr(), destPath.cstr(), ec))
        return true;

    return fileio::CopyFileOver(srcPath.cstr(), destPath.cstr());
}

static bool HasAttachedStreamWaves(const Sound::StreamSoundInfo &streamSoundInfo)
{
    const Sound::StreamSoundInfo::Track::List &trackList = streamSoundInfo.getTrackList();

    if (trackList.isEmpty())
        return false;

    for (u32 i = 0; i < trackList.size(); i++)
    {
        const Sound::StreamSoundInfo::Track &track = *static_cast<const Sound::StreamSoundInfo::Track *>(trackList.nth(i)->val());

        if (!track.getWaveFileRef().isAttached())
            return false;
    }

    return true;
}

static void ReportSkippedStreams(const std::vector<Bfsar::SkippedStream> &skipped)
{
    if (skipped.empty())
        return;

    PopupMgr::instance()->setErrorContext(PopupMgr::ErrorContext::Saving);

    for (const Bfsar::SkippedStream &stream : skipped)
    {
        PopupMgr::instance()->setCurrentProcessItem(const_cast<Sound *>(stream.sound));
        PopupMgr::instance()->pushCurrentItemWarning(stream.message);
    }

    PopupMgr::instance()->setCurrentProcessItem(nullptr);
}

void Bfsar::planNonStreamBinarySave(const Sound *sound, const sead::SafeString &archiveDir, const sead::SafeString &sourceDir, bool inPlace, std::unordered_set<std::string> &seen, std::vector<StreamSaveJob> &out, std::vector<SkippedStream> &outSkipped) const
{
    const Sound::StreamSoundInfo &strmSoundInfo = sound->getStreamSoundInfo();

    if (strmSoundInfo.getPath().isEmpty())
        return;

    const char *path = strmSoundInfo.getPath().cstr();

    if (seen.contains(path))
        return;

    if (strmSoundInfo.getStreamType() == Sound::StreamSoundInfo::StreamType::Opus && testhooks::IsOpusReencodeForced(path))
    {
        if (opusstream::AttachStreamWaves(const_cast<Sound *>(sound)))
            sound->mHasStreamFileBaseline = false;
    }

    if (strmSoundInfo.getStreamType() == Sound::StreamSoundInfo::StreamType::Opus)
    {
        if (HasAttachedStreamWaves(strmSoundInfo))
        {
            u64 sig = opusstream::ComputeContentSignature(*sound);

            if (!sound->mHasStreamFileBaseline || sig != sound->mStreamFileSignature)
            {
                StreamSaveJob job;
                job.sound = sound;
                job.signature = sig;
                job.savePath = std::string(archiveDir.cstr()) + "/" + path;

                seen.emplace(path);
                out.push_back(std::move(job));

                return;
            }
        }
    }

    if (inPlace || !mLoadedArchivePath)
        return;

    std::string sourcePath = std::string(sourceDir.cstr()) + "/" + path;

    std::error_code sourceError;

    if (!std::filesystem::exists(sourcePath, sourceError) || sourceError)
    {
        outSkipped.push_back({sound, messages::stream::cFileNotCopied});
        return;
    }

    StreamSaveJob job;
    job.sound = sound;
    job.signature = 0;
    job.savePath = std::string(archiveDir.cstr()) + "/" + path;
    job.srcPath = std::move(sourcePath);
    job.copyOnly = true;

    seen.emplace(path);
    out.push_back(std::move(job));
}

void Bfsar::planStreamSaves(std::vector<StreamSaveJob> &out, std::vector<SkippedStream> &outSkipped) const
{
    sead::FixedSafeString<512> archiveDir;

    if (!sead::Path::getDirectoryName(&archiveDir, getFilePath()))
        return;

    const bool inPlace = mLoadedArchivePath && getFilePath() == *mLoadedArchivePath;
    sead::FixedSafeString<512> sourceDir;

    if (!inPlace && mLoadedArchivePath)
        sead::Path::getDirectoryName(&sourceDir, *mLoadedArchivePath);

    std::unordered_set<std::string> seen;

    for (const Item *item : mSoundList)
    {
        const Sound *sound = static_cast<const Sound *>(item);

        if (sound->getSoundType() != Sound::SoundType::Strm)
            continue;

        const Sound::StreamSoundInfo &strmSoundInfo = sound->getStreamSoundInfo();

        if (strmSoundInfo.getStreamType() != Sound::StreamSoundInfo::StreamType::NwStreamBinary)
        {
            planNonStreamBinarySave(sound, archiveDir, sourceDir, inPlace, seen, out, outSkipped);
            continue;
        }

        if (strmSoundInfo.getPath().isEmpty())
        {
            outSkipped.push_back({sound, messages::stream::cNoPath});
            continue;
        }

        const char *path = strmSoundInfo.getPath().cstr();

        if (seen.contains(path))
            continue;

        u64 sig = BfstmFile::ComputeContentSignature(strmSoundInfo, getVersionForBfstm(), mEndian, mFormat);
        const bool dirty = !sound->mHasStreamFileBaseline || sig != sound->mStreamFileSignature || testhooks::IsStreamRewriteForced(path);

        StreamSaveJob job;
        job.sound = sound;
        job.signature = sig;
        job.savePath = std::string(archiveDir.cstr()) + "/" + path;

        if (!dirty)
        {
            if (inPlace)
                continue;

            job.srcPath = std::string(sourceDir.cstr()) + "/" + path;
            job.copyOnly = true;
        }
        else if (!HasAttachedStreamWaves(strmSoundInfo))
        {
            std::string sourcePath;

            if (!inPlace && mLoadedArchivePath)
                sourcePath = std::string(sourceDir.cstr()) + "/" + path;

            if (sourcePath.empty())
                continue;

            std::error_code sourceError;

            if (!std::filesystem::exists(sourcePath, sourceError) || sourceError)
            {
                outSkipped.push_back({sound, messages::stream::cFileNotCopied});
                continue;
            }

            job.srcPath = std::move(sourcePath);
            job.copyOnly = true;
        }

        seen.emplace(path);
        out.push_back(std::move(job));
    }
}

static constexpr size_t cMessagePathLengthMax = 200;

static std::string CutPathForMessage(const std::string &path)
{
    return path.size() > cMessagePathLengthMax ? path.substr(path.size() - cMessagePathLengthMax) : path;
}

bool Bfsar::writeStreamFile_(const Sound *sound, const sead::SafeString &path, bool *outReported) const
{
    sead::FileHandle handle;

    if (!fileio::OpenForWrite(&handle, path))
        return false;

    const Sound::StreamSoundInfo &strmSoundInfo = sound->getStreamSoundInfo();

    if (strmSoundInfo.getStreamType() == Sound::StreamSoundInfo::StreamType::Opus)
    {
        if (!opusstream::WriteStreamFile(handle, sound))
        {
            *outReported = true;
            return false;
        }
    }
    else if (!BfstmFile::WriteBfstmFile(handle, strmSoundInfo, getVersionForBfstm(), mEndian, mFormat))
    {
        return false;
    }

    return handle.tryClose();
}

static std::string MakePrefetchTempPath()
{
    static u32 sTempCount = 0;

    std::error_code error;
    const std::filesystem::path dir = std::filesystem::temp_directory_path(error);

    if (error)
        return std::string();

    const sead::FormatFixedSafeString<64> name("TuneBloom-prefetch-%llu-%u.tmp", static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()), sTempCount++);

    return (dir / name.cstr()).string();
}

bool Bfsar::buildStreamPrefetchFromFile_(const Sound *sound, const std::vector<StreamSaveJob> &jobs, std::vector<u8> *out) const
{
    const Sound::StreamSoundInfo &strmSoundInfo = sound->getStreamSoundInfo();
    const u32 bufferTimes = mSoundArchivePlayerInfo.streamBufferTimes;
    sead::FixedSafeString<512> archiveDir;

    if (strmSoundInfo.getPath().isEmpty() || !sead::Path::getDirectoryName(&archiveDir, getFilePath()))
        return false;

    const std::string savePath = std::string(archiveDir.cstr()) + "/" + strmSoundInfo.getPath().cstr();
    const auto job = std::find_if(jobs.begin(), jobs.end(), [&](const StreamSaveJob &other) { return other.savePath == savePath; });

    if (job == jobs.end())
        return BfstpFile::BuildFromStreamFile(out, savePath.c_str(), bufferTimes);

    if (job->copyOnly)
        return BfstpFile::BuildFromStreamFile(out, job->srcPath.c_str(), bufferTimes);

    const std::string tempPath = MakePrefetchTempPath();
    bool reported = false;

    if (tempPath.empty() || tempPath.size() > fileio::cPathLengthMax)
        return false;

    const bool built = writeStreamFile_(job->sound, tempPath.c_str(), &reported) && BfstpFile::BuildFromStreamFile(out, tempPath.c_str(), bufferTimes);

    fileio::RemoveFile(tempPath.c_str());

    return built;
}

bool Bfsar::buildStreamPrefetch_(const Sound *sound, const std::vector<StreamSaveJob> &jobs, std::vector<u8> *out) const
{
    const Sound::StreamSoundInfo &strmSoundInfo = sound->getStreamSoundInfo();
    const std::vector<u8> &source = strmSoundInfo.getPrefetchSource();

    const bool built = isStreamPrefetchBuildable() &&
                       strmSoundInfo.getStreamType() == Sound::StreamSoundInfo::StreamType::NwStreamBinary &&
                       buildStreamPrefetchFromFile_(sound, jobs, out);

    if (built && !source.empty() && *out != source)
        LOG_FMT("Prefetch data of '%s' differs from the prefetch file of the opened archive", sound->getNameOrNull().cstr());

    if (!built)
        *out = source;

    return !out->empty();
}

void Bfsar::executeStreamSave(const StreamSaveJob &job) const
{
    extern bool CreateDirectoryRecursively(const std::string &directory);

    Sound *sound = const_cast<Sound *>(job.sound);

    if (!job.copyOnly && sound->getStreamSoundInfo().getTrackList().size() == 0)
    {
        sead::FormatFixedSafeString<512> msg(messages::archive::cStreamNoTracksSkippedFormat,sound->getFormattedName().cstr());
        ReportArchiveError(mCliMode, {msg, sound});
        return;
    }

    const sead::FormatFixedSafeString<1024> writeFailed(messages::stream::cWriteFailedFormat, CutPathForMessage(job.savePath).c_str());
    const std::string dir = std::filesystem::path(job.savePath).parent_path().string();

    if (job.savePath.size() > fileio::cPathLengthMax || (!dir.empty() && !CreateDirectoryRecursively(dir)))
    {
        ReportArchiveError(mCliMode, {writeFailed, sound});
        return;
    }

    if (job.copyOnly)
    {
        if (CopyStreamFileRaw_(job.srcPath.c_str(), job.savePath.c_str()))
        {
            sound->mStreamFileSignature = job.signature;
            sound->mHasStreamFileBaseline = true;

            return;
        }

        if (sound->getStreamSoundInfo().getStreamType() != Sound::StreamSoundInfo::StreamType::NwStreamBinary ||
            !HasAttachedStreamWaves(sound->getStreamSoundInfo()))
        {
            sead::FormatFixedSafeString<1024> msg(messages::stream::cCopyFailedFormat, CutPathForMessage(job.srcPath).c_str());
            ReportArchiveError(mCliMode, {msg, sound});
            return;
        }
    }

    const std::string tempPath = job.savePath + ".tmp";
    bool reported = false;

    if (!writeStreamFile_(sound, tempPath.c_str(), &reported) || !fileio::RenameOver(tempPath.c_str(), job.savePath.c_str()))
    {
        fileio::RemoveFile(tempPath.c_str());

        if (!reported)
            ReportArchiveError(mCliMode, {writeFailed, sound});

        return;
    }

    sound->mStreamFileSignature = job.signature;
    sound->mHasStreamFileBaseline = true;
}

static bool IsSafeRelativePath_(const std::string &path)
{
    const std::filesystem::path relative(path);

    if (relative.is_absolute() || relative.has_root_name() || relative.has_root_directory())
        return false;

    for (const std::filesystem::path &part : relative)
    {
        if (part == "..")
            return false;
    }

    return true;
}

bool Bfsar::saveBackupStreams(const std::string &dir, const std::string &previousDir, const std::vector<BackupStream> &previous, std::vector<BackupStream> *out) const
{
    std::unordered_set<std::string> seen;
    std::error_code ec;

    for (const Item *item : mSoundList)
    {
        const Sound *sound = static_cast<const Sound *>(item);

        if (sound->getSoundType() != Sound::SoundType::Strm)
            continue;

        const Sound::StreamSoundInfo &strmSoundInfo = sound->getStreamSoundInfo();

        if (strmSoundInfo.getPath().isEmpty() || !HasAttachedStreamWaves(strmSoundInfo))
            continue;

        const std::string path = strmSoundInfo.getPath().cstr();

        if (seen.contains(path) || !IsSafeRelativePath_(path))
            continue;

        const bool isOpus = strmSoundInfo.getStreamType() == Sound::StreamSoundInfo::StreamType::Opus;
        const u64 signature = isOpus ? opusstream::ComputeContentSignature(*sound)
                                     : BfstmFile::ComputeContentSignature(strmSoundInfo, getVersionForBfstm(), mEndian, mFormat);

        if (sound->mHasStreamFileBaseline && signature == sound->mStreamFileSignature)
            continue;

        seen.emplace(path);

        const std::filesystem::path dest = std::filesystem::path(dir) / path;
        std::filesystem::create_directories(dest.parent_path(), ec);
        ec.clear();

        bool written = false;

        for (const BackupStream &entry : previous)
        {
            if (entry.path != path || entry.signature != signature)
                continue;

            const std::filesystem::path source = std::filesystem::path(previousDir) / path;

            std::filesystem::create_hard_link(source, dest, ec);

            if (ec)
            {
                ec.clear();
                std::filesystem::copy_file(source, dest, ec);
            }

            written = !ec;
            ec.clear();
            break;
        }

        bool reported = false;

        if (!written && !writeStreamFile_(sound, dest.string().c_str(), &reported))
            return false;

        const std::uintmax_t size = std::filesystem::file_size(dest, ec);

        if (ec || size == 0)
            return false;

        out->push_back({path, signature});
    }

    return true;
}

bool Bfsar::restoreStreamFromFile(const sead::SafeString &streamPath, const sead::SafeString &filePath)
{
    bool restored = false;

    for (Item *item : mSoundList)
    {
        Sound *sound = static_cast<Sound *>(item);

        if (sound->getSoundType() != Sound::SoundType::Strm || !(sound->getStreamSoundInfo().getPath() == streamPath))
            continue;

        if (!restored)
        {
            if (!reloadStreamSound(sound, &filePath).succeeded)
                return false;

            restored = true;
        }

        sound->mHasStreamFileBaseline = false;
    }

    return restored;
}

void Bfsar::saveStreamFiles_(std::vector<SkippedStream> &&skippedStreams)
{
    std::vector<StreamSaveJob> jobs;
    planStreamSaves(jobs, skippedStreams);

    for (const StreamSaveJob &job : jobs)
        executeStreamSave(job);

    ReportSkippedStreams(skippedStreams);

    LOG_FMT("Stream files written: %d, skipped: %d", (s32)jobs.size(), (s32)skippedStreams.size());
}
