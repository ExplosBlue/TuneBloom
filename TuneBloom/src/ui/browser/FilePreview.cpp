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
            if (mDecoding->isDecoded)
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
    mDecoding = std::make_unique<DecodedFile>();
    mDecoding->path = mRequestedPath;
    mDecoding->name = mRequestedName;

    DecodedFile* decoding = mDecoding.get();

    mJob.start([decoding]()
    {
        if (!ReadFileContents(decoding->path, &decoding->bytes))
            return;

        if (DetectAudioAssetKind(decoding->bytes) == AudioAssetKind::Fwav)
        {
            decoding->isWaveFile = true;
            decoding->isDecoded = true;
            return;
        }

        const AudioAssetFile file = std::make_shared<const std::vector<u8>>(std::move(decoding->bytes));
        decoding->isDecoded = DecodeAudioFile(file, &decoding->pcm);
    });
}

void FilePreview::startPlayback(const DecodedFile& decoded)
{
    WaveFile* wave = decoded.isWaveFile ? ReadFwavFile(decoded.bytes) : BuildPcmWave(decoded.pcm);

    if (!wave)
        return;

    wave->setEnableName(true);
    wave->getName() = decoded.name.c_str();

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
