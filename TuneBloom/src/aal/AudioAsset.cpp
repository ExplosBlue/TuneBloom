#include <aal/AudioAsset.h>

#include <aal/Amta.h>
#include <bfsar/audio/AudioProcessing.h>
#include <aal/Bfstm.h>
#include <aal/Bwav.h>
#include <core/ByteView.h>
#include <bfsar/audio/DecodedPcm.h>
#include <ui/Messages.h>
#include <ui/player/PlayerViews.h>
#include <bfsar/player/SoundPlayer.h>
#include <bfsar/model/StateHash.h>
#include <bfsar/files/WaveFile.h>
#include <bfsar/audio/WaveFileEditDecode.h>
#include <bfsar/audio/WavWriter.h>

#include <snd/snd_ElementType.h>

#include <algorithm>

static constexpr u32 cNwHeaderSize          = 0x14;
static constexpr u32 cNwHeaderByteOrder     = 0x04;
static constexpr u32 cNwHeaderFileSize      = 0x0C;
static constexpr u32 cNwHeaderBlockCount    = 0x10;
static constexpr u32 cNwBlockRefSize        = 0x0C;
static constexpr u32 cNwBlockRefOffset      = 0x04;
static constexpr u32 cNwBlockRefSizeField   = 0x08;
static constexpr u32 cNwBlockHeaderSize     = 0x08;
static constexpr u32 cNwReferenceSize       = 0x08;
static constexpr u32 cNwReferenceOffset     = 0x04;
static constexpr u32 cNwNullOffset          = 0xFFFFFFFF;
static constexpr u32 cFwavInfoLoopEnd       = 0x0C;
static constexpr u32 cFwavInfoChannelTable  = 0x14;
static constexpr u32 cFwavChannelInfoSize   = 0x14;
static constexpr u32 cFwavChannelAdpcmRef   = 0x08;
static constexpr u32 cFwavAdpcmInfoSize     = 0x2C;
static constexpr u32 cAdpcmFrameBytes       = 8;
static constexpr u32 cAdpcmFrameSamples     = 14;

struct NwBlock
{
    u64 offset = 0;
    u64 size = 0;
};

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

static bool FindNwBlock(const ByteView& view, u16 type, NwBlock* out)
{
    const u64 fileSize = view.readU32(cNwHeaderFileSize);
    const u32 count = view.readU16(cNwHeaderBlockCount);

    for (u32 i = 0; i < count; i++)
    {
        const u64 ref = cNwHeaderSize + static_cast<u64>(i) * cNwBlockRefSize;

        if (!view.has(ref, cNwBlockRefSize))
            return false;

        if (view.readU16(ref) != type)
            continue;

        out->offset = view.readU32(ref + cNwBlockRefOffset);
        out->size = view.readU32(ref + cNwBlockRefSizeField);

        return out->offset != cNwNullOffset && out->size >= cNwBlockHeaderSize && out->offset + out->size <= fileSize;
    }

    return false;
}

static u64 GetFwavSampleBytes(u8 encoding, u32 frames)
{
    switch (static_cast<WaveFile::Encoding>(encoding))
    {
        case WaveFile::Encoding::Pcm8:
            return frames;

        case WaveFile::Encoding::Pcm16:
            return static_cast<u64>(frames) * sizeof(s16);

        default:
            break;
    }

    return (static_cast<u64>(frames) + cAdpcmFrameSamples - 1) / cAdpcmFrameSamples * cAdpcmFrameBytes;
}

static bool IsFwavChannelInBounds(const ByteView& view, u64 channelInfo, u64 infoEnd, u64 dataBody, u64 dataEnd, u8 encoding, u32 frames)
{
    if (channelInfo + cFwavChannelInfoSize > infoEnd)
        return false;

    const u64 samples = dataBody + view.readU32(channelInfo + cNwReferenceOffset);

    if (samples + GetFwavSampleBytes(encoding, frames) > dataEnd)
        return false;

    if (static_cast<WaveFile::Encoding>(encoding) != WaveFile::Encoding::DspAdpcm)
        return true;

    return channelInfo + view.readU32(channelInfo + cFwavChannelAdpcmRef + cNwReferenceOffset) + cFwavAdpcmInfoSize <= infoEnd;
}

static bool IsFwavInBounds(const std::vector<u8>& file)
{
    if (file.size() < cNwHeaderSize || !ByteView::IsByteOrderMark(file.data(), file.size(), cNwHeaderByteOrder))
        return false;

    const ByteView view(file.data(), file.size(), ByteView::IsBigEndianMark(file.data(), file.size(), cNwHeaderByteOrder));

    NwBlock info;
    NwBlock data;

    if (view.readU32(cNwHeaderFileSize) > file.size() ||
        !FindNwBlock(view, nw::snd::internal::ElementType_WaveFile_InfoBlock, &info) ||
        !FindNwBlock(view, nw::snd::internal::ElementType_WaveFile_DataBlock, &data))
        return false;

    const u64 infoBody = info.offset + cNwBlockHeaderSize;
    const u64 infoEnd = info.offset + info.size;
    const u64 table = infoBody + cFwavInfoChannelTable;

    if (table + sizeof(u32) > infoEnd)
        return false;

    const u8 encoding = view.readU8(infoBody);
    const u32 frames = view.readU32(infoBody + cFwavInfoLoopEnd);
    const u32 channelCount = view.readU32(table);

    if (encoding > static_cast<u8>(WaveFile::Encoding::DspAdpcm) || channelCount == 0 || table + sizeof(u32) + static_cast<u64>(channelCount) * cNwReferenceSize > infoEnd)
        return false;

    const u64 dataBody = data.offset + cNwBlockHeaderSize;
    const u64 dataEnd = data.offset + data.size;

    for (u32 i = 0; i < std::min<u32>(channelCount, snd::cWaveChannelMax); i++)
    {
        const u64 channelInfo = table + view.readU32(table + sizeof(u32) + i * cNwReferenceSize + cNwReferenceOffset);

        if (!IsFwavChannelInBounds(view, channelInfo, infoEnd, dataBody, dataEnd, encoding, frames))
            return false;
    }

    return true;
}

WaveFile* ReadFwavFile(const std::vector<u8>& file)
{
    if (!IsFwavInBounds(file))
        return nullptr;

    WaveFile* wave = new WaveFile();

    if (!wave->read(file.data()) || wave->getChannels().isEmpty())
    {
        delete wave;
        return nullptr;
    }

    wave->setStandaloneTarget({ FindInnerFileFormat(file.data(), InnerFileKind::Wave)->format, wave->getVersion(), wave->getEndian() });

    return wave;
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

static void FillBfstmInfo(const aal::Bfstm& bfstm, AudioAssetInfo* info)
{
    info->channelCount = static_cast<u32>(bfstm.channels.size());
    info->sampleRate = bfstm.sampleRate;
    info->sampleCount = bfstm.getSourceSampleCount();
    info->storedSampleCount = bfstm.frameCount;
    info->isLoop = bfstm.isLoop;
    info->loopStartFrame = bfstm.isLoop ? bfstm.getSourceLoopStartFrame() : 0;
    info->loopEndFrame = info->sampleCount;
}

static OutputSide GetBwavSide(aal::BwavPan pan)
{
    switch (pan)
    {
        case aal::BwavPan::Left:
            return OutputSide::Left;

        case aal::BwavPan::Right:
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
    aal::Bfstm bfstm;
    const char* error = nullptr;

    if (!file || !aal::ReadBfstm(file->data(), file->size(), &bfstm, &error))
        return false;

    out->channels.resize(bfstm.channels.size());

    for (u32 i = 0; i < bfstm.channels.size(); i++)
    {
        aal::DecodeBfstmChannel(bfstm, i, &out->channels[i]);
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
            return ReadBfstmSource(file, out);

        default:
            break;
    }

    return false;
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
    const bool isRead = kind == AudioAssetKind::Stream ? ReadBfstmSource(file, &source) : kind == AudioAssetKind::Bwav && ReadBwavSource(file, &source);

    return isRead && BuildDecodedPcm(source, out);
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

    if (aal::IsBfstm(file.data(), file.size()))
        return AudioAssetKind::Stream;

    return AudioAssetKind::Unknown;
}

bool ReadAudioAssetBwav(const AudioAssetFile& file, aal::Bwav* out)
{
    const char* error = nullptr;

    return file && aal::ReadBwav(file->data(), file->size(), out, &error);
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
    , mFile()
    , mStreamFile()
    , mStreamFileState(StreamFileState::NotChecked)
    , mStreamFileName()
    , mIsStreamFileModified(false)
    , mIsInResetTable(false)
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
    hash.mixValue(mIsInResetTable);

    return hash.value();
}

const std::vector<u8>& AudioAsset::getMeta() const
{
    return mMeta;
}

void AudioAsset::setMeta(std::vector<u8> meta)
{
    mMeta = std::move(meta);
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
    return mStreamFile ? DetectAudioAssetKind(*mStreamFile) : mInfo.kind;
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

bool AudioAsset::isInResetTable() const
{
    return mIsInResetTable;
}

void AudioAsset::setInResetTable(bool isInResetTable)
{
    mIsInResetTable = isInResetTable;
}

const AudioAssetInfo& AudioAsset::getInfo() const
{
    return mInfo;
}

bool AudioAsset::isStream() const
{
    return mInfo.isPrefetch;
}

bool AudioAsset::getPlaybackError(sead::BufferedSafeString* outError) const
{
    switch (mInfo.kind)
    {
        case AudioAssetKind::None:
            outError->copy(messages::resource::cNoAudioFile);
            return true;

        case AudioAssetKind::Unknown:
            outError->copy(messages::resource::cFormatNotPlayable);
            return true;

        default:
            break;
    }

    if (mInfo.channelCount == 0)
    {
        outError->copy(messages::resource::cAudioDamaged);
        return true;
    }

    if (!mInfo.isPrefetch || mStreamFileState == StreamFileState::Loaded || mStreamFileState == StreamFileState::Found)
        return false;

    outError->copy(mStreamFileState == StreamFileState::Mismatched ? messages::resource::cStreamMismatched : messages::resource::cStreamNotLoaded);
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

    return ReadSourceAudio(mInfo.kind, getCompleteFile(), &source) && BuildDecodedPcm(source, out);
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
                samples[i] = AudioProcessing::restorePcm16(channel[i]);
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

    if (!ReadSourceAudio(mInfo.kind, getCompleteFile(), &source))
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
        return;

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
        aal::Bfstm bfstm;
        mInfo.isPrefetch = true;

        if (!aal::ReadBfstmInfo(mFile->data(), mFile->size(), &bfstm))
            return;

        FillBfstmInfo(bfstm, &mInfo);
        mInfo.prefetchSampleCount = bfstm.prefetchFrameCount;

        if (mStreamFile && aal::ReadBfstmInfo(mStreamFile->data(), mStreamFile->size(), &bfstm))
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
