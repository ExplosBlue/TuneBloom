#include <bfsar/files/LooseStreamFile.h>

#include <core/TestHooks.h>

#include <bfsar/files/BfstmFile.h>
#include <bfsar/io/FileIo.h>
#include <bfsar/io/PackFile.h>

#include <ui/Messages.h>

#include <snd/snd_StreamSoundFileReader.h>
#include <snd/StreamSoundPlayer.h>

#include <filedevice/seadPath.h>

static u32 GetTrackWaveVersion(ArchiveFormat format)
{
    return format == ArchiveFormat::BCSAR ? makeVersion(2, 1, 0) : makeVersion(0, 1, 2);
}

bool LooseStreamFile::IsStreamFile(const void* data, u32 size)
{
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
    const bool isStream = IsStreamFile(data, size);
    const nw::ut::BinaryFileHeader& header = *static_cast<const nw::ut::BinaryFileHeader*>(data);

    FileEndianScope endianScope(isStream ? nw::ut::GetFileEndian(header) : sFileEndian);

    if (!isStream || !BfstmFile::IsComplete(data, size))
    {
        outError->copy(messages::stream::cFileNotStream);
        return false;
    }

    mFilePath = path;
    sead::Path::getFileName(&mFileName, path);
    mTarget = { FindInnerFileFormat(data, InnerFileKind::Stream)->format, header.version, nw::ut::GetFileEndian(header) };

    nw::snd::internal::StreamSoundFileReader reader;
    nw::snd::internal::StreamSoundFile::StreamSoundInfo fileInfo;

    reader.Initialize(data);

    const bool isReadable = reader.IsAvailable() && reader.ReadStreamSoundInfo(&fileInfo) && fileInfo.channelCount != 0 && fileInfo.channelCount <= cStrmChannelNum;

    sead::FixedSafeString<256> stem;
    fileio::GetFileStem(&stem, path);

    if (!isReadable || !ReadTracks(reader, mStream, fileInfo.channelCount) || !ReadStreamWaves(*this, mStream, stem, data, size) || !StreamWavesCoverLayout(mStream))
    {
        outError->format(messages::stream::cFileUnreadableFormat, mFileName.cstr());
        return false;
    }

    mSavedSignature = BfstmFile::ComputeContentSignature(mStream, mTarget);

    return true;
}

void LooseStreamFile::create(const InnerFileTarget& target, const sead::SafeString& fileName)
{
    mFilePath.clear();
    mFileName = fileName;
    mTarget = target;

    mSavedSignature = BfstmFile::ComputeContentSignature(mStream, mTarget);
}

StreamSource LooseStreamFile::getStreamSource()
{
    StreamSource source;
    source.stream = &mStream;
    source.waveOwner = this;
    source.target = mTarget;
    source.fileName = mFileName.cstr();
    source.hasTrackMix = BfstmFile::IsTrackInfoAvailable(mTarget.version, mTarget.format);

    return source;
}

bool LooseStreamFile::isModified() const
{
    return BfstmFile::ComputeContentSignature(mStream, mTarget) != mSavedSignature;
}

void LooseStreamFile::prepareStreamWave(WaveFile* wave)
{
    const InnerFileTarget waveTarget = { mTarget.format, GetTrackWaveVersion(mTarget.format), mTarget.endian };

    wave->setStandaloneTarget(waveTarget);
    wave->setVersion(waveTarget.version);
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

    return BfstmFile::WriteBfstmFile(*file.getHandle(), mStream, mTarget) && file.commit();
}

SaveResult LooseStreamFile::save(sead::BufferedSafeString* outError)
{
    return saveAs(mFilePath, outError);
}

SaveResult LooseStreamFile::saveAs(const sead::SafeString& path, sead::BufferedSafeString* outError)
{
    const u64 signature = BfstmFile::ComputeContentSignature(mStream, mTarget);
    const bool isUnchanged = signature == mSavedSignature && !testhooks::IsStreamRewriteForced(path.cstr());
    const bool isSamePath = path == mFilePath;

    bool written = false;

    if (isUnchanged && isSamePath)
        written = true;
    else if (isUnchanged && !packfile::IsPackPath(mFilePath.cstr()))
        written = fileio::CopyFileOver(mFilePath.cstr(), path.cstr());

    if (!written && !writeTo(path))
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
