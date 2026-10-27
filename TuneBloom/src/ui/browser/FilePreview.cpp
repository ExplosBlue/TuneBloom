#include <ui/browser/FilePreview.h>

#include <aal/AudioAsset.h>
#include <browser/FileProbe.h>
#include <ui/player/PlayerViews.h>
#include <bfsar/player/SoundPlayer.h>
#include <bfsar/files/LooseStreamFile.h>
#include <bfsar/files/LooseWaveFile.h>
#include <bfsar/files/WaveFile.h>

static WaveFile* BuildPcmWave(const DecodedPcm& pcm)
{
    WaveFile* wave = new WaveFile();

    if (wave->setupPreviewPcm16(pcm, pcm.isLoop, pcm.loopStartFrame, pcm.loopEndFrame))
        return wave;

    delete wave;
    return nullptr;
}

void DecodeAudioBytes(DecodedAudioFile* file)
{
    if (LooseWaveFile::IsWaveFile(file->bytes.data(), static_cast<u32>(file->bytes.size())))
    {
        file->isWaveFile = true;
        file->isDecoded = true;
        return;
    }

    const AudioAssetFile data = std::make_shared<const std::vector<u8>>(std::move(file->bytes));
    file->isDecoded = DecodeAudioFile(data, &file->pcm);
}

static void ReadPreviewFileAt(DecodedAudioFile* file)
{
    if (!ReadFileContents(file->path, &file->bytes))
        return;

    if (LooseStreamFile::IsStreamFile(file->bytes.data(), static_cast<u32>(file->bytes.size())))
    {
        file->isStream = true;
        file->isDecoded = true;
        return;
    }

    DecodeAudioBytes(file);
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

FilePreview::FilePreview() = default;

FilePreview::~FilePreview() = default;

void FilePreview::play(const std::string& path, const std::string& name)
{
    releaseWave();

    if (sSoundPlayer.isActive())
        sSoundPlayer.reset();

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
    if (mWavePath != path || !sSoundPlayer.isActive())
        return false;

    if (mStream)
        return sSoundPlayer.getPlayingStream() == &mStream->getStream();

    return mWave && !sSoundPlayer.getPlayingSound() && sSoundPlayer.getPlayingWaveFile() == mWave;
}

void FilePreview::startDecode()
{
    mDecoding = std::make_unique<DecodedAudioFile>();
    mDecoding->path = mRequestedPath;
    mDecoding->name = mRequestedName;

    DecodedAudioFile* decoding = mDecoding.get();

    mJob.start([decoding]()
    {
        ReadPreviewFileAt(decoding);
    });
}

void FilePreview::startPlayback(DecodedAudioFile& decoded)
{
    if (decoded.isStream)
    {
        if (startStream(decoded))
            return;

        decoded.isStream = false;
        DecodeAudioBytes(&decoded);
    }

    startWave(decoded);
}

bool FilePreview::startStream(const DecodedAudioFile& decoded)
{
    std::unique_ptr<LooseStreamFile> stream = std::make_unique<LooseStreamFile>();
    sead::FixedSafeString<256> error;

    if (!stream->open(decoded.bytes.data(), static_cast<u32>(decoded.bytes.size()), sead::FixedSafeString<512>(decoded.path.c_str()), &error))
        return false;

    sead::FixedSafeString<SoundPlayer::cPlaybackErrorLength> playbackError;

    if (SoundPlayer::GetStreamPlaybackError(stream->getStreamSource(), &playbackError))
        return true;

    mStream = std::move(stream);
    mWavePath = decoded.path;

    if (sSoundPlayer.playStream(mStream->getStreamSource()))
        sSoundPlayer.disownPlayback();

    return true;
}

void FilePreview::startWave(const DecodedAudioFile& decoded)
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
    if (mStream)
    {
        sSoundPlayer.forgetStream(&mStream->getStream());
        mStream.reset();
    }

    if (mWave)
    {
        if (sSoundPlayer.getPlayingWaveFile() == mWave)
            sSoundPlayer.reset();

        delete mWave;
        mWave = nullptr;
    }

    mWavePath.clear();
}
