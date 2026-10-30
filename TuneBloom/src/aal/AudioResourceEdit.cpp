#include <aal/AudioResource.h>

#include <aal/Amta.h>
#include <aal/Bars.h>
#include <bfsar/files/Bfstm.h>
#include <aal/Bwav.h>
#include <bfsar/audio/AudioProcessing.h>
#include <bfsar/audio/DecodedPcm.h>
#include <bfsar/files/BfstpFile.h>
#include <bfsar/files/LooseStreamFile.h>
#include <bfsar/files/LooseWaveFile.h>
#include <bfsar/io/FileIo.h>
#include <core/ByteView.h>
#include <messages/Messages.h>

#include <algorithm>
#include <memory>

static constexpr u32 cNewFwavVersion  = 0x00010200;
static constexpr u32 cNewBfstmVersion = 0x00040000;

static constexpr u32 cNewSarcWaveOnlyFwavVersion = 0x00010100;
static constexpr u32 cFwavHeaderByteOrder        = 0x04;
static constexpr u32 cFwavHeaderVersion          = 0x08;

static constexpr u32 cLastSampleDropTests = 16;
static constexpr s64 cLastSampleDropExact = 1000;

static constexpr InnerFileTarget cNewBfstmTarget = { ArchiveFormat::BFSAR, cNewBfstmVersion, sead::Endian::eLittle };

static void UpdateMetaLoudness(AudioAsset* asset, AudioResource* resource, std::vector<u8>* meta)
{
    AudioAssetPcm pcm;

    if (!asset->decodePcm16(&pcm))
        return;

    const bool isAmta5 = aal::GetAmtaVersion(*meta) >= aal::cAmtaVersion5;
    const bool hasIntegerMeanSquare = asset->getInfo().kind != AudioAssetKind::Bwav;
    const bool dropsLastSample = isAmta5 && resource && resource->dropsLastSample();
    const f32 quietTailLevel = isAmta5 && resource ? resource->getQuietTailLevel() : aal::cAmtaQuietTailLevels[0];
    const aal::AmtaLoudness loudness = isAmta5 ? aal::MeasureAmta5Loudness(pcm.channels, pcm.sampleRate, pcm.isLoop, pcm.loopStartFrame, dropsLastSample, quietTailLevel)
                                               : aal::MeasureAmta4Loudness(pcm.channels, pcm.sampleRate, hasIntegerMeanSquare);

    aal::WriteAmtaLoudness(meta, loudness);
}

static s64 CountEqualBytes(const std::vector<u8>& left, const std::vector<u8>& right)
{
    s64 count = 0;

    for (size_t i = 0; i < left.size() && i < right.size(); i++)
        count += left[i] == right[i];

    return count;
}

static bool HoldsWholeAudio(const AudioAsset& asset)
{
    const AudioAssetInfo& info = asset.getInfo();

    return info.sampleCount != 0 && (!info.isPrefetch || asset.getStreamFileState() == StreamFileState::Loaded);
}

static bool ScoreLastSampleDrop(AudioAsset* asset, f32 quietTailLevel, s64* outScore)
{
    const std::vector<u8>& stored = asset->getMeta();
    AudioAssetPcm pcm;

    if (aal::GetAmtaVersion(stored) < aal::cAmtaVersion5 || !aal::CanWriteAmta5Loudness(stored) || !HoldsWholeAudio(*asset) || !asset->decodePcm16(&pcm) || pcm.channels.empty() || pcm.channels.front().empty())
        return false;

    std::vector<u8> kept = stored;
    std::vector<u8> dropped = stored;

    if (!aal::WriteAmtaLoudness(&kept, aal::MeasureAmta5Loudness(pcm.channels, pcm.sampleRate, pcm.isLoop, pcm.loopStartFrame, false, quietTailLevel)) ||
        !aal::WriteAmtaLoudness(&dropped, aal::MeasureAmta5Loudness(pcm.channels, pcm.sampleRate, pcm.isLoop, pcm.loopStartFrame, true, quietTailLevel)))
        return false;

    if ((kept == stored) != (dropped == stored))
        *outScore = dropped == stored ? cLastSampleDropExact : -cLastSampleDropExact;
    else
        *outScore = CountEqualBytes(dropped, stored) - CountEqualBytes(kept, stored);

    return true;
}

static size_t FindQuietTailLevel(const Item::List& assets)
{
    u32 matches[std::size(aal::cAmtaQuietTailLevels)] = {};

    for (const Item* item : assets)
    {
        const AudioAsset* asset = static_cast<const AudioAsset*>(item);
        const u32 sampleCount = GetAudioAssetLength(*asset).sampleCount;
        std::vector<aal::AmtaEnvelopePoint> envelope;
        u32 storedFrames = 0;

        if (sampleCount == 0 || !aal::ReadAmtaQuietTail(asset->getMeta(), &storedFrames, &envelope))
            continue;

        for (size_t i = 0; i < std::size(matches); i++)
            matches[i] += aal::CalcAmtaQuietTailFrames(envelope, sampleCount, aal::cAmtaQuietTailLevels[i]) == storedFrames;
    }

    return std::max_element(std::begin(matches), std::end(matches)) - std::begin(matches);
}

static bool HoldsOpusAudio(const AudioAsset& asset)
{
    aal::BwavCodec codec = aal::BwavCodec::DspAdpcm;

    return GetAudioAssetBwavCodec(asset, &codec) && codec == aal::BwavCodec::Opus;
}

static void WriteMetaFromInfo(AudioAsset* asset, AudioResource* resource, bool measuresLoudness)
{
    const AudioAssetInfo& info = asset->getInfo();
    std::vector<u8> meta = asset->getMeta();

    if (meta.empty())
        return;

    aal::AmtaWaveFields fields;
    fields.sampleRate = info.sampleRate;
    fields.storedSampleCount = !measuresLoudness ? 0 : info.kind == AudioAssetKind::Bwav ? info.sampleCount : info.storedSampleCount;
    fields.sampleCount = info.sampleCount;
    fields.channelCount = info.channelCount;
    fields.isLoop = info.isLoop;
    fields.loopStartFrame = info.loopStartFrame;
    fields.isOpus = HoldsOpusAudio(*asset);
    fields.isStream = asset->isStream();

    if (!aal::UpdateAmtaWaveFields(&meta, fields))
        return;

    if (measuresLoudness)
        UpdateMetaLoudness(asset, resource, &meta);

    asset->setMeta(std::move(meta));
}

static void UpdateMetaFromInfo(AudioAsset* asset)
{
    WriteMetaFromInfo(asset, asset->getResource(), aal::HasAmtaLoudness(asset->getMeta()));
}

template <typename Write>
static bool WriteThroughTempFile(const void* source, Write write, std::vector<u8>* out)
{
    const sead::FormatFixedSafeString<fileio::cPathLengthMax> tempPath("%s/tunebloom_resource_%p.bin", fileio::GetTempFolder(), source);

    if (!write(tempPath))
        return false;

    const bool isRead = fileio::ReadWholeFile(tempPath.cstr(), out);
    fileio::RemoveFile(tempPath.cstr());

    return isRead && !out->empty();
}

static bool StoreWaveFile(AudioAsset* asset, const WaveFile& wave, const char** outError)
{
    std::vector<u8> bytes;

    if (!WriteThroughTempFile(&wave, [&wave](const sead::SafeString& path) { return wave.writeFile(path); }, &bytes))
    {
        *outError = messages::resource::cEncodeFailed;
        return false;
    }

    asset->setFile(MakeAudioAssetFile(std::move(bytes)));
    UpdateMetaFromInfo(asset);

    return true;
}

static bool SetWaveFileLoop(AudioAsset* asset, bool isLoop, u32 loopStartFrame, const char** outError)
{
    const AudioAssetFile file = asset->getFile();
    std::unique_ptr<WaveFile> wave(ReadFwavFile(*file));

    if (!wave)
    {
        *outError = messages::resource::cAudioDamaged;
        return false;
    }

    const u32 sampleCount = wave->getSampleCount();

    if (isLoop && loopStartFrame >= sampleCount)
    {
        *outError = messages::wavefile::cInvalidLoopRange;
        return false;
    }

    const InnerFileTarget target = wave->getTarget();
    const u32 alignment = WaveFile::isOriginalLoopAvailable(target.format, target.version) ? 0 : wave->getLoopStartAlignmentFrames(false);

    if (alignment > 1)
        loopStartFrame -= loopStartFrame % alignment;

    wave->setLoop(isLoop, isLoop ? loopStartFrame : 0, sampleCount);
    wave->updateLoop();

    return StoreWaveFile(asset, *wave, outError);
}

static bool ReadWavPcm(const WaveFile::RiffWaveInfo& info, std::vector<std::vector<s16>>* out, u32* outLoopStartFrame, const char** outError)
{
    std::vector<std::vector<float>> channels;

    if (!decodeRiffChannels(info, &channels))
    {
        *outError = messages::import::cWaveReadFailed;
        return false;
    }

    const u32 sampleCount = info.isLoop ? std::min(info.loopEndFrame, info.sampleCount) : info.sampleCount;
    *outLoopStartFrame = info.isLoop ? info.loopStartFrame : 0;

    if (sampleCount == 0 || (info.isLoop && *outLoopStartFrame >= sampleCount))
    {
        *outError = messages::wavefile::cInvalidLoopRange;
        return false;
    }

    out->assign(channels.size(), std::vector<s16>(sampleCount, 0));

    for (u32 i = 0; i < out->size(); i++)
    {
        for (u32 j = 0; j < sampleCount; j++)
            (*out)[i][j] = AudioProcessing::toPcm16(channels[i][j]);
    }

    return true;
}

static void SetStreamAudio(AudioAsset* asset, const aal::Bwav& full)
{
    aal::Bwav current;
    ReadAudioAssetBwav(asset->getFile(), &current);

    const aal::BwavCodec codec = full.channels.empty() ? aal::BwavCodec::DspAdpcm : full.channels.front().codec;
    const u32 prefetchSamples = current.channels.empty() ? aal::GetBwavPrefetchSampleCount(codec) : current.channels.front().sampleCount;
    const u16 prefetchFlag = current.channels.empty() ? 1 : current.prefetchFlag;

    asset->setStreamFile(MakeAudioAssetFile(aal::WriteBwav(full)), StreamFileState::Loaded, true);

    if (asset->getFile())
        asset->setFile(MakeAudioAssetFile(aal::WriteBwav(aal::MakeBwavPrefetch(full, prefetchSamples, prefetchFlag))));
}

static void SetStreamLoop(AudioAsset* asset, const aal::Bwav& full)
{
    aal::Bwav prefetch;
    ReadAudioAssetBwav(asset->getFile(), &prefetch);
    aal::CopyBwavLoop(full, &prefetch);

    asset->setStreamFile(MakeAudioAssetFile(aal::WriteBwav(full)), StreamFileState::Loaded, true);

    if (asset->getFile())
        asset->setFile(MakeAudioAssetFile(aal::WriteBwav(prefetch)));
}

static bool SetBfstmFiles(AudioAsset* asset, std::vector<u8> stream, bool isBuilt, std::vector<u8> prefetch, const char** outError)
{
    if (!isBuilt)
    {
        *outError = messages::resource::cPrefetchBuildFailed;
        return false;
    }

    asset->setStreamFile(MakeAudioAssetFile(std::move(stream)), StreamFileState::Loaded, true);
    asset->setFile(MakeAudioAssetFile(std::move(prefetch)));

    return true;
}

static bool SetBfstmStreamAudio(AudioAsset* asset, std::vector<u8> stream, const char** outError)
{
    if (!asset->getFile())
    {
        asset->setStreamFile(MakeAudioAssetFile(std::move(stream)), StreamFileState::Loaded, true);
        return true;
    }

    const std::vector<u8>& current = *asset->getFile();
    std::vector<u8> prefetch;
    const bool isBuilt = BfstpFile::Rebuild(&prefetch, stream.data(), stream.size(), current.data(), current.size());

    return SetBfstmFiles(asset, std::move(stream), isBuilt, std::move(prefetch), outError);
}

static bool EncodeFwavAudio(AudioAsset* asset, const InnerFileTarget& target, const WaveFile::RiffWaveInfo& info, WaveFile::Encoding encoding, const char** outError)
{
    if (!IsPlainPcm(info))
    {
        *outError = messages::resource::cAudioFormatBfwavWavReason;
        return false;
    }

    WaveFile wave;
    wave.setStandaloneTarget(target);

    if (!wave.readWavFile(info, encoding))
    {
        *outError = messages::import::cWaveImportFailed;
        return false;
    }

    wave.updateLoop();

    return StoreWaveFile(asset, wave, outError);
}

static bool EncodeBwavAudio(AudioAsset* asset, const WaveFile::RiffWaveInfo& info, const AudioAssetEncoding& encoding, bool isBigEndian, bool isStream, bool isMidSide, const char** outError)
{
    aal::BwavFormat format;
    format.codec = encoding.bwavCodec;
    format.opusChannelBitrate = encoding.opusChannelBitrate;
    format.sampleRate = info.sampleRate;
    format.isLoop = info.isLoop;
    format.isBigEndian = isBigEndian;

    std::vector<std::vector<s16>> pcm;
    aal::Bwav full;

    if (!ReadWavPcm(info, &pcm, &format.loopStartFrame, outError))
        return false;

    if (isMidSide && pcm.size() == 2)
        AudioProcessing::encodeMidSide(pcm[0], pcm[1]);

    if (!aal::EncodeBwav(pcm, format, &full, outError))
        return false;

    if (isStream)
        SetStreamAudio(asset, full);
    else
        asset->setFile(MakeAudioAssetFile(aal::WriteBwav(full)));

    UpdateMetaFromInfo(asset);

    return true;
}

static bool OpenStreamFile(const AudioAsset& asset, LooseStreamFile* out, const char** outError)
{
    const AudioAssetFile& file = asset.getStreamFile();
    sead::FixedSafeString<256> openError;

    if (file && out->open(file->data(), static_cast<u32>(file->size()), asset.getName(), &openError))
        return true;

    *outError = messages::resource::cAudioDamaged;
    return false;
}

static bool WriteStreamBytes(const LooseStreamFile& stream, std::vector<u8>* out, const char** outError)
{
    if (WriteThroughTempFile(&stream, [&stream](const sead::SafeString& path) { return stream.writeTo(path); }, out))
        return true;

    *outError = messages::resource::cEncodeFailed;
    return false;
}

static bool StoreStreamFile(AudioAsset* asset, const LooseStreamFile& stream, const char** outError)
{
    std::vector<u8> bytes;

    return WriteStreamBytes(stream, &bytes, outError) && SetBfstmStreamAudio(asset, std::move(bytes), outError);
}

static const AudioAsset* FindStreamPrefetchAsset(const Item::List& assets)
{
    for (const Item* item : assets)
    {
        const AudioAsset* asset = static_cast<const AudioAsset*>(item);

        if (asset->getInfo().kind == AudioAssetKind::StreamPrefetch && asset->getFile())
            return asset;
    }

    return nullptr;
}

static bool EncodeNewBfstmAudio(AudioAsset* asset, const AudioAsset* sample, const WaveFile::RiffWaveInfo& info, WaveFile::Encoding encoding, const char** outError)
{
    LooseStreamFile stream;
    stream.create(cNewBfstmTarget, Sound::StreamSoundInfo::StreamType::NwStreamBinary, sead::SafeString::cEmptyString);

    std::vector<u8> bytes;

    if (!stream.setAudio(info, encoding, outError) || !WriteStreamBytes(stream, &bytes, outError))
        return false;

    const AudioAssetFile sampleFile = sample ? sample->getFile() : nullptr;
    std::vector<u8> prefetch;
    const bool isBuilt = BfstpFile::BuildForResource(&prefetch, bytes.data(), bytes.size(), sampleFile ? sampleFile->data() : nullptr, sampleFile ? sampleFile->size() : 0);

    return SetBfstmFiles(asset, std::move(bytes), isBuilt, std::move(prefetch), outError);
}

static bool EncodeBfstmAudio(AudioAsset* asset, const WaveFile::RiffWaveInfo& info, bool clearsRegions, const char** outError)
{
    LooseStreamFile stream;

    if (!OpenStreamFile(*asset, &stream, outError) || !stream.replaceAudio(info, outError))
        return false;

    if (clearsRegions)
        stream.getStream().getMainWave()->getStreamFileLayout().regions.clear();

    if (!StoreStreamFile(asset, stream, outError))
        return false;

    UpdateMetaFromInfo(asset);
    return true;
}

static bool SetBfstmStreamLoop(AudioAsset* asset, bool isLoop, u32 loopStartFrame, const char** outError)
{
    LooseStreamFile stream;

    return OpenStreamFile(*asset, &stream, outError) && stream.setLoop(isLoop, loopStartFrame, outError) && StoreStreamFile(asset, stream, outError);
}

bool AudioResource::canCreateNewAsset() const
{
    return mContainer == Container::Bars || mContainer == Container::Sarc;
}

bool AudioResource::hasNewAssetMeta() const
{
    if (mContainer != Container::Sarc)
        return true;

    for (const Item* item : mAssets)
    {
        const AudioAsset* asset = static_cast<const AudioAsset*>(item);

        if (!asset->getMeta().empty())
            return true;

        if (asset->getFile())
            return false;
    }

    return mNewAssetHasMeta;
}

u32 AudioResource::getNewFwavVersion() const
{
    if (mContainer != Container::Sarc)
        return cNewFwavVersion;

    for (const Item* item : mAssets)
    {
        const AudioAsset* asset = static_cast<const AudioAsset*>(item);
        const AudioAssetFile& file = asset->getFile();

        if (asset->getInfo().kind != AudioAssetKind::Fwav || !file || file->size() < cFwavHeaderVersion + sizeof(u32))
            continue;

        return ByteView(file->data(), file->size(), ByteView::IsBigEndianMark(file->data(), file->size(), cFwavHeaderByteOrder)).readU32(cFwavHeaderVersion);
    }

    return hasNewAssetMeta() ? cNewFwavVersion : cNewSarcWaveOnlyFwavVersion;
}

bool AudioResource::canCreateStreamAsset() const
{
    const AudioAssetKind kind = getNewAssetKind();
    const bool hasPrefetchForm = kind == AudioAssetKind::Fwav && mVersion == aal::cBarsVersion11 && !mIsBigEndian;

    return canCreateNewAsset() && (kind == AudioAssetKind::Bwav || hasPrefetchForm);
}

AudioAssetKind AudioResource::getNewAssetKind() const
{
    for (const Item* item : mAssets)
    {
        const AudioAssetKind kind = static_cast<const AudioAsset*>(item)->getInfo().kind;

        if (kind == AudioAssetKind::Bwav)
            return AudioAssetKind::Bwav;

        if (kind == AudioAssetKind::Fwav || kind == AudioAssetKind::StreamPrefetch)
            return AudioAssetKind::Fwav;
    }

    return mNewAssetKind;
}

bool AudioResource::canChooseNewAssetKind() const
{
    return canCreateNewAsset() && mVersion == aal::cBarsVersion11 && mAssets.isEmpty();
}

void AudioResource::setNewAssetKind(AudioAssetKind kind)
{
    SEAD_ASSERT(kind == AudioAssetKind::Fwav || kind == AudioAssetKind::Bwav);

    if (canChooseNewAssetKind())
        mNewAssetKind = kind;
}

bool AudioResource::isNewMetaBigEndian() const
{
    for (const Item* item : mAssets)
    {
        const std::vector<u8>& meta = static_cast<const AudioAsset*>(item)->getMeta();

        if (!meta.empty())
            return aal::IsAmtaBigEndian(meta);
    }

    return mIsBigEndian;
}

u32 AudioResource::getNewMetaInfoMask() const
{
    for (const Item* item : mAssets)
    {
        const u32 mask = aal::GetNewAmta5InfoMask(static_cast<const AudioAsset*>(item)->getMeta());

        if (mask != 0)
            return mask;
    }

    return 0;
}

AudioAsset* AudioResource::createNewAsset(const WaveFile::RiffWaveInfo& info, const AudioAssetEncoding& encoding, bool isStream, const char** outError)
{
    if (!canCreateNewAsset() || (isStream && !canCreateStreamAsset()))
    {
        *outError = messages::resource::cAddUnsupported;
        return nullptr;
    }

    const AudioAssetKind kind = getNewAssetKind();
    const bool isMetaBigEndian = isNewMetaBigEndian();
    std::unique_ptr<AudioAsset> asset(new AudioAsset());

    bool isEncoded = false;

    if (kind == AudioAssetKind::Bwav)
        isEncoded = EncodeBwavAudio(asset.get(), info, encoding, mIsBigEndian, isStream, false, outError);
    else if (isStream)
        isEncoded = EncodeNewBfstmAudio(asset.get(), FindStreamPrefetchAsset(mAssets), info, encoding.fwavEncoding, outError);
    else
        isEncoded = EncodeFwavAudio(asset.get(), { ArchiveFormat::BFSAR, getNewFwavVersion(), mIsBigEndian ? sead::Endian::eBig : sead::Endian::eLittle }, info, encoding.fwavEncoding, outError);

    if (!isEncoded)
        return nullptr;

    if (mContainer == Container::Sarc)
    {
        if (hasNewAssetMeta())
            asset->setMeta(aal::MakeAmta1(std::string(), isMetaBigEndian));
    }
    else if (mVersion == aal::cBarsVersion11)
    {
        std::vector<u8> meta = aal::MakeAmta4(std::string(), kind == AudioAssetKind::Fwav ? aal::AmtaAudioKind::Bfwav : aal::AmtaAudioKind::Bwav, isMetaBigEndian);

        if (isStream)
            aal::WriteAmta4StreamLayout(&meta, asset->getInfo().channelCount);

        asset->setMeta(std::move(meta));
    }
    else
        asset->setMeta(aal::MakeAmta5(std::string(), asset->getInfo().channelCount, getNewMetaInfoMask(), isMetaBigEndian));

    WriteMetaFromInfo(asset.get(), this, true);

    return asset.release();
}

static bool IsEditableAudio(const AudioAsset& asset)
{
    const AudioAssetKind kind = asset.getInfo().kind;

    if (!asset.getFile())
        return asset.isStream() && (kind == AudioAssetKind::Stream || kind == AudioAssetKind::Bwav);

    return kind == AudioAssetKind::Bwav || kind == AudioAssetKind::Fwav || kind == AudioAssetKind::StreamPrefetch;
}

static bool CanEditAudio(const AudioAsset& asset, const char* unsupportedReason, const char** outReason)
{
    if (!IsEditableAudio(asset))
    {
        *outReason = unsupportedReason;
        return false;
    }

    if (asset.getInfo().channelCount == 0)
    {
        *outReason = messages::resource::cAudioDamaged;
        return false;
    }

    return true;
}

bool AudioResource::canReplaceAudio(const AudioAsset& asset, const char** outReason) const
{
    return canEditStreamAudio(asset, outReason) && CanEditAudio(asset, messages::resource::cReplaceUnsupported, outReason);
}

bool AudioResource::canSetLoop(const AudioAsset& asset, const char** outReason) const
{
    if (usesStreamFile(asset) && asset.getStreamFileState() != StreamFileState::Loaded)
    {
        *outReason = GetStreamFileStateError(asset.getStreamFileState());
        return false;
    }

    return CanEditAudio(asset, messages::resource::cLoopUnsupported, outReason);
}

static void WriteReplacedMeta(AudioAsset* asset, const aal::AmtaSourceAudio& source, bool keepsSourceRate, bool isMidSide, const OldAudioClear& cleared)
{
    std::vector<u8> meta = asset->getMeta();
    bool isChanged = keepsSourceRate && aal::WriteAmtaSourceRate(&meta, source);

    if (!isMidSide)
        isChanged |= aal::ClearAmtaMidSide(&meta);

    if (cleared.beatGrid)
        isChanged |= aal::ClearAmtaMusicInfo(&meta);

    const u32 markerLimit = aal::CalcAmtaMarkerLimit(keepsSourceRate, source, asset->getInfo().sampleRate, asset->getInfo().sampleCount);

    isChanged |= cleared.markers ? aal::ClearAmtaMarkers(&meta) : aal::ClampAmtaMarkers(&meta, markerLimit);

    if (isChanged)
        asset->setMeta(std::move(meta));
}

static void ClearMidSide(AudioAsset* asset)
{
    std::vector<u8> meta = asset->getMeta();

    if (aal::ClearAmtaMidSide(&meta))
        asset->setMeta(std::move(meta));
}

static void WriteChangedStreamLayout(AudioAsset* asset, u32 oldChannelCount)
{
    const u32 channelCount = asset->getInfo().channelCount;
    std::vector<u8> meta = asset->getMeta();

    if (!asset->isStream() || channelCount == oldChannelCount || !aal::WriteAmta4StreamLayout(&meta, channelCount))
        return;

    asset->setMeta(std::move(meta));
}

static bool EncodeReplacedAudio(AudioAsset* asset, const WaveFile::RiffWaveInfo& info, const AudioAssetEncoding& encoding, bool usesStreamFile, bool isMidSide, bool clearsRegions, const char** outError)
{
    const AudioAssetKind kind = asset->getInfo().kind;

    if (kind == AudioAssetKind::Fwav)
    {
        const WaveFile* current = asset->getPreviewWave();

        if (!current)
        {
            *outError = messages::resource::cAudioDamaged;
            return false;
        }

        return EncodeFwavAudio(asset, current->getTarget(), info, encoding.fwavEncoding, outError);
    }

    if (kind == AudioAssetKind::StreamPrefetch || kind == AudioAssetKind::Stream)
        return EncodeBfstmAudio(asset, info, clearsRegions, outError);

    aal::Bwav current;
    ReadAudioAssetBwav(asset->getCompleteFile(), &current);

    return EncodeBwavAudio(asset, info, encoding, current.isBigEndian, usesStreamFile, isMidSide, outError);
}

OldAudioData AudioResource::findOldAudioData(const AudioAsset& asset) const
{
    OldAudioData data;
    const std::vector<u8>& meta = asset.getMeta();
    aal::AmtaLabels labels;

    aal::ReadAmtaLabels(meta, &labels);

    for (const aal::AmtaMarker& marker : labels.markers)
        data.markerEnds.push_back(static_cast<u32>(std::min<u64>(static_cast<u64>(marker.startFrame) + marker.frameCount, UINT32_MAX)));

    data.hasBeatGrid = aal::HasAmtaMusicInfo(meta);
    data.tempo = aal::ReadAmtaTempo(meta);
    data.isMidSide = aal::IsAmtaMidSide(meta) && asset.getInfo().channelCount == 2;
    data.canRewriteMeta = aal::CanWriteAmtaLabels(meta);
    data.hasSourceRate = aal::HasAmtaSourceRate(meta, asset.getInfo().sampleRate);

    const AudioAssetKind kind = asset.getInfo().kind;
    LooseStreamFile stream;
    const char* error = nullptr;

    if ((kind != AudioAssetKind::StreamPrefetch && kind != AudioAssetKind::Stream) || !OpenStreamFile(asset, &stream, &error))
        return data;

    if (const StreamFileLayout* layout = stream.getStream().getFileLayout())
    {
        for (const StreamRegion& region : layout->regions)
            data.regionEnds.push_back(region.end);
    }

    return data;
}

void AudioResource::fillLoudnessChoices()
{
    getQuietTailLevel();
    dropsLastSample();
}

bool AudioResource::replaceAudio(AudioAsset* asset, const WaveFile::RiffWaveInfo& info, const aal::AmtaSourceAudio& source, const AudioAssetEncoding& encoding, const OldAudioClear& cleared, const char** outError)
{
    if (!canReplaceAudio(*asset, outError))
        return false;

    fillLoudnessChoices();

    const bool keepsSourceRate = aal::HasAmtaSourceRate(asset->getMeta(), asset->getInfo().sampleRate);
    const u32 oldChannelCount = asset->getInfo().channelCount;
    const bool isMidSide = !cleared.midSide && info.numChannels == 2 && aal::IsAmtaMidSide(asset->getMeta());

    if (!EncodeReplacedAudio(asset, info, encoding, usesStreamFile(*asset), isMidSide, cleared.regions, outError))
        return false;

    WriteReplacedMeta(asset, source, keepsSourceRate, isMidSide, cleared);
    WriteChangedStreamLayout(asset, oldChannelCount);

    return true;
}

bool AudioResource::replaceFile(AudioAsset* asset, std::vector<u8> file, const char** outError)
{
    const AudioAssetKind kind = DetectAudioAssetKind(file);

    if (!canReplaceAudio(*asset, outError))
        return false;

    if (kind != asset->getCompleteFileKind())
    {
        *outError = kind == AudioAssetKind::Bwav || kind == AudioAssetKind::Fwav || kind == AudioAssetKind::Stream ? messages::resource::cImportKindMismatch : messages::resource::cImportUnsupported;
        return false;
    }

    fillLoudnessChoices();

    const u32 oldChannelCount = asset->getInfo().channelCount;

    if (kind == AudioAssetKind::Stream)
    {
        bfstm::Bfstm stream;

        if (!bfstm::ReadBfstm(file.data(), file.size(), &stream, outError) || !SetBfstmStreamAudio(asset, std::move(file), outError))
            return false;

        UpdateMetaFromInfo(asset);
        WriteChangedStreamLayout(asset, oldChannelCount);

        return true;
    }

    if (kind == AudioAssetKind::Fwav)
    {
        if (!IsReadableFwav(file))
        {
            *outError = messages::resource::cAudioDamaged;
            return false;
        }

        asset->setFile(MakeAudioAssetFile(std::move(file)));
        UpdateMetaFromInfo(asset);

        return true;
    }

    aal::Bwav bwav;

    if (!aal::ReadBwav(file.data(), file.size(), &bwav, outError))
        return false;

    if (!aal::IsBwavComplete(bwav))
    {
        *outError = messages::resource::cImportNeedsCompleteBwav;
        return false;
    }

    if (usesStreamFile(*asset))
        SetStreamAudio(asset, bwav);
    else
        asset->setFile(MakeAudioAssetFile(std::move(file)));

    UpdateMetaFromInfo(asset);
    WriteChangedStreamLayout(asset, oldChannelCount);
    ClearMidSide(asset);

    return true;
}

bool AudioResource::setLoop(AudioAsset* asset, bool isLoop, u32 loopStartFrame, const char** outError)
{
    if (!canSetLoop(*asset, outError))
        return false;

    fillLoudnessChoices();

    if (asset->getInfo().kind == AudioAssetKind::Fwav)
        return SetWaveFileLoop(asset, isLoop, loopStartFrame, outError);

    if (asset->getInfo().kind == AudioAssetKind::StreamPrefetch || asset->getInfo().kind == AudioAssetKind::Stream)
    {
        if (!SetBfstmStreamLoop(asset, isLoop, loopStartFrame, outError))
            return false;

        UpdateMetaFromInfo(asset);
        return true;
    }

    aal::Bwav bwav;

    if (!ReadAudioAssetBwav(asset->getCompleteFile(), &bwav))
    {
        *outError = messages::resource::cAudioDamaged;
        return false;
    }

    if (!aal::CanSetBwavLoop(bwav, isLoop, loopStartFrame))
    {
        *outError = messages::wavefile::cInvalidLoopRange;
        return false;
    }

    aal::SetBwavLoop(&bwav, isLoop, loopStartFrame);

    if (usesStreamFile(*asset))
        SetStreamLoop(asset, bwav);
    else
        asset->setFile(MakeAudioAssetFile(aal::WriteBwav(bwav)));

    UpdateMetaFromInfo(asset);

    return true;
}

bool AudioResource::canEditLabels(const AudioAsset& asset, const char** outReason) const
{
    if (!aal::CanWriteAmtaLabels(asset.getMeta()))
    {
        *outReason = messages::resource::cMarkersLayoutUnknown;
        return false;
    }

    return true;
}

bool AudioResource::dropsLastSample()
{
    if (mLastSampleDrop >= 0)
        return mLastSampleDrop == 1;

    const f32 quietTailLevel = getQuietTailLevel();
    u32 testNum = 0;
    s64 total = 0;

    for (Item* item : mAssets)
    {
        s64 score = 0;

        if (testNum >= cLastSampleDropTests || total >= cLastSampleDropExact || total <= -cLastSampleDropExact)
            break;

        if (!ScoreLastSampleDrop(static_cast<AudioAsset*>(item), quietTailLevel, &score))
            continue;

        total += score;
        testNum += score != 0;
    }

    mLastSampleDrop = total > 0 ? 1 : 0;

    return mLastSampleDrop == 1;
}

f32 AudioResource::getQuietTailLevel()
{
    if (mQuietTailLevel < 0)
        mQuietTailLevel = static_cast<s8>(FindQuietTailLevel(mAssets));

    return aal::cAmtaQuietTailLevels[mQuietTailLevel];
}

bool AudioResource::canEditMarkers(const AudioAsset& asset, const char** outReason) const
{
    if (asset.getInfo().sampleCount == 0)
    {
        *outReason = asset.getNoAudioReason();
        return false;
    }

    return canEditLabels(asset, outReason);
}

bool AudioResource::setLabels(AudioAsset* asset, const aal::AmtaLabels& labels, const char** outError)
{
    aal::AmtaLabels current;
    const bool movesMarkers = !aal::ReadAmtaLabels(asset->getMeta(), &current) || current.markers != labels.markers;

    if (movesMarkers ? !canEditMarkers(*asset, outError) : !canEditLabels(*asset, outError))
        return false;

    std::vector<u8> meta = asset->getMeta();

    if (!aal::WriteAmtaLabels(&meta, labels))
    {
        *outError = messages::resource::cMarkersLayoutUnknown;
        return false;
    }

    asset->setMeta(std::move(meta));
    return true;
}
