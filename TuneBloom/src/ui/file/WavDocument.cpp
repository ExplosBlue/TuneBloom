#include <ui/file/WavDocument.h>

#include <messages/Messages.h>

#include <bfsar/model/ArchiveFormat.h>
#include <bfsar/audio/DecodedPcm.h>
#include <scan/FileProbe.h>
#include <bfsar/player/SoundPlayer.h>
#include <bfsar/files/WaveFile.h>

#include <filedevice/seadPath.h>

Document* WavDocument::Open(File* file, const sead::SafeString& path)
{
    return OpenAs(new WavDocument(), file, path);
}

WavDocument::~WavDocument()
{
    if (mWave && sSoundPlayer.getPlayingWaveFile() == mWave)
        sSoundPlayer.stopAllPlayers();

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
    playable.wave = mWave;

    return playable;
}

bool WavDocument::getFileFacts(FileFacts* out) const
{
    const char* bitsFormat = mIsFloat ? messages::import::cTrackImportBitsFloatFormat : messages::import::cTrackImportBitsFormat;

    SetWaveFacts(out, *mWave);

    out->format = fileFormat();
    out->kindName = messages::common::cWaveFile;
    out->encoding.format(bitsFormat, static_cast<u32>(mBitsPerSample));
    out->channelNum = mChannelNum;
    out->bitrate = CalcPcmBitrate(out->sampleRate, mChannelNum, mBitsPerSample);

    return true;
}

void WavDocument::forEachItemList(const std::function<void(Item::List&)>& visit)
{
    visit(mWaveList);
}

bool WavDocument::isInfoModified() const
{
    return false;
}

bool WavDocument::ownsItem(const Item& item) const
{
    return item.list() == &mWaveList;
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

    mWave = decodeRiffBytes(file.data, file.size, &pcm, &info) ? BuildPcm16Wave(pcm) : nullptr;

    if (!mWave)
    {
        outError->copy(messages::wavefile::cNotWavFile);
        return false;
    }

    mWave->setEnableName(true);
    mWave->getName().copy(mFileName);
    mWave->setId(0);
    mWaveList.pushBack(mWave);

    mBitsPerSample = info.bitsPerSample;
    mChannelNum = info.numChannels;
    mIsFloat = info.format == cRiffFormatFloat;

    return true;
}

bool WavDocument::writeTo(const sead::SafeString&) const
{
    return false;
}
