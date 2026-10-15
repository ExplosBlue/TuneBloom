#include <bfsar/files/WaveFile.h>

#include <core/Debug.h>

#include <bfsar/audio/AudioProcessing.h>
#include <bfsar/audio/DecodedPcm.h>
#include <bfsar/io/FileIo.h>
#include <bfsar/io/FileWriter.h>

#include <snd/DisposeCallbackMgr.h>
#include <snd/SoundThread.h>

#include <snd/snd_WaveFileReader.h>

#include <ui/Messages.h>
#include <ui/app/PopupMgr.h>
#include <ui/app/UI.h>

#include <filedevice/seadFileDeviceMgr.h>
#include <stream/seadFileDeviceStream.h>

#include <libs/md5.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

static constexpr u32 cBlockAlignment      = 0x20;
static constexpr u32 cDataBlockHeaderSize = 0x8;
static constexpr u32 cAdpcmFrameBytes     = 8;

const char* WaveFile::sEncodingTypes[3] = {
    messages::wavefile::cEncodingPcm8,
    messages::wavefile::cEncodingPcm16,
    messages::wavefile::cEncodingDspAdpcm,
    //"ImaAdpcm"
};

WaveFile::Channel::~Channel()
{
    dispose_();

    if (mOwnsData && mData)
    {
        delete[] static_cast<const u8*>(mData);
        mData = nullptr;
    }

    if (mFullData)
    {
        delete[] static_cast<const u8*>(mFullData);
        mFullData = nullptr;
    }
}

void WaveFile::Channel::dispose_()
{
    snd::internal::driver::SoundThreadLock lock;

    snd::internal::driver::DisposeCallbackMgr::instance()->dispose(mData, mDataSize);
}

WaveFile::~WaveFile()
{
    {
        snd::internal::driver::SoundThreadLock lock;

        mChannels.freeBuffer();
    }

    invalidateOriginalData_();
    ForgetWaveFileView(this);

    if (this == sSoundPlayer.getPlayingWaveFile())
    {
        sSoundPlayer.resetPlayingWaveFile();
    }
}

const Item* WaveFile::validate(sead::BufferedSafeString& error) const
{
    switch (getEncoding())
    {
        case Encoding::Pcm8:
        case Encoding::Pcm16:
        case Encoding::DspAdpcm:
            break;

        default:
            error = messages::wavefile::cInvalidEncoding;
            return this;
    }

    if (!mReferences.isEmpty())
    {
        const_cast<WaveFile*>(this)->updateLoop(); // dude this is like, so awesome
    }

    if (getLoopEndFrame(false) <= getLoopStartFrame(false))
    {
        error = messages::wavefile::cInvalidLoopRange;
        return this;
    }

    if (getLoopEndFrame(false) < 1)
    {
        error = messages::wavefile::cNoDataToWrite;
        return this;
    }

    if (getChannels().isEmpty())
    {
        error = messages::wavefile::cNeedsChannel;
        return this;
    }

    if (getChannels().size() > 2)
    {
        error = messages::wavefile::cChannelLimit;
        return this;
    }

    return nullptr;
}

void FillAdpcmInfo(ADPCMINFO* adpcmInfo, const snd::DspAdpcmParam& param, const snd::internal::DspAdpcmLoopParam& loopParam)
{
    LOG_FUNC();
    SEAD_ASSERT(adpcmInfo);

    adpcmInfo->coef[0]  = param.coef[0][0];
    adpcmInfo->coef[1]  = param.coef[0][1];
    adpcmInfo->coef[2]  = param.coef[1][0];
    adpcmInfo->coef[3]  = param.coef[1][1];
    adpcmInfo->coef[4]  = param.coef[2][0];
    adpcmInfo->coef[5]  = param.coef[2][1];
    adpcmInfo->coef[6]  = param.coef[3][0];
    adpcmInfo->coef[7]  = param.coef[3][1];
    adpcmInfo->coef[8]  = param.coef[4][0];
    adpcmInfo->coef[9]  = param.coef[4][1];
    adpcmInfo->coef[10] = param.coef[5][0];
    adpcmInfo->coef[11] = param.coef[5][1];
    adpcmInfo->coef[12] = param.coef[6][0];
    adpcmInfo->coef[13] = param.coef[6][1];
    adpcmInfo->coef[14] = param.coef[7][0];
    adpcmInfo->coef[15] = param.coef[7][1];

    adpcmInfo->pred_scale = param.predScale;
    adpcmInfo->yn1 = param.yn1;
    adpcmInfo->yn2 = param.yn2;

    adpcmInfo->loop_pred_scale = loopParam.loopPredScale;
    adpcmInfo->loop_yn1 = loopParam.loopYn1;
    adpcmInfo->loop_yn2 = loopParam.loopYn2;
}

void FillAdpcmParam(snd::DspAdpcmParam* param, snd::internal::DspAdpcmLoopParam* loopParam, const ADPCMINFO& adpcmInfo)
{
    LOG_FUNC();
    SEAD_ASSERT(param);
    SEAD_ASSERT(loopParam);

    param->coef[0][0] = adpcmInfo.coef[0];
    param->coef[0][1] = adpcmInfo.coef[1];
    param->coef[1][0] = adpcmInfo.coef[2];
    param->coef[1][1] = adpcmInfo.coef[3];
    param->coef[2][0] = adpcmInfo.coef[4];
    param->coef[2][1] = adpcmInfo.coef[5];
    param->coef[3][0] = adpcmInfo.coef[6];
    param->coef[3][1] = adpcmInfo.coef[7];
    param->coef[4][0] = adpcmInfo.coef[8];
    param->coef[4][1] = adpcmInfo.coef[9];
    param->coef[5][0] = adpcmInfo.coef[10];
    param->coef[5][1] = adpcmInfo.coef[11];
    param->coef[6][0] = adpcmInfo.coef[12];
    param->coef[6][1] = adpcmInfo.coef[13];
    param->coef[7][0] = adpcmInfo.coef[14];
    param->coef[7][1] = adpcmInfo.coef[15];

    param->predScale = adpcmInfo.pred_scale;
    param->yn1 = adpcmInfo.yn1;
    param->yn2 = adpcmInfo.yn2;

    loopParam->loopPredScale = adpcmInfo.loop_pred_scale;
    loopParam->loopYn1 = adpcmInfo.loop_yn1;
    loopParam->loopYn2 = adpcmInfo.loop_yn2;
}

void WriteDspAdpcmInfo(sead::WriteStream* stream, const snd::DspAdpcmParam& adpcmParam, const snd::internal::DspAdpcmLoopParam& adpcmLoopParam)
{
    for (u32 i = 0; i < 8; i++)
    {
        stream->writeU16(adpcmParam.coef[i][0]);
        stream->writeU16(adpcmParam.coef[i][1]);
    }

    stream->writeU16(adpcmParam.predScale);
    stream->writeU16(adpcmParam.yn1);
    stream->writeU16(adpcmParam.yn2);

    stream->writeU16(adpcmLoopParam.loopPredScale);
    stream->writeU16(adpcmLoopParam.loopYn1);
    stream->writeU16(adpcmLoopParam.loopYn2);
    stream->writeU16(0); // Padding
}

bool WaveFile::readChannel_(const nw::snd::internal::WaveFileReader& reader, const nw::snd::internal::WaveInfo& waveInfo, s32 channelIndex)
{
    const nw::snd::internal::WaveInfo::ChannelParam& channelParam = waveInfo.channelParam[channelIndex];

    const s32 samplesOffset = reader.mInfoBlockBody->GetChannelInfo(channelIndex).referToSamples.offset;
    if (samplesOffset < 0 || static_cast<u32>(samplesOffset) >= mOriginalDataSize)
    {
        sead::FormatFixedSafeString<256> msg(messages::wavefile::cSampleOffsetInvalidFormat, channelIndex);
        PopupMgr::instance()->pushCurrentItemError(msg);
        return false;
    }

    Channel* channel = mChannels.birthBack();
    SEAD_ASSERT(channel);

    channel->mOwnsData = false;
    channel->mData = reader.mInfoBlockBody->GetChannelInfo(channelIndex).GetSamplesAddress(mOriginalData);

    if (mEncoding == Encoding::DspAdpcm)
    {
        channel->mDataSize = getBytesForAdpcmBuffer(getLoopEndFrame(false));
    }
    else
    {
        // TODO: Different between versions ??? :(
        channel->mDataSize = nw::snd::internal::Util::GetByteBySample(getLoopEndFrame(false), nw::snd::internal::WaveFileReader::GetSampleFormat(static_cast<u8>(mEncoding)));
    }

    channel->mDataSizeMin = channel->mDataSize;
    channel->mOriginalDataOffset = samplesOffset;

    if (mEncoding == Encoding::DspAdpcm)
        channel->setAdpcmParams_(channelParam.adpcmParam, channelParam.adpcmLoopParam);

    return true;
}

bool WaveFile::doRead(const void* fileAddr)
{
    LOG_FUNC();

    nw::snd::internal::WaveFileReader reader(fileAddr);
    if (!reader.IsAvailable())
    {
        LOG_FMT("WaveFileReader not available");
        return false;
    }

    if (reader.mInfoBlockBody->encoding > static_cast<u8>(Encoding::DspAdpcm))
    {
        PopupMgr::instance()->pushCurrentItemError(messages::wavefile::cInvalidEncoding);
        return false;
    }

    nw::snd::internal::WaveInfo waveInfo;
    reader.ReadWaveInfo(&waveInfo);

    LOG_U32("endian", waveInfo.endian);
    LOG_U32("encoding", reader.mInfoBlockBody->encoding);
    LOG_U32("sampleRate", waveInfo.sampleRate);
    LOG_U32("channelCount", waveInfo.channelCount);
    LOG_BOOL("loopFlag", waveInfo.loopFlag);
    LOG_U32("loopStartFrame", waveInfo.loopStartFrame);
    LOG_U32("loopEndFrame", waveInfo.loopEndFrame);
    LOG_U32("originalLoopStartFrame", waveInfo.originalLoopStartFrame);
    LOG_U32("dataBlockSize", reader.mHeader->GetDataBlock()->header.size);

    if (waveInfo.channelCount < 1)
    {
        PopupMgr::instance()->pushCurrentItemError(messages::wavefile::cNeedsChannel);
        return false;
    }

    const u32 dataBlockOffset = static_cast<u32>(static_cast<const u8*>(static_cast<const void*>(reader.mHeader->GetDataBlock())) - static_cast<const u8*>(static_cast<const void*>(reader.mHeader)));
    const u32 dataBlockSize = reader.mHeader->GetDataBlock()->header.size;
    if (dataBlockSize < cDataBlockHeaderSize || static_cast<u64>(dataBlockOffset) + dataBlockSize > reader.mHeader->header.fileSize)
    {
        PopupMgr::instance()->pushCurrentItemError(messages::wavefile::cDataBlockDamaged);
        return false;
    }

    mDataEndian = waveInfo.endian;
    mEncoding = static_cast<Encoding>(reader.mInfoBlockBody->encoding);
    mIsLoop = waveInfo.loopFlag;
    mSampleRate = waveInfo.sampleRate;

    mOriginalFileLoopStartFrame = waveInfo.originalLoopStartFrame;
    if (waveInfo.originalLoopStartFrame == 0 && waveInfo.loopStartFrame != 0)
        waveInfo.originalLoopStartFrame = waveInfo.loopStartFrame;

    const u32 fileSpoolFrames = waveInfo.loopStartFrame - waveInfo.originalLoopStartFrame;

    mLoopStartFrame = waveInfo.originalLoopStartFrame;
    mLoopEndFrame = waveInfo.loopEndFrame - fileSpoolFrames;
    mDataSpoolFrames = mIsLoop ? fileSpoolFrames : 0;

    if (mLoopStartFrame >= mLoopEndFrame)
    {
        sead::FormatFixedSafeString<1024> msg(messages::wavefile::cLoopInvalidFormat);
        PopupMgr::instance()->pushCurrentItemError(msg);
    }

    mSampleCount = mLoopEndFrame;

    const s32 ch0DataOffset = reader.mInfoBlockBody->GetChannelInfo(0).referToSamples.offset;
    const void* dataBlockBodySrc = sead::PtrUtil::addOffset(waveInfo.channelParam[0].dataAddress, -ch0DataOffset);

    mUseOriginalData = true;
    mOriginalDataSize = dataBlockSize - cDataBlockHeaderSize;

    if (mEncoding == Encoding::DspAdpcm)
        mOriginalDmaAlignOffset = dataBlockOffset;
    else
        mOriginalDmaAlignOffset = static_cast<u32>(static_cast<const u8*>(waveInfo.channelParam[0].dataAddress) - static_cast<const u8*>(static_cast<const void*>(reader.mHeader)));

    mOriginalData = new u8[mOriginalDataSize];
    sead::MemUtil::copy(mOriginalData, dataBlockBodySrc, mOriginalDataSize);

    for (s32 i = 0; i < waveInfo.channelCount && i < snd::cWaveChannelMax; i++)
    {
        if (!readChannel_(reader, waveInfo, i))
            return false;
    }

    mIsLoopDirty = false;
    mIsStreamExtended = false;

    return true;
}

void WaveFile::writeInfoBlock_(FileWriter& writer, sead::WriteStream* stream) const
{
    writer.openBlock(nw::snd::internal::ElementType_WaveFile_InfoBlock, "INFO");

    stream->writeU8(static_cast<u8>(mEncoding));
    stream->writeU8(mIsLoop);
    stream->writeU16(0); // Padding
    stream->writeU32(mSampleRate);
    stream->writeU32(getLoopStartFrame(false));
    stream->writeU32(getLoopEndFrame(false));
    stream->writeU32(isOriginalLoopAvailable(mFormat, mVersion) ? (mUseOriginalData ? mOriginalFileLoopStartFrame : getOriginalLoopStartFrame()) : 0);

    writer.pushOffsetBase();
    {
        writer.openReferenceTable("ChannelInfoTable", mChannels.size());

        for (u32 i = 0; i < mChannels.size(); i++)
        {
            writer.addReferenceTableReference("ChannelInfoTable", nw::snd::internal::ElementType_WaveFile_ChannelInfo);

            writer.pushOffsetBase();
            {
                writer.openReference(sead::FormatFixedSafeString<16>("Samples%u", i));
                writer.openReference(sead::FormatFixedSafeString<16>("AdpcmInfo%u", i));

                stream->writeU32(0); // Reserved
            }
            writer.popOffsetBase();
        }

        for (u32 i = 0; i < mChannels.size(); i++)
        {
            const Channel* channel = mChannels.nth(i);

            if (mEncoding != Encoding::DspAdpcm)
            {
                writer.closeNullReference(sead::FormatFixedSafeString<16>("AdpcmInfo%u", i));
                continue;
            }

            writer.closeReference(sead::FormatFixedSafeString<16>("AdpcmInfo%u", i), nw::snd::internal::ElementType_Codec_DspAdpcmInfo);

            WriteDspAdpcmInfo(stream, channel->getAdpcmParam(false), channel->getAdpcmLoopParam(false));
        }

        writer.closeReferenceTable("ChannelInfoTable");
    }
    writer.popOffsetBase();

    writer.align(cBlockAlignment);
    writer.closeBlock();
}

void WaveFile::writeOriginalData_(FileWriter& writer, sead::WriteStream* stream) const
{
    LOG_U32("originalDataSize", mOriginalDataSize);

    for (u32 i = 0; i < mChannels.size(); i++)
    {
        const Channel* channel = mChannels.nth(i);

        writer.closeReference(
            sead::FormatFixedSafeString<16>("Samples%u", i),
            nw::snd::internal::ElementType_General_ByteStream,
            channel->mOriginalDataOffset
        );
    }

    stream->writeMemBlock(mOriginalData, mOriginalDataSize);
}

void WaveFile::writeChannelSamples_(sead::WriteStream* stream, const Channel& channel, u32 sampleBytes) const
{
    switch (mEncoding)
    {
        case Encoding::Pcm8:
        case Encoding::DspAdpcm:
            stream->writeMemBlock(channel.getData(), sampleBytes);
            break;

        case Encoding::Pcm16:
            if (mDataEndian == mEndian)
            {
                stream->writeMemBlock(channel.getData(), sampleBytes);
            }
            else
            {
                u32 sampleCount = getLoopEndFrame(false);

                const s16* data = static_cast<const s16*>(channel.getData());
                s16* samples = new s16[sampleCount];

                for (u32 j = 0; j < sampleCount; j++)
                {
                    samples[j] = sead::Endian::convertS16(mDataEndian, mEndian, data[j]);
                }

                stream->writeMemBlock(samples, sampleBytes);

                delete[] samples;
            }

            break;

        default:
            SEAD_ASSERT_MSG(false, "Invalid Encoding");
            break;
    }
}

void WaveFile::writeDataBlock_(FileWriter& writer, sead::WriteStream* stream) const
{
    writer.openBlock(nw::snd::internal::ElementType_WaveFile_DataBlock, "DATA");

    u32 dataBlockBodyPos = writer.getPosition();

    if (mUseOriginalData)
    {
        writeOriginalData_(writer, stream);
    }
    else
    {
        writer.align(cBlockAlignment);

        u32 sampleBytes = mChannels.front()->mDataSizeMin;
        LOG_U32("sampleBytes", sampleBytes);

        for (u32 i = 0; i < mChannels.size(); i++)
        {
            writer.align(cBlockAlignment);

            writer.closeReference(
                sead::FormatFixedSafeString<16>("Samples%u", i),
                nw::snd::internal::ElementType_General_ByteStream,
                writer.getPosition() - dataBlockBodyPos
            );

            writeChannelSamples_(stream, *mChannels.nth(i), sampleBytes);
        }
    }

    writer.closeBlock();
}

u32 WaveFile::doWrite(sead::FileHandle* handle, sead::WriteStream* stream, bool isLast) const
{
    LOG_FUNC();
    LOG_U32("encoding", static_cast<u32>(mEncoding));
    LOG_U32("sampleRate", mSampleRate);
    LOG_U32("sampleCount", mSampleCount);
    LOG_U32("channels", mChannels.size());
    LOG_BOOL("isLoop", mIsLoop);
    LOG_BOOL("useOriginalData", mUseOriginalData);

    mFormat = sBfsar.getFormat();
    mVersion = sBfsar.getVersionForBfwav();
    mEndian = sBfsar.getEndian();

    FileWriter writer(handle, stream);
    writer.openFile(GetInnerFileMagic(mFormat, InnerFileKind::Wave), 2, mVersion);

    writeInfoBlock_(writer, stream);
    writeDataBlock_(writer, stream);

    u32 fileSize = writer.getPosition();

    writer.closeFile();

    return fileSize;
}

const std::string& WaveFile::getMd5Hash() const
{
    const bool isStale = mMd5Dirty || mIsLoopDirty || mMd5Format != sBfsar.getFormat() ||
                         mMd5Version != sBfsar.getVersionForBfwav() || mMd5Endian != sBfsar.getEndian();

    if (isStale)
        const_cast<WaveFile*>(this)->refreshSerializedHash();

    return mMd5Hash;
}

void WaveFile::setSerializedHash(const std::string& hash)
{
    mMd5Hash = hash;
    mMd5Dirty = false;
    mMd5Format = sBfsar.getFormat();
    mMd5Version = sBfsar.getVersionForBfwav();
    mMd5Endian = sBfsar.getEndian();
}

void WaveFile::refreshSerializedHash()
{
    updateLoop();

    sead::FileDevice *device = sead::FileDeviceMgr::instance()->findDevice("native");

    if (!device)
        return;

    char tempPath[700];
    snprintf(tempPath, sizeof(tempPath), "%s/tunebloom_wavehash_%p.bin", fileio::GetTempFolder(), static_cast<const void *>(this));

    sead::FileHandle handle;
    device->tryOpen(&handle, tempPath, sead::FileDevice::FileOpenFlag::eWriteOnly, 0);

    if (!handle.getDevice())
    {
        device->tryOpen(&handle, tempPath, sead::FileDevice::FileOpenFlag::eCreate, 0);
        if (!handle.getDevice())
            return;
    }

    {
        sead::FileDeviceWriteStream stream(&handle, sead::Stream::Modes::eBinary);
        setup(sBfsar.getEndian());
        setFormat(sBfsar.getFormat());

        write(&handle, &stream, sBfsar.getEndian(), true);
    }

    handle.close();

    sead::FileDevice::LoadArg loadArg;
    loadArg.path = tempPath;
    u8 *buf = device->tryLoad(loadArg);

    if (buf)
    {
        mMd5Hash = md5(buf, loadArg.read_size);
        device->unload(buf);

        mMd5Dirty = false;
        mMd5Format = sBfsar.getFormat();
        mMd5Version = sBfsar.getVersionForBfwav();
        mMd5Endian = sBfsar.getEndian();
    }

    ::remove(tempPath);
}

void WaveFile::buildSeekTable_(const void* samples, u32 sampleCount, snd::SampleFormat sampleFormat, Channel::SeekData* outSeekData, const Channel::SeekData* srcSeekData, u32 preserveSamples)
{
    LOG_FUNC();
    const u32 cSamplesPerBlock = 0x3800;

    SEAD_ASSERT(outSeekData);

    bool freeSeek = false;
    if (outSeekData == srcSeekData)
    {
        Channel::SeekData* seekData = new Channel::SeekData();
        seekData->copy(*srcSeekData);

        srcSeekData = seekData;
        freeSeek = true;
    }

    outSeekData->free();

    u32 blockNum = (u32)ceil(sampleCount / (f32)cSamplesPerBlock);
    outSeekData->mSeekInfo.allocBuffer(blockNum);
    outSeekData->mOwner = true;

    u32 blocksToPreserve = preserveSamples / cSamplesPerBlock;

    for (u32 blockNo = 0; blockNo < blockNum; blockNo++)
    {
        Channel::SeekInfo& seekInfo = outSeekData->mSeekInfo[blockNo];

        //? Copy seek entry directly from source if it falls within the preserved region
        if (srcSeekData != nullptr && blockNo < blocksToPreserve)
        {
            seekInfo = srcSeekData->getSeekInfo(blockNo);
            continue;
        }

        if (blockNo == 0) //? First block is always 0
        {
            seekInfo.yn1 = 0;
            seekInfo.yn2 = 0;
            continue;
        }

        if (sampleFormat == snd::SampleFormat::PcmS8)
        {
            const s8* samples8 = (const s8*)samples;

            seekInfo.yn1 = s16(samples8[blockNo * cSamplesPerBlock - 1] << 8);
            seekInfo.yn2 = s16(samples8[blockNo * cSamplesPerBlock - 2] << 8);
        }
        else if (sampleFormat == snd::SampleFormat::PcmS16)
        {
            const s16* samples16 = (const s16*)samples;

            seekInfo.yn1 = samples16[blockNo * cSamplesPerBlock - 1];
            seekInfo.yn2 = samples16[blockNo * cSamplesPerBlock - 2];
        }
    }

    if (freeSeek)
    {
        delete srcSeekData;
    }
}

void WaveFile::disposeChannels_()
{
    snd::internal::driver::SoundThreadLock lock;

    for (u32 i = 0; i < mChannels.size(); i++)
    {
        Channel* channel = mChannels.nth(i);
        channel->dispose_();
    }
}

u32 WaveFile::getLoopStartAlignmentFrames(bool forStream) const
{
    const bool isCtr = sBfsar.getFormat() == ArchiveFormat::BCSAR;

    switch (mEncoding)
    {
        case Encoding::DspAdpcm:
            if (forStream)
                return cStreamLoopStartAlignmentFramesAdpcm;

            return isCtr ? cCtrLoopStartAlignmentBytes / cAdpcmFrameBytes * cSamplesPerFrame : cSamplesPerFrame;

        case Encoding::Pcm16:
            if (forStream)
                return 8192 / sizeof(s16);

            return isCtr ? cCtrLoopStartAlignmentBytes / sizeof(s16) : 0;

        case Encoding::Pcm8:
            if (forStream)
                return 8192 / sizeof(u8);

            return isCtr ? cCtrLoopStartAlignmentBytes / sizeof(u8) : 0;
    }

    return 0;
}

void WaveFile::updateLoopInfo_(bool update, bool updateStream)
{
    if (mEncoding != Encoding::DspAdpcm)
    {
        return;
    }

    for (u32 i = 0; i < mChannels.size(); i++)
    {
        Channel* channel = mChannels.nth(i);

        ADPCMINFO adpcmInfo;
        sead::MemUtil::fillZero(&adpcmInfo, sizeof(adpcmInfo));

        if (update)
        {
            FillAdpcmInfo(&adpcmInfo, channel->getAdpcmParam(false), channel->getAdpcmLoopParam(false));
            getLoopContext(static_cast<u8*>(const_cast<void*>(channel->getData())), &adpcmInfo, getLoopStartFrame(false));

            channel->mAdpcmLoopParam.loopPredScale = adpcmInfo.loop_pred_scale;
            channel->mAdpcmLoopParam.loopYn1 = adpcmInfo.loop_yn1;
            channel->mAdpcmLoopParam.loopYn2 = adpcmInfo.loop_yn2;
        }

        if (updateStream)
        {
            FillAdpcmInfo(&adpcmInfo, channel->getAdpcmParam(true), channel->getAdpcmLoopParam(true));
            getLoopContext(static_cast<u8*>(const_cast<void*>(channel->getData())), &adpcmInfo, getLoopStartFrame(true));

            channel->mAdpcmLoopParamStream.loopPredScale = adpcmInfo.loop_pred_scale;
            channel->mAdpcmLoopParamStream.loopYn1 = adpcmInfo.loop_yn1;
            channel->mAdpcmLoopParamStream.loopYn2 = adpcmInfo.loop_yn2;
        }
    }
}

void WaveFile::updateLoop()
{
    if (mIsLoopDirty)
    {
        rebuildSpooledData_();
        invalidateOriginalData_();

        mIsLoopDirty = false;
    }
}

void WaveFile::rebuildSpooledData_()
{
    disposeChannels_();
    reencodeChannels_(mEncoding);
    mMd5Dirty = true;
}

void WaveFile::reencodeChannels_(Encoding fromEncoding)
{
    mDataSpoolFrames.reset();

    for (u32 i = 0; i < mChannels.size(); i++)
    {
        Channel* channel = mChannels.nth(i);

        u32 dataSize = 0;
        u32 dataSizeMin = 0;
        void* newData = convertOwnChannel_(*channel, channel->getData(), mDataEndian, fromEncoding, &dataSize, &dataSizeMin);

        if (channel->mOwnsData && channel->mData && channel->mData != newData)
        {
            delete[] static_cast<const u8*>(channel->mData);
        }

        channel->mData = newData;
        channel->mDataSize = dataSize;
        channel->mDataSizeMin = dataSizeMin;
        channel->mOwnsData = true;
    }

    mDataEndian = sead::Endian::getHostEndian();
    mIsLoopDirty = false;
    mIsStreamExtended = true;
}

void* WaveFile::convertOwnChannel_(Channel& channel, const void* data, sead::Endian::Types dataEndian, Encoding from, u32* outSize, u32* outSizeMin) const
{
    return convertChannel_(
        channel, data, dataEndian,
        from, mEncoding, outSize, outSizeMin, true, getIsLoop(),
        getSampleCount(), getSampleCount(),
        getOriginalLoopStartFrame(), getOriginalLoopEndFrame(),
        getLoopStartFrame(false), getLoopEndFrame(false),
        getLoopStartFrame(true), getLoopEndFrame(true),
        &channel.mAdpcmParam, &channel.mAdpcmLoopParam,
        &channel.mAdpcmParamStream, &channel.mAdpcmLoopParamStream,
        &channel.mSeekData
    );
}

namespace
{

struct ChannelConvertContext
{
    WaveFile::Channel& channel;
    bool updateChannel;
    bool isLoop;
    u32 baseSamples;
    u32 targetSampleCount;
    u32 originalLoopStartFrame;
    u32 originalLoopEndFrame;
    u32 loopStartFrame;
    u32 loopStartFrameStream;
    u32 loopEndFrameStream;
    u32 totalSamples;
    u32 totalSamplesMin;
    const u8* srcAdpcmData;

    snd::DspAdpcmParam* outAdpcmParam;
    snd::internal::DspAdpcmLoopParam* outAdpcmLoopParam;
    snd::DspAdpcmParam* outAdpcmParamStream;
    snd::internal::DspAdpcmLoopParam* outAdpcmLoopParamStream;
    WaveFile::Channel::SeekData* outSeekData;
};

}

static s16* DecodeBasePcm(ChannelConvertContext& context, const void* data, WaveFile::Encoding from, sead::Endian::Types dataEndian)
{
    const u32 baseSamples = context.baseSamples;
    s16* basePcm = new s16[sead::Mathu::max(baseSamples, context.targetSampleCount)];

    if (from == WaveFile::Encoding::Pcm16)
    {
        const s16* src = static_cast<const s16*>(data);

        if (dataEndian == sead::Endian::getHostEndian())
        {
            sead::MemUtil::copy(basePcm, src, baseSamples * sizeof(s16));
        }
        else
        {
            for (u32 i = 0; i < baseSamples; i++)
            {
                basePcm[i] = sead::Endian::toHostS16(dataEndian, src[i]);
            }
        }
    }
    else if (from == WaveFile::Encoding::Pcm8)
    {
        const s8* src = static_cast<const s8*>(data);

        for (u32 i = 0; i < baseSamples; i++)
        {
            basePcm[i] = s16(src[i] << 8);
        }
    }
    else if (from == WaveFile::Encoding::DspAdpcm)
    {
        context.srcAdpcmData = static_cast<const u8*>(data);

        ADPCMINFO adpcmInfo;
        sead::MemUtil::fillZero(&adpcmInfo, sizeof(adpcmInfo));
        FillAdpcmInfo(&adpcmInfo, context.channel.getAdpcmParam(false), context.channel.getAdpcmLoopParam(false));

        decode(const_cast<u8*>(context.srcAdpcmData), basePcm, &adpcmInfo, baseSamples);
    }

    if (baseSamples < context.targetSampleCount)
        sead::MemUtil::fillZero(basePcm + baseSamples, (context.targetSampleCount - baseSamples) * sizeof(s16));

    return basePcm;
}

static void PreserveFullData(WaveFile::Channel& channel, const void* data, WaveFile::Encoding encoding, sead::Endian::Types dataEndian)
{
    if (!channel.getFullData_())
    {
        // TODO: If converting to DspAdpcm and full data is not already DspAdpcm, resave full data as DspAdpcm

        u32 dataSize = channel.getDataSize();
        void* fullData = new u8[dataSize];
        sead::MemUtil::copy(fullData, data, dataSize);

        channel.setFullData_(fullData, encoding, dataEndian);
    }

    if (!channel.getFullSeekData_())
        channel.setFullSeekData_(channel.getSeekData());
}

static s16* SpoolPcm(const ChannelConvertContext& context, const s16* basePcm)
{
    s16* spooledPcm = new s16[context.totalSamples];

    u32 copyCount = sead::Mathu::min(context.targetSampleCount, context.originalLoopEndFrame);
    sead::MemUtil::copy(spooledPcm, basePcm, copyCount * sizeof(s16));

    if (context.isLoop)
    {
        u32 loopStart = context.originalLoopStartFrame;
        u32 loopEnd = context.originalLoopEndFrame;
        u32 loopFrames = loopEnd - loopStart;

        if (loopFrames > 0)
        {
            for (u32 currentPos = loopEnd; currentPos < context.totalSamples; currentPos++)
            {
                spooledPcm[currentPos] = basePcm[loopStart + ((currentPos - loopEnd) % loopFrames)];
            }
        }
    }
    else if (copyCount < context.loopEndFrameStream)
    {
        sead::MemUtil::fillZero(spooledPcm + copyCount, (context.loopEndFrameStream - copyCount) * sizeof(s16));
    }

    return spooledPcm;
}

static u32 EncodeAdpcmAfterSource(const ChannelConvertContext& context, const s16* spooledPcm, u8* dst)
{
    u32 copyCount = sead::Mathu::min(context.baseSamples, context.originalLoopEndFrame);
    u32 copiedFrames = copyCount / WaveFile::cSamplesPerFrame;
    u32 copiedBytes = copiedFrames * cAdpcmFrameBytes;
    u32 copiedSamples = copiedFrames * WaveFile::cSamplesPerFrame;

    sead::MemUtil::copy(dst, context.srcAdpcmData, copiedBytes);

    s16 pcmFrame[16] = { 0 };
    if (copiedSamples >= 2)
    {
        pcmFrame[0] = spooledPcm[copiedSamples - 2];
        pcmFrame[1] = spooledPcm[copiedSamples - 1];
    }
    else if (copiedSamples == 1)
    {
        pcmFrame[1] = spooledPcm[0];
    }

    u32 remainingSamples = context.totalSamples - copiedSamples;
    u32 remainingFrames = (remainingSamples + WaveFile::cSamplesPerFrame - 1) / WaveFile::cSamplesPerFrame;

    const s16* pcm = spooledPcm + copiedSamples;
    u8* adpcm = dst + copiedBytes;

    const snd::DspAdpcmParam& param = context.channel.getAdpcmParam(false);

    for (u32 i = 0; i < remainingFrames; i++, pcm += WaveFile::cSamplesPerFrame, adpcm += cAdpcmFrameBytes)
    {
        u32 frameSamples = remainingSamples - i * WaveFile::cSamplesPerFrame;
        if (frameSamples > WaveFile::cSamplesPerFrame)
            frameSamples = WaveFile::cSamplesPerFrame;

        sead::MemUtil::fillZero(pcmFrame + 2, WaveFile::cSamplesPerFrame * sizeof(s16));
        sead::MemUtil::copy(pcmFrame + 2, pcm, frameSamples * sizeof(s16));

        encodeFrame(pcmFrame, adpcm, (s16*)param.coef, 0);

        pcmFrame[0] = pcmFrame[14];
        pcmFrame[1] = pcmFrame[15];
    }

    return copiedSamples;
}

static void StoreAdpcmParams(const ChannelConvertContext& context, u8* dst, ADPCMINFO& adpcmInfo)
{
    getLoopContext(dst, &adpcmInfo, context.loopStartFrame);

    ADPCMINFO adpcmInfoStream;
    sead::MemUtil::copy(&adpcmInfoStream, &adpcmInfo, sizeof(ADPCMINFO));
    getLoopContext(dst, &adpcmInfoStream, context.loopStartFrameStream);

    if (context.outAdpcmParam)
        FillAdpcmParam(context.outAdpcmParam, context.outAdpcmLoopParam, adpcmInfo);

    if (context.outAdpcmParamStream)
        FillAdpcmParam(context.outAdpcmParamStream, context.outAdpcmLoopParamStream, adpcmInfoStream);
}

static u8* EncodeDspAdpcm(const ChannelConvertContext& context, s16* spooledPcm, bool isSourceAdpcm, u32 bufferSize)
{
    u8* dst = new u8[bufferSize];

    ADPCMINFO adpcmInfo;
    sead::MemUtil::fillZero(&adpcmInfo, sizeof(adpcmInfo));

    if (isSourceAdpcm && context.srcAdpcmData != nullptr)
    {
        u32 copiedSamples = EncodeAdpcmAfterSource(context, spooledPcm, dst);

        FillAdpcmInfo(&adpcmInfo, context.channel.getAdpcmParam(false), context.channel.getAdpcmLoopParam(false));
        StoreAdpcmParams(context, dst, adpcmInfo);

        if (context.outSeekData)
        {
            const WaveFile::Channel::SeekData* srcSeekData = context.channel.getFullSeekData_();
            if (!srcSeekData)
                srcSeekData = &context.channel.getSeekData();

            WaveFile::buildSeekTable_(spooledPcm, context.totalSamples, snd::SampleFormat::PcmS16, context.outSeekData, srcSeekData, copiedSamples);
        }
    }
    else
    {
        encode(spooledPcm, dst, &adpcmInfo, context.totalSamples);
        StoreAdpcmParams(context, dst, adpcmInfo);

        if (context.outSeekData)
            WaveFile::buildSeekTable_(spooledPcm, context.totalSamples, snd::SampleFormat::PcmS16, context.outSeekData);
    }

    return dst;
}

static void StoreSizes(u32* outSize, u32* outSizeMin, u32 size, u32 sizeMin)
{
    if (outSize)
        *outSize = size;

    if (outSizeMin)
        *outSizeMin = sizeMin;
}

void* WaveFile::convertChannel_(
    Channel& channel, const void* data, sead::Endian::Types dataEndian,
    Encoding from, Encoding to, u32* outSize, u32* outSizeMin, bool updateChannel, bool isLoop,
    u32 sampleCount, u32 targetSampleCount, u32 originalLoopStartFrame, u32 originalLoopEndFrame,
    u32 loopStartFrame, u32 loopEndFrame, u32 loopStartFrameStream, u32 loopEndFrameStream,
    snd::DspAdpcmParam* outAdpcmParam, snd::internal::DspAdpcmLoopParam* outAdpcmLoopParam,
    snd::DspAdpcmParam* outAdpcmParamStream, snd::internal::DspAdpcmLoopParam* outAdpcmLoopParamStream,
    Channel::SeekData* outSeekData)
{
    LOG_U32("from", static_cast<u32>(from));
    LOG_U32("to", static_cast<u32>(to));
    LOG_U32("sampleCount", sampleCount);
    LOG_U32("targetSampleCount", targetSampleCount);
    LOG_BOOL("isLoop", isLoop);
    LOG_U32("loopStartFrame", loopStartFrame);
    LOG_U32("loopEndFrame", loopEndFrame);
    LOG_U32("loopStartFrameStream", loopStartFrameStream);
    LOG_U32("loopEndFrameStream", loopEndFrameStream);

    if (from != Encoding::Pcm8 && from != Encoding::Pcm16 && from != Encoding::DspAdpcm)
    {
        SEAD_ASSERT_MSG(false, "Invalid source encoding");
        return nullptr;
    }

    const u32 framesAfterLoopEnd = (isLoop && to == Encoding::DspAdpcm) ? cFramesAfterLoopEnd : 0;

    ChannelConvertContext context = {
        channel, updateChannel, isLoop,
        sampleCount, targetSampleCount,
        originalLoopStartFrame, originalLoopEndFrame,
        loopStartFrame, loopStartFrameStream, loopEndFrameStream,
        loopEndFrameStream + framesAfterLoopEnd, loopEndFrame + framesAfterLoopEnd,
        nullptr,
        outAdpcmParam, outAdpcmLoopParam, outAdpcmParamStream, outAdpcmLoopParamStream, outSeekData
    };

    const bool shouldPreserveFullData = originalLoopEndFrame < sampleCount;

    if (channel.getFullData_())
    {
        data = channel.getFullData_();
        from = channel.getFullDataEncoding_();
        dataEndian = channel.getFullDataEndian_();
    }

    s16* basePcm = DecodeBasePcm(context, data, from, dataEndian);

    if (updateChannel && shouldPreserveFullData)
        PreserveFullData(channel, data, from, dataEndian);

    s16* spooledPcm = SpoolPcm(context, basePcm);
    delete[] basePcm;

    void* dstData = nullptr;

    if (to == Encoding::Pcm16)
    {
        dstData = spooledPcm;
        StoreSizes(outSize, outSizeMin, sizeof(s16) * context.totalSamples, sizeof(s16) * context.totalSamplesMin);
    }
    else if (to == Encoding::Pcm8)
    {
        s8* dst = new s8[context.totalSamples];
        for (u32 i = 0; i < context.totalSamples; i++)
        {
            dst[i] = s8(spooledPcm[i] >> 8);
        }

        dstData = dst;
        StoreSizes(outSize, outSizeMin, sizeof(s8) * context.totalSamples, sizeof(s8) * context.totalSamplesMin);

        delete[] spooledPcm;
    }
    else if (to == Encoding::DspAdpcm)
    {
        u32 bufferSize = getBytesForAdpcmBuffer(context.totalSamples);
        u32 bufferSizeMin = getBytesForAdpcmBuffer(context.totalSamplesMin);

        dstData = EncodeDspAdpcm(context, spooledPcm, from == Encoding::DspAdpcm, bufferSize);
        StoreSizes(outSize, outSizeMin, bufferSize, bufferSizeMin);

        delete[] spooledPcm;
    }

    if (updateChannel && !shouldPreserveFullData && channel.getFullData_())
        channel.freeFullData_();

    if (updateChannel && !shouldPreserveFullData && channel.getFullSeekData_())
        channel.freeFullSeekData_();

    return dstData;
}

bool WaveFile::setupPreviewPcm16(const DecodedPcm& pcm, bool isLoop, u32 loopStartFrame, u32 loopEndFrame)
{
    if (!pcm.isValid())
        return false;

    {
        snd::internal::driver::SoundThreadLock lock;
        mChannels.clear();
    }

    mDataEndian = sead::Endian::getHostEndian();
    mEncoding = Encoding::Pcm16;
    mSampleRate = pcm.sampleRate;
    mSampleCount = pcm.sampleCount;
    mIsLoop = isLoop;
    mLoopStartFrame = loopStartFrame;
    mLoopEndFrame = loopEndFrame;
    mDataSpoolFrames = 0;
    mIsLoopDirty = false;

    for (const auto& chFloats : pcm.channels)
    {
        Channel* channel = mChannels.birthBack();
        SEAD_ASSERT(channel);

        s16* data = new s16[pcm.sampleCount];
        const float* src = chFloats.data();

        for (u32 i = 0; i < pcm.sampleCount; i++)
            data[i] = AudioProcessing::toPcm16(src[i]);

        channel->mOwnsData = true;
        channel->mData = data;
        channel->mDataSize = pcm.sampleCount * sizeof(s16);
        channel->mDataSizeMin = channel->mDataSize;
    }

    return true;
}
