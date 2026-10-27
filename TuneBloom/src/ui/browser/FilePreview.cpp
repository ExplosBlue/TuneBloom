#include <ui/browser/FilePreview.h>

#include <aal/AudioAsset.h>
#include <browser/FileProbe.h>
#include <ui/player/PlayerViews.h>
#include <bfsar/player/SoundPlayer.h>
#include <bfsar/files/WaveFile.h>

static WaveFile* BuildPcmWave(const DecodedPcm& pcm)
{
    WaveFile* wave = new WaveFile();

    if (wave->setupPreviewPcm16(pcm, pcm.isLoop, pcm.loopStartFrame, pcm.loopEndFrame))
        return wave;

    delete wave;
    return nullptr;
}

void DecodeAudioFileAt(DecodedAudioFile* file)
{
    if (!ReadFileContents(file->path, &file->bytes))
        return;

    if (DetectAudioAssetKind(file->bytes) == AudioAssetKind::Fwav)
    {
        file->isWaveFile = true;
        file->isDecoded = true;
        return;
    }

    const AudioAssetFile data = std::make_shared<const std::vector<u8>>(std::move(file->bytes));
    file->isDecoded = DecodeAudioFile(data, &file->pcm);
}

WaveFile* BuildDecodedWave(const DecodedAudioFile& file)
{
    if (!file.isDecoded)
        return nullptr;

    WaveFile* wave = file.isWaveFile ? ReadFwavFile(file.bytes) : BuildPcmWave(file.pcm);

    if (!wave)
        return nullptr;

    wave->setEnableName(true);
    wave->getName() = file.name.c_str();

    return wave;
}

void FilePreview::play(const std::string& path, const std::string& name)
{
    releaseWave();

    mRequestedPath = path;
    mRequestedName = name;
}

void FilePreview::stop()
{
    releaseWave();

    mRequestedPath.clear();
    mRequestedName.clear();
}

void FilePreview::update()
{
    if (mJob.finish() && mDecoding)
    {
        if (mDecoding->path == mRequestedPath)
        {
            startPlayback(*mDecoding);

            mRequestedPath.clear();
            mRequestedName.clear();
        }

        mDecoding.reset();
    }

    if (!mRequestedPath.empty() && !mJob.isBusy())
        startDecode();
}

bool FilePreview::isPlaying(const std::string& path) const
{
    return mWave && mWavePath == path && sSoundPlayer.isActive() && !sSoundPlayer.getPlayingSound() && sSoundPlayer.getPlayingWaveFile() == mWave;
}

void FilePreview::startDecode()
{
    mDecoding = std::make_unique<DecodedAudioFile>();
    mDecoding->path = mRequestedPath;
    mDecoding->name = mRequestedName;

    DecodedAudioFile* decoding = mDecoding.get();

    mJob.start([decoding]()
    {
        DecodeAudioFileAt(decoding);
    });
}

void FilePreview::startPlayback(const DecodedAudioFile& decoded)
{
    WaveFile* wave = BuildDecodedWave(decoded);

    if (!wave)
        return;

    sead::FixedSafeString<SoundPlayer::cPlaybackErrorLength> error;

    if (SoundPlayer::GetWaveFilePlaybackError(*wave, &error))
    {
        delete wave;
        return;
    }

    mWave = wave;
    mWavePath = decoded.path;

    if (sSoundPlayer.playWaveFile(*mWave, -1, nullptr, 0, false))
        sSoundPlayer.disownPlayback();
}

void FilePreview::releaseWave()
{
    if (!mWave)
        return;

    if (sSoundPlayer.getPlayingWaveFile() == mWave)
        sSoundPlayer.reset();

    delete mWave;
    mWave = nullptr;
    mWavePath.clear();
}
