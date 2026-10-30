#include <bfsar/files/LooseStreamFile.h>

#include <core/TestHooks.h>

#include <bfsar/archive/Bfsar.h>
#include <bfsar/audio/DecodedPcm.h>
#include <bfsar/audio/TempWavWriter.h>
#include <bfsar/files/BfstmFile.h>
#include <bfsar/files/OpusStream.h>
#include <bfsar/io/FileIo.h>
#include <bfsar/io/PackFile.h>

#include <messages/Messages.h>
#include <bfsar/model/ModelEvents.h>

#include <snd/snd_StreamSoundFileReader.h>
#include <snd/StreamSoundPlayer.h>

#include <filedevice/seadPath.h>

#include <algorithm>
#include <vector>

static u32 GetTrackWaveVersion(ArchiveFormat format)
{
    return format == ArchiveFormat::BCSAR ? makeVersion(2, 1, 0) : makeVersion(0, 1, 2);
}

bool LooseStreamFile::IsStreamFile(const void* data, u32 size)
{
    if (opusstream::IsOpusStream(data, size))
        return true;

    return size >= sizeof(nw::ut::BinaryFileHeader) && FindInnerFileFormat(data, InnerFileKind::Stream) != nullptr;
}

static bool ReadTracks(nw::snd::internal::StreamSoundFileReader& reader, Sound::StreamSoundInfo& stream, u32 channelCount)
{
    if (reader.IsTrackInfoAvailable())
        return ReadStreamFileTracks(reader, stream, nullptr);

    ApplyStreamTrackLayout(stream, GetDefaultStreamTrackCount(channelCount), channelCount);

    return true;
}

bool LooseStreamFile::open(const void* data, u32 size, const sead::SafeString& path, sead::BufferedSafeString* outError)
{
    mFilePath = path;
    sead::Path::getFileName(&mFileName, path);

    const bool opened = opusstream::IsOpusStream(data, size) ? openOpus(data, size, outError) : openStream(data, size, outError);

    if (opened)
        mSavedSignature = computeSignature();

    return opened;
}

void LooseStreamFile::setStreamType(Sound::StreamSoundInfo::StreamType streamType)
{
    mStream.setEnableStreamSoundExtension(streamType != Sound::StreamSoundInfo::StreamType::NwStreamBinary);
    mStream.setStreamType(streamType);
}

bool LooseStreamFile::openOpus(const void* data, u32 size, sead::BufferedSafeString* outError)
{
    mTarget = Bfsar::GetNewFileTarget(ArchiveFormat::BFSAR, ArchivePlatform::NX, InnerFileKind::Stream);
    setStreamType(Sound::StreamSoundInfo::StreamType::Opus);

    sead::FixedSafeString<256> stem;
    fileio::GetFileStem(&stem, mFilePath);

    const char* error = nullptr;

    if (!opusstream::ReadStreamFile(*this, mStream, stem, data, size, &error))
    {
        if (error)
            outError->format("%s", error);
        else
            outError->format(messages::stream::cFileUnreadableFormat, mFileName.cstr());

        return false;
    }

    return true;
}

bool LooseStreamFile::openStream(const void* data, u32 size, sead::BufferedSafeString* outError)
{
    const bool isStream = size >= sizeof(nw::ut::BinaryFileHeader) && FindInnerFileFormat(data, InnerFileKind::Stream) != nullptr;
    const nw::ut::BinaryFileHeader& header = *static_cast<const nw::ut::BinaryFileHeader*>(data);

    FileEndianScope endianScope(isStream ? nw::ut::GetFileEndian(header) : sFileEndian);

    if (!isStream || !BfstmFile::IsComplete(data, size))
    {
        outError->copy(messages::stream::cFileNotStream);
        return false;
    }

    mTarget = { FindInnerFileFormat(data, InnerFileKind::Stream)->format, header.version, nw::ut::GetFileEndian(header) };

    nw::snd::internal::StreamSoundFileReader reader;
    nw::snd::internal::StreamSoundFile::StreamSoundInfo fileInfo;

    reader.Initialize(data);

    const bool isReadable = reader.IsAvailable() && reader.ReadStreamSoundInfo(&fileInfo) && fileInfo.channelCount != 0 && fileInfo.channelCount <= cStrmChannelNum;

    sead::FixedSafeString<256> stem;
    fileio::GetFileStem(&stem, mFilePath);

    if (!isReadable || !ReadTracks(reader, mStream, fileInfo.channelCount) || !ReadStreamWaves(*this, mStream, stem, data, size) || !StreamWavesCoverLayout(mStream))
    {
        outError->format(messages::stream::cFileUnreadableFormat, mFileName.cstr());
        return false;
    }

    return true;
}

void LooseStreamFile::create(const InnerFileTarget& target, Sound::StreamSoundInfo::StreamType streamType, const sead::SafeString& fileName)
{
    mFilePath.clear();
    mFileName = fileName;
    mTarget = target;
    setStreamType(streamType);

    mSavedSignature = computeSignature();
}

StreamSource LooseStreamFile::getStreamSource()
{
    StreamSource source;
    source.stream = &mStream;
    source.waveOwner = this;
    source.target = mTarget;
    source.fileName = mFileName.cstr();
    source.hasTrackMix = !isOpus() && BfstmFile::IsTrackInfoAvailable(mTarget.version, mTarget.format);

    return source;
}

bool LooseStreamFile::isOpus() const
{
    return mStream.getStreamType() == Sound::StreamSoundInfo::StreamType::Opus;
}

u64 LooseStreamFile::computeSignature() const
{
    return isOpus() ? opusstream::ComputeContentSignature(mStream) : BfstmFile::ComputeContentSignature(mStream, mTarget);
}

bool LooseStreamFile::isModified() const
{
    return computeSignature() != mSavedSignature;
}

void LooseStreamFile::prepareStreamWave(WaveFile* wave)
{
    const InnerFileTarget waveTarget = { mTarget.format, GetTrackWaveVersion(mTarget.format), mTarget.endian };

    wave->setStandaloneTarget(waveTarget);
    wave->setVersion(waveTarget.version);
    wave->setStreamTarget(mTarget);
}

void LooseStreamFile::nameStreamWave(WaveFile* wave, const sead::SafeString&, u32 trackNo, u32)
{
    wave->setEnableName(true);
    wave->getName().format(messages::stream::cTrackNameFormat, trackNo);
}

bool LooseStreamFile::writeTo(const sead::SafeString& path) const
{
    fileio::StagedFile file;

    if (!file.open(path))
        return false;

    const bool written = isOpus() ? opusstream::WriteStreamFile(*file.getHandle(), mStream, nullptr) : BfstmFile::WriteBfstmFile(*file.getHandle(), mStream, mTarget);

    return written && file.commit();
}

static Sound::StreamSoundInfo::Track& GetTrack(Sound::StreamSoundInfo& stream, s32 trackNo)
{
    return *static_cast<Sound::StreamSoundInfo::Track*>(stream.getTrackList().nth(trackNo)->val());
}

static WaveFile* GetTrackWave(Sound::StreamSoundInfo& stream, s32 trackNo)
{
    return static_cast<WaveFile*>(GetTrack(stream, trackNo).getWaveFileRef().getItem());
}

bool LooseStreamFile::setLoop(bool isLoop, u32 loopStartFrame, const char** outError)
{
    const WaveFile* mainWave = mStream.getMainWave();

    if (!mainWave || (isLoop && loopStartFrame >= mainWave->getSampleCount()))
    {
        *outError = messages::wavefile::cInvalidLoopRange;
        return false;
    }

    for (s32 trackNo = 0; trackNo < mStream.getTrackList().size(); trackNo++)
    {
        WaveFile* wave = GetTrackWave(mStream, trackNo);

        wave->setLoop(isLoop, isLoop ? loopStartFrame : 0, wave->getSampleCount());
        wave->updateLoop();
    }

    return true;
}

void LooseStreamFile::setChannelCount(u32 channelCount)
{
    const u32 trackCount = GetDefaultStreamTrackCount(channelCount);
    std::vector<Item*> removedWaves;

    for (s32 trackNo = static_cast<s32>(trackCount); trackNo < mStream.getTrackList().size(); trackNo++)
        removedWaves.push_back(GetTrackWave(mStream, trackNo));

    ApplyStreamTrackLayout(mStream, trackCount, channelCount);
    modelevents::ItemsRemoved(removedWaves);

    for (Item* wave : removedWaves)
        delete wave;

    for (u32 trackNo = 0; trackNo < trackCount; trackNo++)
    {
        Sound::StreamSoundInfo::Track& track = GetTrack(mStream, static_cast<s32>(trackNo));

        if (track.getWaveFileRef().isAttached())
            continue;

        WaveFile* wave = new WaveFile();
        prepareStreamWave(wave);
        nameStreamWave(wave, mFileName, trackNo, trackCount);
        addStreamWave(wave);
        track.getWaveFileRef().attach(wave);
    }

    Bfsar::updateList(mWaveList);
}

static bool ReadTrackAudio(WaveFile* wave, const std::vector<std::vector<float>>& channels, const WaveFile::RiffWaveInfo& source, u32 sampleCount, WaveFile::Encoding encoding)
{
    const sead::FormatFixedSafeString<fileio::cPathLengthMax> tempPath("%s/tunebloom_stream_track_%p.wav", fileio::GetTempFolder(), static_cast<const void*>(wave));

    WaveFile::RiffWaveInfo info;
    const bool isWritten = TempWavWriter::writeInfo(channels, source.sampleRate, tempPath.cstr(), &info);

    info.isLoop = source.isLoop;
    info.loopStartFrame = source.isLoop ? source.loopStartFrame : 0;
    info.loopEndFrame = sampleCount;

    const bool isRead = isWritten && wave->readWavFile(info, encoding);
    fileio::RemoveFile(tempPath.cstr());

    return isRead;
}

bool LooseStreamFile::replaceAudio(const WaveFile::RiffWaveInfo& info, const char** outError)
{
    const WaveFile* mainWave = mStream.getMainWave();

    if (!mainWave)
    {
        *outError = messages::resource::cAudioDamaged;
        return false;
    }

    return setAudio(info, mainWave->getEncoding(), outError);
}

static size_t CountTrackChannels(Sound::StreamSoundInfo& stream)
{
    size_t count = 0;

    for (s32 trackNo = 0; trackNo < stream.getTrackList().size(); trackNo++)
        count += GetTrack(stream, trackNo).getChannels_().size();

    return count;
}

bool LooseStreamFile::setAudio(const WaveFile::RiffWaveInfo& info, WaveFile::Encoding encoding, const char** outError)
{
    std::vector<std::vector<float>> channels;

    if (!decodeRiffChannels(info, &channels) || channels.empty())
    {
        *outError = messages::import::cWaveReadFailed;
        return false;
    }

    if (channels.size() > cStrmChannelNum)
    {
        *outError = messages::resource::cStreamChannelLimit;
        return false;
    }

    const u32 sampleCount = info.isLoop ? std::min(info.loopEndFrame, info.sampleCount) : info.sampleCount;

    if (sampleCount == 0 || (info.isLoop && info.loopStartFrame >= sampleCount))
    {
        *outError = messages::wavefile::cInvalidLoopRange;
        return false;
    }

    const bool keepsLayout = channels.size() == mStream.getAllocateChannelCount();

    if (keepsLayout && CountTrackChannels(mStream) > channels.size())
    {
        *outError = messages::resource::cEncodeFailed;
        return false;
    }

    for (std::vector<float>& channel : channels)
        channel.resize(sampleCount, 0.0f);

    if (!keepsLayout)
        setChannelCount(static_cast<u32>(channels.size()));

    u32 firstChannel = 0;

    for (s32 trackNo = 0; trackNo < mStream.getTrackList().size(); trackNo++)
    {
        WaveFile* wave = GetTrackWave(mStream, trackNo);
        const u32 channelCount = static_cast<u32>(GetTrack(mStream, trackNo).getChannels_().size());
        const std::vector<std::vector<float>> trackChannels(channels.begin() + firstChannel, channels.begin() + firstChannel + channelCount);

        if (!ReadTrackAudio(wave, trackChannels, info, sampleCount, encoding))
        {
            *outError = messages::resource::cEncodeFailed;
            return false;
        }

        firstChannel += channelCount;
    }

    WaveFile* mainWave = mStream.getMainWave();
    mainWave->getStreamFileLayout().removeRegionsAfter(mainWave->getSampleCount());

    return true;
}

SaveResult LooseStreamFile::save(sead::BufferedSafeString* outError)
{
    return saveAs(mFilePath, outError);
}

SaveResult LooseStreamFile::saveAs(const sead::SafeString& path, sead::BufferedSafeString* outError)
{
    const u64 signature = computeSignature();
    const bool isUnchanged = signature == mSavedSignature && !testhooks::IsStreamRewriteForced(path.cstr());
    const bool isCopied = isUnchanged && packfile::CopyFileOver(mFilePath.cstr(), path.cstr());

    if (!isCopied && !writeTo(path))
    {
        sead::FixedSafeString<256> fileName;
        sead::Path::getFileName(&fileName, path);

        outError->format(messages::stream::cWriteFailedFormat, fileName.cstr());
        return SaveResult::Failed;
    }

    mFilePath = path;
    sead::Path::getFileName(&mFileName, path);
    mSavedSignature = signature;

    return SaveResult::Saved;
}
