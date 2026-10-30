#include <ui/file/WavDocument.h>

#include <ui/file/LooseStreamDocument.h>
#include <messages/Messages.h>

#include <bfsar/archive/Bfsar.h>
#include <bfsar/files/LooseStreamFile.h>
#include <bfsar/model/ArchiveFormat.h>
#include <bfsar/audio/DecodedPcm.h>
#include <scan/FileProbe.h>
#include <bfsar/player/SoundPlayer.h>
#include <bfsar/files/WaveFile.h>
#include <snd/Global.h>
#include <snd/StreamSoundPlayer.h>

#include <filedevice/seadPath.h>

#include <utility>

static bool HasTrackLayout(u32 channelNum)
{
    return channelNum > snd::cWaveChannelMax && channelNum <= cStrmChannelNum;
}

static std::unique_ptr<LooseStreamFile> BuildTrackStream(std::vector<std::vector<float>> channels, const WaveFile::RiffWaveInfo& info, const sead::SafeString& name)
{
    std::unique_ptr<LooseStreamFile> tracks = std::make_unique<LooseStreamFile>();
    const char* error = nullptr;

    tracks->create(Bfsar::GetNewFileTarget(ArchiveFormat::BFSAR, ArchivePlatform::NX, InnerFileKind::Stream), Sound::StreamSoundInfo::StreamType::NwStreamBinary, name);

    if (!tracks->setAudioChannels(std::move(channels), info, WaveFile::Encoding::Pcm16, &error))
        return nullptr;

    return tracks;
}

Document* WavDocument::Open(File* file, const sead::SafeString& path)
{
    return OpenAs(new WavDocument(), file, path);
}

WavDocument::~WavDocument()
{
    if (mWave && sSoundPlayer.getPlayingWaveFile() == mWave)
        sSoundPlayer.stopAllPlayers();

    if (mTracks)
        ForgetLooseStream(mTracks->getStream());

    closeWindows();
}

Document::Kind WavDocument::kind() const
{
    return Kind::WavFile;
}

const sead::SafeString& WavDocument::filePath() const
{
    return mFilePath;
}

std::string WavDocument::displayName() const
{
    return mFileName.cstr();
}

BrowserFileKind WavDocument::fileKind() const
{
    return BrowserFileKind::Wav;
}

void WavDocument::drawInfo(UIType)
{
}

bool WavDocument::isPlayerOnly() const
{
    return true;
}

StreamSource WavDocument::activeStreamSource()
{
    return {};
}

PlayableFile WavDocument::playableFile()
{
    PlayableFile playable;

    if (mTracks)
        playable.stream = mTracks->getStreamSource();
    else
        playable.wave = mWave;

    return playable;
}

bool WavDocument::getFileFacts(FileFacts* out) const
{
    const char* bitsFormat = mIsFloat ? messages::import::cTrackImportBitsFloatFormat : messages::import::cTrackImportBitsFormat;

    SetWaveFacts(out, mTracks ? *mTracks->getStream().getMainWave() : *mWave);

    out->format = fileFormat();
    out->kindName = messages::common::cWaveFile;
    out->encoding.format(bitsFormat, static_cast<u32>(mBitsPerSample));
    out->channelNum = mChannelNum;
    out->trackNum = mTracks ? static_cast<u32>(mTracks->getStream().getTrackList().size()) : 0;
    out->bitrate = CalcPcmBitrate(out->sampleRate, mChannelNum, mBitsPerSample);

    return true;
}

void WavDocument::forEachItemList(const std::function<void(Item::List&)>& visit)
{
    visit(mWaveList);

    if (!mTracks)
        return;

    visit(mTracks->getStream().getTrackList());
    visit(mTracks->getStreamWaveList());
}

bool WavDocument::isInfoModified() const
{
    return false;
}

bool WavDocument::ownsItem(const Item& item) const
{
    if (item.list() == &mWaveList)
        return true;

    return mTracks && (item.list() == &mTracks->getStream().getTrackList() || item.list() == &mTracks->getStreamWaveList());
}

bool WavDocument::prepareSave()
{
    return false;
}

bool WavDocument::prepareSaveAs()
{
    return false;
}

SaveResult WavDocument::save(const char** outError)
{
    *outError = nullptr;

    return SaveResult::Failed;
}

SaveResult WavDocument::saveAs(const sead::SafeString&, const char** outError)
{
    return save(outError);
}

LooseFileFormat WavDocument::fileFormat() const
{
    LooseFileFormat format;
    format.name = messages::common::cWav;
    format.extension = cWavExtension;
    format.endian = sead::Endian::eLittle;

    return format;
}

bool WavDocument::openFile(const File& file, const sead::SafeString& path, sead::BufferedSafeString* outError)
{
    mFilePath = path;
    sead::Path::getFileName(&mFileName, path);

    DecodedPcm pcm;
    WaveFile::RiffWaveInfo info;

    if (!decodeRiffBytes(file.data, file.size, &pcm, &info))
    {
        outError->copy(messages::wavefile::cNotWavFile);
        return false;
    }

    mBitsPerSample = info.bitsPerSample;
    mChannelNum = info.numChannels;
    mIsFloat = info.format == cRiffFormatFloat;

    if (HasTrackLayout(info.numChannels))
    {
        mTracks = BuildTrackStream(std::move(pcm.channels), info, mFileName);

        if (mTracks)
            return true;

        decodeRiffBytes(file.data, file.size, &pcm);
    }

    mWave = BuildPcm16Wave(pcm);

    if (!mWave)
    {
        outError->copy(messages::wavefile::cNotWavFile);
        return false;
    }

    mWave->setEnableName(true);
    mWave->getName().copy(mFileName);
    mWave->setId(0);
    mWaveList.pushBack(mWave);

    return true;
}

bool WavDocument::writeTo(const sead::SafeString&) const
{
    return false;
}
