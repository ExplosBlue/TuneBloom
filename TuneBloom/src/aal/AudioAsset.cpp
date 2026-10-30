#include <aal/AudioAsset.h>

#include <aal/Amta.h>
#include <aal/AudioResource.h>
#include <bfsar/files/Bfstm.h>
#include <aal/Bwav.h>
#include <bfsar/audio/AudioProcessing.h>
#include <bfsar/audio/DecodedPcm.h>
#include <bfsar/audio/NxOpus.h>
#include <bfsar/audio/WavWriter.h>
#include <bfsar/audio/WaveFileEditDecode.h>
#include <bfsar/files/LooseWaveFile.h>
#include <bfsar/files/WaveFile.h>
#include <bfsar/model/StateHash.h>
#include <bfsar/player/SoundPlayer.h>
#include <messages/Messages.h>

#include <algorithm>

namespace
{

enum class OutputSide
{
    Left,
    Right,
    Both
};

struct SourceAudio
{
    u32 sampleRate = 0;
    u32 sampleCount = 0;
    bool isLoop = false;
    u32 loopStartFrame = 0;
    std::vector<std::vector<s16>> channels;
    std::vector<OutputSide> sides;
};

}

static void FillBwavInfo(const aal::Bwav& bwav, AudioAssetInfo* info)
{
    const aal::BwavChannel& channel = bwav.channels.front();

    info->channelCount = static_cast<u32>(bwav.channels.size());
    info->sampleRate = channel.sampleRate;
    info->sampleCount = channel.fullSampleCount;
    info->storedSampleCount = channel.sampleCount;
    info->isLoop = bwav.isLoop();
    info->loopStartFrame = info->isLoop ? channel.loopStartFrame : 0;
    info->loopEndFrame = info->isLoop ? channel.loopEndFrame : channel.fullSampleCount;
}

static void FillWaveFileInfo(const WaveFile& wave, AudioAssetInfo* info)
{
    info->channelCount = static_cast<u32>(wave.getChannels().size());
    info->sampleRate = wave.getSampleRate();
    info->sampleCount = wave.getSampleCount();
    info->storedSampleCount = wave.getLoopEndFrame(false);
    info->isLoop = wave.getIsLoop();
    info->loopStartFrame = wave.getOriginalLoopStartFrame();
    info->loopEndFrame = wave.getOriginalLoopEndFrame();
}

static void FillBfstmInfo(const bfstm::Bfstm& bfstm, AudioAssetInfo* info)
{
    info->channelCount = static_cast<u32>(bfstm.channels.size());
    info->sampleRate = bfstm.sampleRate;
    info->sampleCount = bfstm.getSourceSampleCount();
    info->storedSampleCount = bfstm.frameCount;
    info->isLoop = bfstm.isLoop;
    info->loopStartFrame = bfstm.isLoop ? bfstm.getSourceLoopStartFrame() : 0;
    info->loopEndFrame = info->sampleCount;
}

static void FillStreamFileInfo(const AudioAssetFile& stream, AudioAssetInfo* info)
{
    if (!stream)
        return;

    bfstm::Bfstm bfstm;
    aal::Bwav bwav;
    const AudioAssetKind kind = DetectAudioAssetKind(*stream);

    if (kind == AudioAssetKind::Stream && bfstm::ReadBfstmInfo(stream->data(), stream->size(), &bfstm))
        FillBfstmInfo(bfstm, info);
    else if (kind == AudioAssetKind::Bwav && ReadAudioAssetBwav(stream, &bwav) && aal::IsBwavComplete(bwav))
        FillBwavInfo(bwav, info);
    else
        return;

    info->kind = kind;
}

static OutputSide GetBwavSide(aal::BwavPan pan)
{
    switch (pan)
    {
        case aal::BwavPan::Left:
        case aal::BwavPan::RearLeft:
            return OutputSide::Left;

        case aal::BwavPan::Right:
        case aal::BwavPan::RearRight:
            return OutputSide::Right;

        default:
            break;
    }

    return OutputSide::Both;
}

static OutputSide GetPairedSide(u32 channelIndex, u32 channelCount)
{
    if (channelCount == 1)
        return OutputSide::Both;

    return channelIndex % 2 == 0 ? OutputSide::Left : OutputSide::Right;
}

static void TrimSourceAudio(SourceAudio* source)
{
    for (std::vector<s16>& channel : source->channels)
        channel.resize(source->sampleCount, 0);

    source->isLoop = source->isLoop && source->loopStartFrame < source->sampleCount;
    source->loopStartFrame = source->isLoop ? source->loopStartFrame : 0;
}

static bool ReadBwavSource(const AudioAssetFile& file, SourceAudio* out)
{
    aal::Bwav bwav;

    if (!ReadAudioAssetBwav(file, &bwav))
        return false;

    out->channels.resize(bwav.channels.size());

    for (u32 i = 0; i < bwav.channels.size(); i++)
    {
        aal::DecodeBwavChannel(bwav, i, &out->channels[i]);
        out->sides.push_back(GetBwavSide(bwav.channels[i].pan));
        out->sampleCount = i == 0 ? static_cast<u32>(out->channels[i].size()) : std::min<u32>(out->sampleCount, static_cast<u32>(out->channels[i].size()));
    }

    out->sampleRate = bwav.channels.front().sampleRate;
    out->isLoop = bwav.isLoop();
    out->loopStartFrame = bwav.channels.front().loopStartFrame;

    TrimSourceAudio(out);
    return out->sampleCount != 0;
}

static bool ReadBfstmSource(const AudioAssetFile& file, SourceAudio* out)
{
    bfstm::Bfstm bfstm;
    const char* error = nullptr;

    if (!file || !bfstm::ReadBfstm(file->data(), file->size(), &bfstm, &error))
        return false;

    out->channels.resize(bfstm.channels.size());

    for (u32 i = 0; i < bfstm.channels.size(); i++)
    {
        bfstm::DecodeBfstmChannel(bfstm, i, &out->channels[i]);
        out->sides.push_back(GetPairedSide(i, static_cast<u32>(bfstm.channels.size())));

        if (out->channels[i].size() < bfstm.getSourceSampleCount())
            return false;
    }

    out->sampleRate = bfstm.sampleRate;
    out->sampleCount = bfstm.getSourceSampleCount();
    out->isLoop = bfstm.isLoop;
    out->loopStartFrame = bfstm.getSourceLoopStartFrame();

    TrimSourceAudio(out);
    return out->sampleCount != 0;
}

static bool ReadSourceAudio(AudioAssetKind kind, const AudioAssetFile& file, SourceAudio* out)
{
    switch (kind)
    {
        case AudioAssetKind::Bwav:
            return ReadBwavSource(file, out);

        case AudioAssetKind::StreamPrefetch:
        case AudioAssetKind::Stream:
            return ReadBfstmSource(file, out);

        default:
            break;
    }

    return false;
}

static bool ReadPlayableSource(const AudioAsset& asset, SourceAudio* out)
{
    if (!ReadSourceAudio(asset.getInfo().kind, asset.getCompleteFile(), out))
        return false;

    if (out->channels.size() == 2 && aal::IsAmtaMidSide(asset.getMeta()))
        AudioProcessing::decodeMidSide(out->channels[0], out->channels[1]);

    return true;
}

static bool FeedsOutputChannel(OutputSide side, u32 output)
{
    return side == OutputSide::Both || static_cast<u32>(side) == output;
}

static std::vector<float> MixOutputChannel(const SourceAudio& source, u32 output)
{
    std::vector<float> mix(source.sampleCount, 0.0f);
    u32 sourceCount = 0;

    for (u32 i = 0; i < source.channels.size(); i++)
    {
        if (!FeedsOutputChannel(source.sides[i], output))
            continue;

        for (u32 j = 0; j < source.sampleCount; j++)
            mix[j] += AudioProcessing::fromPcm16(source.channels[i][j]);

        sourceCount++;
    }

    if (sourceCount > 1)
    {
        for (float& sample : mix)
            sample /= static_cast<float>(sourceCount);
    }

    return mix;
}

static std::vector<std::vector<float>> BuildOutputChannels(const SourceAudio& source)
{
    std::vector<std::vector<float>> channels;

    if (source.channels.size() > snd::cWaveChannelMax)
    {
        for (u32 output = 0; output < snd::cWaveChannelMax; output++)
            channels.push_back(MixOutputChannel(source, output));

        return channels;
    }

    for (const std::vector<s16>& samples : source.channels)
    {
        std::vector<float>& channel = channels.emplace_back(source.sampleCount, 0.0f);

        for (u32 j = 0; j < source.sampleCount; j++)
            channel[j] = AudioProcessing::fromPcm16(samples[j]);
    }

    return channels;
}

static bool BuildDecodedPcm(const SourceAudio& source, DecodedPcm* out)
{
    out->sampleRate = source.sampleRate;
    out->sampleCount = source.sampleCount;
    out->isLoop = source.isLoop;
    out->loopStartFrame = source.loopStartFrame;
    out->loopEndFrame = source.sampleCount;
    out->channels = BuildOutputChannels(source);

    return out->isValid();
}

bool DecodeAudioFile(const AudioAssetFile& file, DecodedPcm* out)
{
    *out = DecodedPcm();

    if (!file)
        return false;

    SourceAudio source;
    const AudioAssetKind kind = DetectAudioAssetKind(*file);

    return kind != AudioAssetKind::StreamPrefetch && ReadSourceAudio(kind, file, &source) && BuildDecodedPcm(source, out);
}

const char* GetAudioAssetKindName(AudioAssetKind kind)
{
    switch (kind)
    {
        case AudioAssetKind::Bwav:
            return GetResourceFormatInfo(ResourceFormat::Bwav).displayName;

        case AudioAssetKind::Fwav:
            return GetInnerFileDisplayName(ArchiveFormat::BFSAR, InnerFileKind::Wave);

        case AudioAssetKind::StreamPrefetch:
            return GetInnerFileDisplayName(ArchiveFormat::BFSAR, InnerFileKind::StreamPrefetch);

        case AudioAssetKind::Stream:
            return GetInnerFileDisplayName(ArchiveFormat::BFSAR, InnerFileKind::Stream);

        case AudioAssetKind::Unknown:
            return messages::common::cUnknown;

        default:
            break;
    }

    return messages::common::cNone;
}

const char* GetAudioAssetKindExtension(AudioAssetKind kind)
{
    switch (kind)
    {
        case AudioAssetKind::Bwav:
            return GetResourceFormatInfo(ResourceFormat::Bwav).extension;

        case AudioAssetKind::Fwav:
            return GetInnerFileExtension(ArchiveFormat::BFSAR, InnerFileKind::Wave);

        case AudioAssetKind::StreamPrefetch:
            return GetInnerFileExtension(ArchiveFormat::BFSAR, InnerFileKind::StreamPrefetch);

        case AudioAssetKind::Stream:
            return GetInnerFileExtension(ArchiveFormat::BFSAR, InnerFileKind::Stream);

        default:
            break;
    }

    return nullptr;
}

AudioAssetKind DetectAudioAssetKind(const std::vector<u8>& file)
{
    if (file.size() < cInnerFileMagicLength)
        return file.empty() ? AudioAssetKind::None : AudioAssetKind::Unknown;

    if (aal::IsBwav(file.data(), file.size()))
        return AudioAssetKind::Bwav;

    if (MatchesInnerFileKind(file.data(), InnerFileKind::Wave))
        return AudioAssetKind::Fwav;

    if (MatchesInnerFileKind(file.data(), InnerFileKind::StreamPrefetch))
        return AudioAssetKind::StreamPrefetch;

    if (bfstm::IsBfstm(file.data(), file.size()))
        return AudioAssetKind::Stream;

    return AudioAssetKind::Unknown;
}

bool ReadAudioAssetBwav(const AudioAssetFile& file, aal::Bwav* out)
{
    const char* error = nullptr;

    return file && aal::ReadBwav(file->data(), file->size(), out, &error);
}

bool GetAudioAssetBwavCodec(const AudioAsset& asset, aal::BwavCodec* out)
{
    const AudioAssetFile& file = asset.getFile();

    return asset.getInfo().kind == AudioAssetKind::Bwav && file && aal::ReadBwavCodec(file->data(), file->size(), out);
}

u32 GetAudioAssetOpusBitrate(const AudioAsset& asset)
{
    const AudioAssetFile& file = asset.getStreamFile() ? asset.getStreamFile() : asset.getFile();
    aal::Bwav bwav;

    if (asset.getInfo().kind != AudioAssetKind::Bwav || !ReadAudioAssetBwav(file, &bwav) || bwav.channels.empty())
        return 0;

    const aal::BwavChannel& channel = bwav.channels.front();

    if (channel.codec != aal::BwavCodec::Opus)
        return 0;

    return nxopus::MeasureChannelBitrate(channel.data.data(), channel.data.size());
}

static bool HasInfoOnlyInMeta(const AudioAsset& asset)
{
    return asset.getInfo().sampleRate == 0 && asset.isStream();
}

aal::AmtaSourceAudio GetAudioAssetLength(const AudioAsset& asset)
{
    const AudioAssetInfo& info = asset.getInfo();
    aal::AmtaSourceAudio length = { info.sampleRate, info.sampleCount };

    if (HasInfoOnlyInMeta(asset) && !aal::ReadAmtaLength(asset.getMeta(), &length))
        length = {};

    return length;
}

bool IsAudioAssetLoop(const AudioAsset& asset)
{
    return HasInfoOnlyInMeta(asset) ? aal::IsAmtaLoop(asset.getMeta()) : asset.getInfo().isLoop;
}

bool IsReadableFwav(const std::vector<u8>& file)
{
    WaveFile* wave = ReadFwavFile(file);
    const bool isReadable = wave != nullptr;

    delete wave;
    return isReadable;
}

AudioAsset::AudioAsset()
    : Item()
    , mMeta()
    , mMetaStreamKind(AudioAssetKind::None)
    , mFile()
    , mStreamFile()
    , mStreamFileState(StreamFileState::NotChecked)
    , mStreamFileName()
    , mIsStreamFileModified(false)
    , mRevision(0)
    , mInfo()
    , mPreviewWave(nullptr)
{
    mItemType = ItemType::AudioAsset;
    setEnableName(true);
}

AudioAsset::~AudioAsset()
{
    releasePreviewWave();
}

const AudioResource* AudioAsset::getResource() const
{
    const ItemList* ownerList = static_cast<const ItemList*>(list());

    return ownerList ? ownerList->getResource() : nullptr;
}

AudioResource* AudioAsset::getResource()
{
    ItemList* ownerList = static_cast<ItemList*>(list());

    return ownerList ? ownerList->getResource() : nullptr;
}

const Item* AudioAsset::validate(sead::BufferedSafeString& error) const
{
    std::string name;

    if (!mMeta.empty() && !aal::ReadAmtaName(mMeta, &name))
    {
        error = messages::resource::cAmtaDamaged;
        return this;
    }

    return nullptr;
}

bool AudioAsset::isStateTracked() const
{
    return true;
}

u64 AudioAsset::computeStateSignature() const
{
    StateHash hash;
    hash.mixName(*this);
    hash.mixBytes(mMeta.data(), mMeta.size());
    hash.mixValue(mRevision);

    return hash.value();
}

const std::vector<u8>& AudioAsset::getMeta() const
{
    return mMeta;
}

void AudioAsset::setMeta(std::vector<u8> meta)
{
    mMeta = std::move(meta);

    aal::AmtaAudioKind kind = aal::AmtaAudioKind::Bwav;
    const bool isStream = aal::IsAmtaStream(mMeta, &kind);

    mMetaStreamKind = !isStream ? AudioAssetKind::None : kind == aal::AmtaAudioKind::Bwav ? AudioAssetKind::Bwav : AudioAssetKind::Stream;
}

u16 AudioAsset::getMetaVersion() const
{
    return aal::GetAmtaVersion(mMeta);
}

const AudioAssetFile& AudioAsset::getFile() const
{
    return mFile;
}

const AudioAssetFile& AudioAsset::getCompleteFile() const
{
    return mStreamFile ? mStreamFile : mFile;
}

AudioAssetKind AudioAsset::getCompleteFileKind() const
{
    if (mStreamFile)
        return DetectAudioAssetKind(*mStreamFile);

    if (mStreamFileState == StreamFileState::Found && (mInfo.kind == AudioAssetKind::StreamPrefetch || !mFile))
        return getStreamFileKind();

    return mInfo.kind;
}

void AudioAsset::setFile(AudioAssetFile file)
{
    mFile = std::move(file);
    mRevision++;

    refreshInfo();
}

const AudioAssetFile& AudioAsset::getStreamFile() const
{
    return mStreamFile;
}

StreamFileState AudioAsset::getStreamFileState() const
{
    return mStreamFileState;
}

void AudioAsset::setStreamFile(AudioAssetFile file, StreamFileState state, bool isModified)
{
    mStreamFile = std::move(file);
    mStreamFileState = state;
    mIsStreamFileModified = isModified;

    if (isModified)
        mRevision++;
    else
        mStreamFileName = getName().cstr();

    refreshInfo();
}

bool AudioAsset::needsStreamFileWrite() const
{
    return mStreamFile && (mIsStreamFileModified || mStreamFileName != getName().cstr());
}

void AudioAsset::markStreamFileWritten()
{
    mIsStreamFileModified = false;
    mStreamFileName = getName().cstr();
}

const AudioAssetInfo& AudioAsset::getInfo() const
{
    return mInfo;
}

bool AudioAsset::isStream() const
{
    return mInfo.isPrefetch || mMetaStreamKind != AudioAssetKind::None;
}

AudioAssetKind AudioAsset::getStreamFileKind() const
{
    if (!mFile)
        return mMetaStreamKind == AudioAssetKind::None ? AudioAssetKind::Bwav : mMetaStreamKind;

    return mInfo.kind == AudioAssetKind::StreamPrefetch ? AudioAssetKind::Stream : AudioAssetKind::Bwav;
}

const char* GetStreamFileStateError(StreamFileState state)
{
    switch (state)
    {
        case StreamFileState::Missing:
            return messages::resource::cStreamFileMissing;

        case StreamFileState::Damaged:
            return messages::resource::cStreamDamaged;

        case StreamFileState::Mismatched:
            return messages::resource::cStreamMismatched;

        default:
            break;
    }

    return messages::resource::cStreamNotLoaded;
}

static const char* GetStreamPlaybackError(StreamFileState state)
{
    return state == StreamFileState::Loaded || state == StreamFileState::Found ? nullptr : GetStreamFileStateError(state);
}

bool AudioAsset::readsStreamFile() const
{
    const AudioResource* resource = getResource();

    return !resource || resource->holdsStreams();
}

const char* AudioAsset::findPlaybackError() const
{
    if (!mFile && isStream())
        return GetStreamPlaybackError(mStreamFileState);

    switch (mInfo.kind)
    {
        case AudioAssetKind::None:
            return messages::resource::cNoAudioFile;

        case AudioAssetKind::Unknown:
            return messages::resource::cFormatNotPlayable;

        default:
            break;
    }

    if (mInfo.channelCount == 0)
        return messages::resource::cAudioDamaged;

    if (!mInfo.isPrefetch)
        return nullptr;

    if (!readsStreamFile())
        return mInfo.prefetchSampleCount == 0 ? messages::resource::cBwavNeedsAudio : nullptr;

    return GetStreamPlaybackError(mStreamFileState);
}

const char* AudioAsset::getNoAudioReason() const
{
    return !mFile && isStream() ? GetStreamFileStateError(mStreamFileState) : messages::resource::cNoAudioFile;
}

bool AudioAsset::getPlaybackError(sead::BufferedSafeString* outError) const
{
    const char* error = findPlaybackError();

    if (!error)
        return false;

    outError->copy(error);
    return true;
}

bool AudioAsset::decode(DecodedPcm* out)
{
    *out = DecodedPcm();

    if (mInfo.kind == AudioAssetKind::Fwav)
    {
        const WaveFile* wave = getPreviewWave();

        if (wave)
            *out = decodeWaveFileForEditing(*wave);

        return out->isValid();
    }

    SourceAudio source;

    return ReadPlayableSource(*this, &source) && BuildDecodedPcm(source, out);
}

bool AudioAsset::decodePcm16(AudioAssetPcm* out)
{
    *out = AudioAssetPcm();

    if (mInfo.kind == AudioAssetKind::Fwav)
    {
        DecodedPcm pcm;

        if (!decode(&pcm))
            return false;

        out->sampleRate = pcm.sampleRate;
        out->isLoop = pcm.isLoop;
        out->loopStartFrame = pcm.loopStartFrame;

        for (const std::vector<float>& channel : pcm.channels)
        {
            std::vector<s16>& samples = out->channels.emplace_back(channel.size(), 0);

            for (u32 i = 0; i < channel.size(); i++)
                samples[i] = AudioProcessing::toPcm16(channel[i]);
        }

        return true;
    }

    SourceAudio source;

    if (!ReadSourceAudio(mInfo.kind, getCompleteFile(), &source))
        return false;

    out->sampleRate = source.sampleRate;
    out->isLoop = source.isLoop;
    out->loopStartFrame = source.loopStartFrame;
    out->channels = std::move(source.channels);

    return true;
}

bool AudioAsset::writeWavFile(const sead::SafeString& path)
{
    sead::FixedSafeString<SoundPlayer::cPlaybackErrorLength> error;

    if (getPlaybackError(&error))
        return false;

    if (mInfo.kind == AudioAssetKind::Fwav)
        return getPreviewWave() && mPreviewWave->writeWavFile(path);

    SourceAudio source;

    if (!ReadPlayableSource(*this, &source))
        return false;

    Pcm16Wav wav;
    wav.sampleRate = source.sampleRate;
    wav.channels = std::move(source.channels);
    wav.loop.enabled = source.isLoop;
    wav.loop.start = source.loopStartFrame;
    wav.loop.end = source.sampleCount;

    return WritePcm16Wav(path, wav);
}

bool AudioAsset::isPlaying() const
{
    return mPreviewWave && sSoundPlayer.isActive() && !sSoundPlayer.getPlayingSound() && sSoundPlayer.getPlayingWaveFile() == mPreviewWave;
}

const WaveFile* AudioAsset::getPreviewWave()
{
    if (!mPreviewWave)
        mPreviewWave = buildPreviewWave();

    return mPreviewWave;
}

void AudioAsset::releasePreviewWave()
{
    if (mPreviewWave && sSoundPlayer.getPlayingWaveFile() == mPreviewWave)
        sSoundPlayer.reset();

    delete mPreviewWave;
    mPreviewWave = nullptr;
}

WaveFile* AudioAsset::buildPreviewWave()
{
    WaveFile* wave = nullptr;

    if (mInfo.kind == AudioAssetKind::Fwav && mFile)
    {
        wave = ReadFwavFile(*mFile);
    }
    else
    {
        sead::FixedSafeString<SoundPlayer::cPlaybackErrorLength> error;
        DecodedPcm pcm;

        if (getPlaybackError(&error) || !decode(&pcm))
            return nullptr;

        wave = new WaveFile();

        if (!wave->setupPreviewPcm16(pcm, pcm.isLoop, pcm.loopStartFrame, pcm.loopEndFrame))
        {
            delete wave;
            return nullptr;
        }
    }

    if (wave)
    {
        wave->setEnableName(true);
        wave->getName() = getName();
    }

    return wave;
}

void AudioAsset::refreshInfo()
{
    releasePreviewWave();
    mInfo = AudioAssetInfo();

    if (!mFile)
    {
        FillStreamFileInfo(mStreamFile, &mInfo);
        return;
    }

    mInfo.kind = DetectAudioAssetKind(*mFile);
    mInfo.fileSize = static_cast<u32>(mFile->size());

    if (mInfo.kind == AudioAssetKind::Fwav)
    {
        mPreviewWave = buildPreviewWave();

        if (mPreviewWave)
            FillWaveFileInfo(*mPreviewWave, &mInfo);

        return;
    }

    if (mInfo.kind == AudioAssetKind::StreamPrefetch)
    {
        bfstm::Bfstm bfstm;
        mInfo.isPrefetch = true;

        if (!bfstm::ReadBfstmInfo(mFile->data(), mFile->size(), &bfstm))
            return;

        FillBfstmInfo(bfstm, &mInfo);
        mInfo.prefetchSampleCount = bfstm.prefetchFrameCount;

        if (mStreamFile && bfstm::ReadBfstmInfo(mStreamFile->data(), mStreamFile->size(), &bfstm))
            FillBfstmInfo(bfstm, &mInfo);

        return;
    }

    aal::Bwav bwav;

    if (mInfo.kind != AudioAssetKind::Bwav || !ReadAudioAssetBwav(mFile, &bwav))
        return;

    FillBwavInfo(bwav, &mInfo);
    mInfo.isPrefetch = !aal::IsBwavComplete(bwav);
    mInfo.prefetchSampleCount = mInfo.isPrefetch ? mInfo.storedSampleCount : 0;

    aal::Bwav stream;

    if (mInfo.isPrefetch && ReadAudioAssetBwav(mStreamFile, &stream) && aal::IsBwavComplete(stream))
        FillBwavInfo(stream, &mInfo);
}
