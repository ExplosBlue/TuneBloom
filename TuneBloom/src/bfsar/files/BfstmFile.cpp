#include <bfsar/files/BfstmFile.h>

#include <core/Debug.h>
#include <core/ParallelFor.h>

#include <bfsar/files/WaveFile.h>
#include <bfsar/io/FileWriter.h>

#include <ui/Messages.h>
#include <ui/app/PopupMgr.h>
#include <ui/app/UI.h>
#include <ui/items/Selection.h>

#include <snd/snd_StreamSoundFileReader.h>
#include <snd/StreamSoundPlayer.h>

#include <stream/seadFileDeviceStream.h>

#include <vector>

static constexpr u32 cSignatureStep  = 4096;
static constexpr u32 cSignatureBytes = 64;

void MixWaveChannelData(StateHash& hash, const WaveFile& wave)
{
    const sead::ObjList<WaveFile::Channel>& channels = wave.getChannels();

    hash.mixValue<u32>(channels.size());

    for (s32 ch = 0; ch < channels.size(); ch++)
    {
        const WaveFile::Channel& channel = *channels.nth(ch);
        u32 dataSize = channel.getDataSize();

        hash.mixValue<u32>(dataSize);

        const u8* data = static_cast<const u8*>(channel.getData());
        if (!data)
            continue;

        for (u32 offset = 0; offset < dataSize; offset += cSignatureStep)
        {
            hash.mixBytes(data + offset, dataSize - offset < cSignatureBytes ? dataSize - offset : cSignatureBytes);
        }
    }
}

u32 BfstmFile::GetFileVersion(const Sound::StreamSoundInfo &soundInfo, const InnerFileTarget &target)
{
    const StreamFileLayout* layout = soundInfo.getFileLayout();

    if (!layout || layout->version == 0 || target.format != ArchiveFormat::BFSAR)
        return target.version;

    if (IsTrackInfoAvailable(layout->version, target.format) != IsTrackInfoAvailable(target.version, target.format))
        return target.version;

    return layout->version;
}

u64 BfstmFile::ComputeContentSignature(const Sound::StreamSoundInfo &soundInfo, const InnerFileTarget &target)
{
    StateHash hash;

    hash.mixValue<u32>(GetFileVersion(soundInfo, target));
    hash.mixValue<u32>(static_cast<u32>(target.endian));
    hash.mixValue<u32>(static_cast<u32>(target.format));

    if (const StreamFileLayout* layout = soundInfo.getFileLayout())
        layout->mixInto(hash);

    const Sound::StreamSoundInfo::Track::List& trackList = soundInfo.getTrackList();

    hash.mixValue<u32>(trackList.size());

    for (u32 i = 0; i < trackList.size(); i++)
    {
        const Sound::StreamSoundInfo::Track* track = static_cast<const Sound::StreamSoundInfo::Track*>(trackList.nth(i)->val());

        hash.mixValue<u32>(track->getVolume());
        hash.mixValue<u32>(track->getPan());
        hash.mixValue<u32>(track->getSPan());
        hash.mixValue<u32>(track->getFlags());
        hash.mixValue<u32>(track->getChannelCount());
        hash.mixValue<u32>(track->getWaveFileRef().getItemId());

        if (!track->getWaveFileRef().isAttached())
            continue;

        const WaveFile& wave = *static_cast<const WaveFile*>(track->getWaveFileRef().getItem());

        hash.mixValue<u32>(static_cast<u32>(wave.getEncoding()));
        hash.mixValue<u32>(wave.getSampleRate());
        hash.mixValue<u32>(wave.getIsLoop() ? 1u : 0u);
        hash.mixValue<u32>(wave.getLoopStartFrame(true));
        hash.mixValue<u32>(wave.getLoopEndFrame(true));
        hash.mixValue<u32>(wave.getOriginalLoopStartFrame());
        hash.mixValue<u32>(wave.getOriginalLoopEndFrame());
        hash.mixValue<u32>(wave.getSampleCount());
        hash.mixValue<u32>(static_cast<u32>(wave.getDataEndian()));

        MixWaveChannelData(hash, wave);
    }

    return hash.value();
}

static constexpr u32 cBytesPerBlock            = 0x2000;
static constexpr u32 cBytesPerSeekTableEntry   = 2 * sizeof(s16);
static constexpr u32 cBlockAlignment           = 0x20;
static constexpr u32 cLastBlockAlignmentV2_0_1 = 0x40;

namespace
{

struct StreamChannelData
{
    ~StreamChannelData()
    {
        if (shouldDelete)
            delete[] buffer;
    }

    u8* buffer = nullptr;
    u32 bufferSize = 0;
    bool shouldDelete = false;
    snd::DspAdpcmParam adpcmParam;
    snd::internal::DspAdpcmLoopParam adpcmLoopParam;
    WaveFile::Channel::SeekData seekData;
};

struct StreamWriteContext
{
    StreamWriteContext(sead::FileHandle* handle, sead::WriteStream* stream_, u32 version_, sead::Endian::Types endian_, ArchiveFormat format_)
        : writer(handle, stream_)
        , stream(stream_)
        , version(version_)
        , endian(endian_)
        , format(format_)
    {
    }

    FileWriter writer;
    sead::WriteStream* stream;
    u32 version;
    sead::Endian::Types endian;
    ArchiveFormat format;

    u32 trackNum = 0;
    const Sound::StreamSoundInfo::Track* tracks[cStrmTrackNum] = {};

    u32 channelNum = 0;
    const WaveFile::Channel* channels[cStrmChannelNum] = {};
    const WaveFile* channelOwners[cStrmChannelNum] = {};
    StreamChannelData channelData[cStrmChannelNum];

    const WaveFile* mainWave = nullptr;
    const StreamFileLayout* layout = nullptr;
    bool isLoop = false;
    bool isDspAdpcm = false;
    u32 loopStartFrame = 0;
    u32 loopEndFrame = 0;

    u32 sampleCount = 0;
    u32 samplePerBlock = 0;
    u32 blockNum = 0;
    u32 lastBlockSamples = 0;
    u32 lastBlockBytes = 0;
    u32 lastBlockBytesPadded = 0;
};

}

static void CollectTracksAndChannels(StreamWriteContext& context, const Sound::StreamSoundInfo& soundInfo)
{
    context.trackNum = soundInfo.getTrackList().size();

    for (u32 i = 0; i < cStrmTrackNum && i < context.trackNum; i++)
    {
        const Sound::StreamSoundInfo::Track* track = static_cast<const Sound::StreamSoundInfo::Track*>(soundInfo.getTrackList().nth(i)->val());
        context.tracks[i] = track;

        SEAD_ASSERT(track->getWaveFileRef().isAttached());
        const WaveFile& wave = *static_cast<const WaveFile*>(track->getWaveFileRef().getItem());
        for (s32 ch = 0; ch < wave.getChannels().size(); ch++)
        {
            context.channels[context.channelNum] = wave.getChannels().nth(ch);
            context.channelOwners[context.channelNum] = &wave;
            context.channelNum++;
        }
    }

    SEAD_ASSERT(context.channelNum == soundInfo.getAllocateChannelCount());

    SEAD_ASSERT(context.tracks[0]->getWaveFileRef().isAttached());
    context.mainWave = static_cast<const WaveFile*>(context.tracks[0]->getWaveFileRef().getItem());
    context.layout = &context.mainWave->getStreamFileLayout();

    context.loopStartFrame = context.mainWave->getLoopStartFrame(true);
    context.loopEndFrame = context.mainWave->getLoopEndFrame(true);
    context.isLoop = context.mainWave->getIsLoop();
    context.isDspAdpcm = context.mainWave->getEncoding() == WaveFile::Encoding::DspAdpcm;

    LOG_U32("channelNum", context.channelNum);
    LOG_U32("trackNum", context.trackNum);
    LOG_U32("sampleRate", context.mainWave->getSampleRate());
    LOG_U32("loopStartFrame", context.loopStartFrame);
    LOG_U32("loopEndFrame", context.loopEndFrame);
    LOG_BOOL("isLoop", context.isLoop);
    LOG_BOOL("isDspAdpcm", context.isDspAdpcm);
}

static u32 GetLastBlockAlignment(u32 version, ArchiveFormat format)
{
    if (format == ArchiveFormat::BFSAR && version >= makeVersion(2, 0, 1))
        return cLastBlockAlignmentV2_0_1;

    return cBlockAlignment;
}

static void CalcBlockLayout(StreamWriteContext& context)
{
    nw::snd::SampleFormat sampleFormat = nw::snd::internal::WaveFileReader::GetSampleFormat(static_cast<u8>(context.mainWave->getEncoding()));

    context.sampleCount = context.loopEndFrame;
    context.samplePerBlock = nw::snd::internal::Util::GetSampleByByte(cBytesPerBlock, sampleFormat);
    context.blockNum = context.sampleCount / context.samplePerBlock + (context.sampleCount % context.samplePerBlock != 0);
    context.lastBlockSamples = context.sampleCount - ((context.blockNum - 1) * context.samplePerBlock);
    context.lastBlockBytes = nw::snd::internal::Util::GetByteBySample(context.lastBlockSamples, sampleFormat);
    context.lastBlockBytesPadded = sead::Mathu::roundUpPow2(context.lastBlockBytes, GetLastBlockAlignment(context.version, context.format));

    LOG_U32("sampleFormat", static_cast<u32>(sampleFormat));
    LOG_U32("sampleCount", context.sampleCount);
    LOG_U32("blockNum", context.blockNum);
    LOG_U32("lastBlockSamples", context.lastBlockSamples);
    LOG_U32("lastBlockBytes", context.lastBlockBytes);
}

static void ConvertSampleEndian(const StreamWriteContext& context, StreamChannelData& channelData, sead::Endian::Types dataEndian)
{
    if (dataEndian == context.endian)
        return;

    s16* samples = reinterpret_cast<s16*>(channelData.buffer);

    for (u32 s = 0; s < context.sampleCount; s++)
    {
        samples[s] = sead::Endian::convertS16(dataEndian, context.endian, samples[s]);
    }
}

static void CopyStreamChannelData(StreamWriteContext& context, u32 channelIndex, u32 copySize)
{
    const WaveFile& wave = *context.channelOwners[channelIndex];
    const WaveFile::Channel& channel = *context.channels[channelIndex];
    StreamChannelData& channelData = context.channelData[channelIndex];

    channelData.buffer = new u8[copySize];
    channelData.bufferSize = copySize;
    channelData.shouldDelete = true;

    sead::MemUtil::copy(channelData.buffer, channel.getData(), copySize);

    if (context.isDspAdpcm)
    {
        channelData.adpcmParam = channel.getAdpcmParam(true);
        channelData.adpcmLoopParam = channel.getAdpcmLoopParam(true);
        channelData.seekData.mSeekInfo.setBuffer(
            channel.getSeekData().mSeekInfo.size(),
            const_cast<WaveFile::Channel::SeekInfo*>(channel.getSeekData().mSeekInfo.getBufferPtr())
        );
        channelData.seekData.mOwner = false;
    }

    if (wave.getEncoding() == WaveFile::Encoding::Pcm16)
        ConvertSampleEndian(context, channelData, wave.getDataEndian());
}

static void ConvertStreamChannelData(StreamWriteContext& context, u32 channelIndex)
{
    const WaveFile& wave = *context.channelOwners[channelIndex];
    const WaveFile& mainWave = *context.mainWave;
    const WaveFile::Channel& channel = *context.channels[channelIndex];
    StreamChannelData& channelData = context.channelData[channelIndex];

    channelData.buffer = static_cast<u8*>(WaveFile::convertChannel_(
        const_cast<WaveFile::Channel&>(channel), channel.getData(), wave.getDataEndian(),
        wave.getEncoding(), wave.getEncoding(), &channelData.bufferSize, nullptr, false, context.isLoop,
        wave.getSampleCount(), mainWave.getOriginalLoopEndFrame(),
        mainWave.getOriginalLoopStartFrame(), mainWave.getOriginalLoopEndFrame(),
        0, 0,
        context.loopStartFrame, context.loopEndFrame,
        nullptr, nullptr,
        &channelData.adpcmParam, &channelData.adpcmLoopParam,
        &channelData.seekData
    ));
    channelData.shouldDelete = true;

    if (wave.getEncoding() == WaveFile::Encoding::Pcm16)
        ConvertSampleEndian(context, channelData, sead::Endian::getHostEndian());
}

static void PrepareChannelData(StreamWriteContext& context)
{
    u32 mainWaveSize = context.channels[0]->getDataSize();
    u32 channelWriteBytes = cBytesPerBlock * (context.blockNum - 1) + context.lastBlockBytes;

    for (u32 i = 0; i < context.channelNum; i++)
    {
        const WaveFile& wave = *context.channelOwners[i];

        bool isReadyForStream = wave.getIsStreamExtended() &&
                                wave.getLoopStartFrame(true) == context.loopStartFrame &&
                                wave.getLoopEndFrame(true) == context.loopEndFrame &&
                                context.channels[i]->getDataSize() >= channelWriteBytes;

        if (isReadyForStream)
            CopyStreamChannelData(context, i, mainWaveSize);
        else
            ConvertStreamChannelData(context, i);
    }
}

static void WriteStreamSoundInfo(StreamWriteContext& context)
{
    FileWriter& writer = context.writer;
    sead::WriteStream& stream = *context.stream;
    const WaveFile& mainWave = *context.mainWave;

    stream.writeU8(static_cast<u8>(mainWave.getEncoding()));
    stream.writeU8(context.isLoop);
    stream.writeU8(static_cast<u8>(context.channelNum));
    stream.writeU8(static_cast<u8>(context.layout->regions.size()));
    stream.writeU32(mainWave.getSampleRate());
    stream.writeU32(context.loopStartFrame);
    stream.writeU32(context.sampleCount);
    stream.writeU32(context.blockNum);
    stream.writeU32(cBytesPerBlock);
    stream.writeU32(context.samplePerBlock);
    stream.writeU32(context.lastBlockBytes);
    stream.writeU32(context.lastBlockSamples);
    stream.writeU32(context.lastBlockBytesPadded);
    stream.writeU32(cBytesPerSeekTableEntry);
    stream.writeU32(context.samplePerBlock);

    writer.openReference("SampleDataRef");

    stream.writeU16(sizeof(nw::snd::internal::StreamSoundFile::RegionInfo));
    stream.writeU16(0); // Padding

    writer.openReference("RegionDataRef");

    if (context.layout->regions.empty())
        writer.closeNullReference("RegionDataRef");

    if (BfstmFile::IsOriginalLoopAvailable(context.version, context.format))
    {
        stream.writeU32(mainWave.getOriginalLoopStartFrame());
        stream.writeU32(mainWave.getOriginalLoopEndFrame());

        if (context.layout->hasUnknown4C)
            stream.writeU32(context.layout->unknown4C);
    }
}

static void WriteTrackInfoTable(StreamWriteContext& context)
{
    FileWriter& writer = context.writer;
    sead::WriteStream& stream = *context.stream;

    writer.openReferenceTable("TrackInfoTable", context.trackNum);

    u32 channelIdxStart = 0;
    for (u32 i = 0; i < context.trackNum; i++)
    {
        writer.addReferenceTableReference("TrackInfoTable", nw::snd::internal::ElementType_StreamSoundFile_TrackInfo);

        const Sound::StreamSoundInfo::Track& track = *context.tracks[i];

        writer.pushOffsetBase();
        {
            stream.writeU8(track.getVolume());
            stream.writeU8(track.getPan());
            stream.writeU8(track.getSPan());
            stream.writeU8(track.getFlags());

            writer.openReference("GlobalChannelIndexTable");

            writer.closeReference("GlobalChannelIndexTable", nw::snd::internal::ElementType_Table_EmbeddingTable);
            stream.writeU32(track.getChannelCount());

            for (u32 ch = 0; ch < track.getChannelCount(); ch++)
            {
                stream.writeU8(channelIdxStart + ch);
            }

            writer.align(0x4);
        }
        writer.popOffsetBase();

        channelIdxStart += track.getChannelCount();
    }

    writer.closeReferenceTable("TrackInfoTable");
}

static void WriteChannelInfoTable(StreamWriteContext& context)
{
    FileWriter& writer = context.writer;

    writer.openReferenceTable("ChannelInfoTable", context.channelNum);

    for (u32 i = 0; i < context.channelNum; i++)
    {
        writer.addReferenceTableReference("ChannelInfoTable", nw::snd::internal::ElementType_StreamSoundFile_ChannelInfo);

        writer.pushOffsetBase();
        {
            writer.openReference(sead::FormatFixedSafeString<32>("DetailChannelInfoRef%i", i));
        }
        writer.popOffsetBase();
    }

    for (u32 i = 0; i < context.channelNum; i++)
    {
        sead::FormatFixedSafeString<32> refName("DetailChannelInfoRef%i", i);
        if (!context.isDspAdpcm)
        {
            writer.closeNullReference(refName);
            continue;
        }

        writer.closeReference(refName, nw::snd::internal::ElementType_Codec_DspAdpcmInfo);

        WriteDspAdpcmInfo(context.stream, context.channelData[i].adpcmParam, context.channelData[i].adpcmLoopParam);
    }

    writer.closeReferenceTable("ChannelInfoTable");
}

static void WriteInfoBlock(StreamWriteContext& context)
{
    FileWriter& writer = context.writer;

    writer.openBlock(nw::snd::internal::ElementType_StreamSoundFile_InfoBlock, "INFO");

    writer.openReference("StreamSoundInfoRef");
    writer.openReference("TrackInfoTableRef");
    writer.openReference("ChannelInfoTableRef");

    writer.closeReference("StreamSoundInfoRef", nw::snd::internal::ElementType_StreamSoundFile_StreamSoundInfo);
    writer.pushOffsetBase();
    {
        WriteStreamSoundInfo(context);
    }
    writer.popOffsetBase();

    if (BfstmFile::IsTrackInfoAvailable(context.version, context.format))
    {
        writer.closeReference("TrackInfoTableRef", nw::snd::internal::ElementType_Table_ReferenceTable);
        writer.pushOffsetBase();
        {
            WriteTrackInfoTable(context);
        }
        writer.popOffsetBase();
    }
    else
    {
        writer.closeNullReference("TrackInfoTableRef");
    }

    writer.closeReference("ChannelInfoTableRef", nw::snd::internal::ElementType_Table_ReferenceTable);
    writer.pushOffsetBase();
    {
        WriteChannelInfoTable(context);
    }
    writer.popOffsetBase();

    writer.align(cBlockAlignment);
    writer.closeBlock();
}

static u32 GetSeekRowCount(const StreamWriteContext& context)
{
    u32 rowCount = context.channelNum > 0 ? static_cast<u32>(context.channelData[0].seekData.getSeekInfoBlocks()) : 0;

    for (u32 ch = 1; ch < context.channelNum; ch++)
        rowCount = std::min(rowCount, static_cast<u32>(context.channelData[ch].seekData.getSeekInfoBlocks()));

    return std::max(rowCount, context.blockNum);
}

static void WriteSeekBlock(StreamWriteContext& context)
{
    FileWriter& writer = context.writer;

    writer.openBlock(nw::snd::internal::ElementType_StreamSoundFile_SeekBlock, "SEEK");

    for (u32 rowNo = 0; rowNo < GetSeekRowCount(context); rowNo++)
    {
        for (u32 ch = 0; ch < context.channelNum; ch++)
        {
            const StreamChannelData& channelData = context.channelData[ch];

            s16 s1 = sead::Endian::fromHostS16(sead::Endian::eLittle, channelData.seekData.getSeekInfo(rowNo).yn1);
            s16 s2 = sead::Endian::fromHostS16(sead::Endian::eLittle, channelData.seekData.getSeekInfo(rowNo).yn2);

            context.stream->writeMemBlock(&s1, sizeof(s16));
            context.stream->writeMemBlock(&s2, sizeof(s16));
        }
    }

    writer.align(cBlockAlignment);
    writer.closeBlock();
}

static void WriteRegionBlock(StreamWriteContext& context)
{
    FileWriter& writer = context.writer;

    writer.openBlock(nw::snd::internal::ElementType_StreamSoundFile_RegionBlock, "REGN");

    u32 regionBlockPos = writer.getPosition();
    writer.align(cBlockAlignment);

    writer.closeReference("RegionDataRef", 0, writer.getPosition() - regionBlockPos);

    const u8* channels[cStrmChannelNum] = {};

    for (u32 ch = 0; ch < context.channelNum; ch++)
        channels[ch] = context.channelData[ch].buffer;

    StreamRegionSampleData sampleData;
    sampleData.channels = channels;
    sampleData.channelCount = context.channelNum;
    sampleData.channelBytes = cBytesPerBlock * (context.blockNum - 1) + context.lastBlockBytes;
    sampleData.isDspAdpcm = context.isDspAdpcm;

    WriteStreamRegions(context.stream, *context.layout, sampleData);

    writer.closeBlock();
}

static void WriteChannelBlock(sead::WriteStream* stream, const StreamChannelData& channelData, u32 offset, u32 blockBytes)
{
    const u32 availableBytes = offset < channelData.bufferSize ? channelData.bufferSize - offset : 0;
    const u32 dataBytes = blockBytes < availableBytes ? blockBytes : availableBytes;

    stream->writeMemBlock(channelData.buffer + offset, dataBytes);

    for (u32 i = dataBytes; i < blockBytes; i++)
        stream->writeU8(0);
}

static void WriteDataBlock(StreamWriteContext& context)
{
    FileWriter& writer = context.writer;

    writer.openBlock(nw::snd::internal::ElementType_StreamSoundFile_DataBlock, "DATA");

    u32 dataBlockPos = writer.getPosition();
    writer.align(cBlockAlignment);

    writer.closeReference("SampleDataRef", nw::snd::internal::ElementType_General_ByteStream, writer.getPosition() - dataBlockPos);

    // Interleave channel blocks
    for (u32 blockNo = 0; blockNo < context.blockNum; blockNo++)
    {
        for (u32 ch = 0; ch < context.channelNum; ch++)
        {
            bool isLastBlock = blockNo == context.blockNum - 1;
            u32 offset = blockNo * cBytesPerBlock;
            u32 blockBytes = isLastBlock ? context.lastBlockBytesPadded : cBytesPerBlock;

            WriteChannelBlock(context.stream, context.channelData[ch], offset, blockBytes);
        }
    }

    writer.closeBlock();
}

bool BfstmFile::WriteBfstmFile(sead::FileHandle& handle, const Sound::StreamSoundInfo& soundInfo, const InnerFileTarget& target)
{
    const u32 version = GetFileVersion(soundInfo, target);

    LOG_FUNC();
    LOG_FMT("format=%s, version=0x%04X, endian=%d", GetInnerFileMagic(target.format, InnerFileKind::Stream), version, target.endian);

    CheckedFileStreamSrc streamSrc(&handle);
    sead::WriteStream stream(&streamSrc, sead::Stream::Modes::eBinary);
    stream.setBinaryEndian(target.endian);

    SEAD_ASSERT(soundInfo.getTrackList().size() > 0);

    StreamWriteContext context(&handle, &stream, version, target.endian, target.format);
    CollectTracksAndChannels(context, soundInfo);
    CalcBlockLayout(context);
    PrepareChannelData(context);

    bool hasSeekBlock = context.isDspAdpcm;
    bool hasRegionBlock = !context.layout->regions.empty();
    u32 fileBlockCount = 2 + (hasSeekBlock ? 1 : 0) + (hasRegionBlock ? 1 : 0);

    context.writer.openFile(GetInnerFileMagic(target.format, InnerFileKind::Stream), fileBlockCount, version);

    WriteInfoBlock(context);

    if (hasSeekBlock)
        WriteSeekBlock(context);

    if (hasRegionBlock)
        WriteRegionBlock(context);

    WriteDataBlock(context);

    context.writer.closeFile();

    return !streamSrc.hasFailed() && context.writer.isFileComplete();
}

bool BfstmFile::IsComplete(const void *data, u32 size)
{
    if (!data || size < sizeof(nw::ut::BinaryFileHeader))
        return false;

    const nw::ut::BinaryFileHeader &header = *static_cast<const nw::ut::BinaryFileHeader *>(data);
    return header.fileSize <= size;
}

static bool sIsLoopInfoBatch = false;
static std::vector<WaveFile*> sLoopInfoBatchWaves;

StreamLoopInfoBatch::StreamLoopInfoBatch()
{
    sIsLoopInfoBatch = true;
}

StreamLoopInfoBatch::~StreamLoopInfoBatch()
{
    Flush();

    sIsLoopInfoBatch = false;
}

void StreamLoopInfoBatch::Flush()
{
    ParallelFor(static_cast<u32>(sLoopInfoBatchWaves.size()), [](u32 index)
    {
        sLoopInfoBatchWaves[index]->updateLoopInfo_(true, false);
    });

    sLoopInfoBatchWaves.clear();
}

class StreamWaveReader
{
public:
    StreamWaveReader(StreamWaveOwner& owner, Sound::StreamSoundInfo& soundInfo, const sead::SafeString& streamName, const void* strmFile, u32 strmFileSize)
        : mOwner(owner)
        , mSoundInfo(soundInfo)
        , mStreamName(streamName)
        , mStrmFile(strmFile)
        , mStrmFileSize(strmFileSize)
    {
    }

    ~StreamWaveReader()
    {
        for (u32 i = 0; i < cStrmChannelNum; i++)
        {
            if (!mChannelBuffersUsed[i])
                delete[] mChannelBuffers[i];
        }
    }

    bool read();

private:
    bool readStreamSoundInfo_();
    void readChannelBuffers_();
    WaveFile* createTrackWave_(u32 trackNo, u32 trackCount);
    void readFileLayout_(WaveFile* wave);
    bool checkTrackLoop_(const WaveFile& wave, u32 trackNo) const;
    void readTrackChannel_(WaveFile* wave, u32 trackNo, s8 globalChannelIndex);
    void readAdpcmChannelInfo_(WaveFile::Channel* channel, u32 trackNo, s8 globalChannelIndex);
    u32 getSeekRowCount() const;

    void pushTrackWarning_(const char* format, u32 trackNo)
    {
        sead::FormatFixedSafeString<1024> msg(format, trackNo);
        PopupMgr::instance()->pushCurrentItemWarning(msg, messages::stream::cAudioUnreadableDetail);

        mReadAllTracks = false;
    }

private:
    StreamWaveOwner& mOwner;
    Sound::StreamSoundInfo& mSoundInfo;
    const sead::SafeString& mStreamName;
    const void* mStrmFile;
    u32 mStrmFileSize;

    nw::snd::internal::StreamSoundFileReader mReader;
    nw::snd::internal::StreamSoundFile::StreamSoundInfo mStreamSoundInfo;
    WaveFile::Encoding mEncoding = WaveFile::Encoding::DspAdpcm;
    u32 mChannelSize = 0;

    u8* mChannelBuffers[cStrmChannelNum] = {};
    bool mChannelBuffersUsed[cStrmChannelNum] = {};

    bool mReadAllTracks = true;
};

bool StreamWaveReader::readStreamSoundInfo_()
{
    mReader.Initialize(mStrmFile);

    if (!mReader.IsAvailable())
    {
        LOG("ReadStreamWaves: reader not available");
        return false;
    }

    if (!mReader.ReadStreamSoundInfo(&mStreamSoundInfo))
    {
        LOG("ReadStreamWaves: failed to read StreamSoundInfo");
        return false;
    }

    LOG_U32("encodeMethod", mStreamSoundInfo.encodeMethod);
    LOG_BOOL("isLoop", mStreamSoundInfo.isLoop);
    LOG_U32("channelCount", mStreamSoundInfo.channelCount);
    LOG_U32("sampleRate", mStreamSoundInfo.sampleRate);
    LOG_U32("blockCount", mStreamSoundInfo.blockCount);
    LOG_U32("oneBlockBytes", mStreamSoundInfo.oneBlockBytes);
    LOG_U32("lastBlockPaddedBytes", mStreamSoundInfo.lastBlockPaddedBytes);
    LOG_U32("frameCount", mStreamSoundInfo.frameCount);
    LOG_U32("loopStart", mStreamSoundInfo.loopStart);
    LOG_U32("originalLoopStart", mStreamSoundInfo.originalLoopStart);
    LOG_U32("originalLoopEnd", mStreamSoundInfo.originalLoopEnd);

    mEncoding = static_cast<WaveFile::Encoding>(mStreamSoundInfo.encodeMethod);

    const u64 channelCount = mStreamSoundInfo.channelCount;
    const u64 blockCount = mStreamSoundInfo.blockCount;

    if (blockCount == 0 || channelCount == 0 || channelCount > cStrmChannelNum)
    {
        LOG("ReadStreamWaves: invalid block or channel count");
        return false;
    }

    const u64 sampleDataEnd = mReader.GetSampleDataOffset() +
                              (blockCount - 1) * mStreamSoundInfo.oneBlockBytes * channelCount +
                              static_cast<u64>(mStreamSoundInfo.lastBlockPaddedBytes) * channelCount;

    const u64 seekDataEnd = mEncoding == WaveFile::Encoding::DspAdpcm
                                ? mReader.GetSeekBlockOffset() + sizeof(nw::ut::BinaryBlockHeader) + blockCount * channelCount * sizeof(WaveFile::Channel::SeekInfo)
                                : 0;

    if (sampleDataEnd > mStrmFileSize || seekDataEnd > mStrmFileSize)
    {
        LOG("ReadStreamWaves: file is shorter than its blocks");
        return false;
    }

    mChannelSize = mStreamSoundInfo.oneBlockBytes * (mStreamSoundInfo.blockCount - 1) + mStreamSoundInfo.lastBlockPaddedBytes;

    LOG_U32("sampleDataOffset", mReader.GetSampleDataOffset());
    LOG_U32("seekBlockOffset", mReader.GetSeekBlockOffset());
    LOG_U32("channelSize", mChannelSize);

    return true;
}

void StreamWaveReader::readChannelBuffers_()
{
    for (u32 i = 0; i < mStreamSoundInfo.channelCount; i++)
    {
        mChannelBuffers[i] = new u8[mChannelSize];
    }

    const u8* sampleData = static_cast<const u8*>(mStrmFile) + mReader.GetSampleDataOffset();

    for (u32 blockNo = 0; blockNo < mStreamSoundInfo.blockCount; blockNo++)
    {
        for (u32 channelNo = 0; channelNo < mStreamSoundInfo.channelCount; channelNo++)
        {
            bool isLastBlock = blockNo == mStreamSoundInfo.blockCount - 1;
            u32 blockBytes = isLastBlock ? mStreamSoundInfo.lastBlockPaddedBytes : mStreamSoundInfo.oneBlockBytes;

            const void* src = sampleData;
            sampleData += blockBytes;

            void* dst = mChannelBuffers[channelNo] + (blockNo * mStreamSoundInfo.oneBlockBytes);

            sead::MemUtil::copy(dst, src, blockBytes);
        }
    }
}

WaveFile* StreamWaveReader::createTrackWave_(u32 trackNo, u32 trackCount)
{
    WaveFile* wave = new WaveFile();
    mOwner.prepareStreamWave(wave);
    wave->mIsFromStreamFile = true;

    mOwner.nameStreamWave(wave, mStreamName, trackNo, trackCount);

    wave->mDataEndian = nw::ut::GetFileEndian(*mReader.mHeader);
    wave->mEncoding = mEncoding;
    wave->mIsLoop = mStreamSoundInfo.isLoop;
    wave->mSampleRate = mStreamSoundInfo.sampleRate;
    wave->mLoopStartFrame = mStreamSoundInfo.originalLoopStart;
    wave->mLoopEndFrame = mStreamSoundInfo.originalLoopEnd;

    return wave;
}

void StreamWaveReader::readFileLayout_(WaveFile* wave)
{
    if (ReadStreamFileLayout(&wave->mStreamFileLayout, mReader, mStrmFileSize) == StreamRegionReadResult::Invalid)
        PopupMgr::instance()->pushCurrentItemWarning(messages::stream::cRegionsUnreadable);

    LOG_U32("regionCount", static_cast<u32>(wave->mStreamFileLayout.regions.size()));
}

bool StreamWaveReader::checkTrackLoop_(const WaveFile& wave, u32 trackNo) const
{
    bool isLoopValid = true;

    if (wave.mLoopStartFrame >= wave.mLoopEndFrame)
    {
        sead::FormatFixedSafeString<1024> msg(messages::stream::cTrackLoopInvalidFormat, trackNo);
        PopupMgr::instance()->pushCurrentItemWarning(msg, messages::stream::cAudioUnreadableDetail);
        isLoopValid = false;
    }

    if (wave.getLoopStartFrame(true) != mStreamSoundInfo.loopStart)
    {
        sead::FormatFixedSafeString<1024> msg(messages::stream::cTrackLoopStartMismatchFormat, trackNo);
        PopupMgr::instance()->pushCurrentItemWarning(msg, messages::stream::cAudioUnreadableDetail);
        isLoopValid = false;
    }

    if (wave.getLoopEndFrame(true) != mStreamSoundInfo.frameCount)
    {
        sead::FormatFixedSafeString<1024> msg(messages::stream::cTrackLoopEndMismatchFormat, trackNo);
        PopupMgr::instance()->pushCurrentItemWarning(msg, messages::stream::cAudioUnreadableDetail);
        isLoopValid = false;
    }

    return isLoopValid;
}

void StreamWaveReader::readAdpcmChannelInfo_(WaveFile::Channel* channel, u32 trackNo, s8 globalChannelIndex)
{
    nw::snd::DspAdpcmParam adpcmParam;
    nw::snd::internal::DspAdpcmLoopParam adpcmLoopParam;
    if (mReader.ReadDspAdpcmChannelInfo(&adpcmParam, &adpcmLoopParam, globalChannelIndex))
        channel->setAdpcmParams_(adpcmParam, adpcmLoopParam);
    else
        pushTrackWarning_(messages::stream::cTrackAdpcmUnreadableFormat, trackNo);

    const u32 rowCount = getSeekRowCount();

    channel->mSeekData.mSeekInfo.allocBuffer(rowCount);
    channel->mSeekData.mOwner = true;

    const u8* seekStart = static_cast<const u8*>(mStrmFile) + mReader.GetSeekBlockOffset() + sizeof(nw::ut::BinaryBlockHeader);

    WaveFile::Channel::SeekInfo* seekInfo = channel->mSeekData.mSeekInfo.getBufferPtr();
    for (u32 rowNo = 0; rowNo < rowCount; rowNo++)
    {
        const WaveFile::Channel::SeekInfo* rowSeek = reinterpret_cast<const WaveFile::Channel::SeekInfo*>(seekStart);
        rowSeek += rowNo * mStreamSoundInfo.channelCount;
        seekInfo[rowNo].yn1 = rowSeek[globalChannelIndex].yn1;
        seekInfo[rowNo].yn2 = rowSeek[globalChannelIndex].yn2;
    }
}

u32 StreamWaveReader::getSeekRowCount() const
{
    const u64 blockSize = mReader.GetSeekBlockSize();
    const u64 rowSize = static_cast<u64>(mStreamSoundInfo.channelCount) * sizeof(WaveFile::Channel::SeekInfo);

    if (blockSize < sizeof(nw::ut::BinaryBlockHeader) || mReader.GetSeekBlockOffset() + blockSize > mStrmFileSize)
        return mStreamSoundInfo.blockCount;

    return static_cast<u32>(std::max<u64>(mStreamSoundInfo.blockCount, (blockSize - sizeof(nw::ut::BinaryBlockHeader)) / rowSize));
}

void StreamWaveReader::readTrackChannel_(WaveFile* wave, u32 trackNo, s8 globalChannelIndex)
{
    if (globalChannelIndex < 0 || globalChannelIndex >= static_cast<s32>(cStrmChannelNum) || !mChannelBuffers[globalChannelIndex])
    {
        pushTrackWarning_(messages::stream::cTrackChannelMissingFormat, trackNo);
        return;
    }

    WaveFile::Channel* channel = wave->mChannels.birthBack();
    SEAD_ASSERT(channel);

    channel->mOwnsData = true;
    channel->mData = mChannelBuffers[globalChannelIndex];
    channel->mDataSize = mChannelSize;

    if (wave->mEncoding == WaveFile::Encoding::DspAdpcm)
        channel->mDataSizeMin = getBytesForAdpcmBuffer(wave->getLoopEndFrame(false));
    else
        channel->mDataSizeMin = nw::snd::internal::Util::GetByteBySample(wave->getLoopEndFrame(false), nw::snd::internal::WaveFileReader::GetSampleFormat(static_cast<u8>(wave->mEncoding)));

    channel->mOriginalDataOffset = 0;

    mChannelBuffersUsed[globalChannelIndex] = true;

    if (wave->mEncoding == WaveFile::Encoding::DspAdpcm)
        readAdpcmChannelInfo_(channel, trackNo, globalChannelIndex);
}

bool StreamWaveReader::read()
{
    if (!readStreamSoundInfo_())
        return false;

    readChannelBuffers_();

    Sound::StreamSoundInfo::Track::List& tracks = mSoundInfo.getTrackList();

    for (u32 trackNo = 0; trackNo < tracks.size(); trackNo++)
    {
        Sound::StreamSoundInfo::Track& track = *static_cast<Sound::StreamSoundInfo::Track*>(tracks.nth(trackNo)->val());

        WaveFile* wave = createTrackWave_(trackNo, tracks.size());
        bool isLoopValid = checkTrackLoop_(*wave, trackNo);

        if (trackNo == 0)
            readFileLayout_(wave);

        mOwner.addStreamWave(wave);
        track.getWaveFileRef().attach(wave);

        if (!isLoopValid)
        {
            mReadAllTracks = false;
            continue;
        }

        wave->mSampleCount = wave->mLoopEndFrame;

        wave->mUseOriginalData = false;

        for (s32 ch = 0; ch < track.getChannels_().size() && ch < snd::cWaveChannelMax; ch++)
        {
            readTrackChannel_(wave, trackNo, *track.getChannels_().nth(ch));
        }

        if (wave->mEncoding == WaveFile::Encoding::DspAdpcm)
        {
            if (sIsLoopInfoBatch)
                sLoopInfoBatchWaves.push_back(wave);
            else
                wave->updateLoopInfo_(true, false);
        }

        wave->mIsLoopDirty = false;
        wave->mIsStreamExtended = true;
    }

    return mReadAllTracks;
}

static bool AttachSharedStreamWaves(Sound* sound, const Sound* srcSound)
{
    Sound::StreamSoundInfo::Track::List& tracks = sound->getStreamSoundInfo().getTrackList();
    const Sound::StreamSoundInfo::Track::List& srcTracks = srcSound->getStreamSoundInfo().getTrackList();

    if (tracks.size() != srcTracks.size())
    {
        sead::FormatFixedSafeString<1024> msg(messages::stream::cSharedTrackCountFormat,
                                              sound->getFormattedName().cstr(), srcSound->getFormattedName().cstr(), tracks.size(), srcTracks.size());
        PopupMgr::instance()->pushCurrentItemWarning(msg, messages::stream::cAudioUnreadableDetail);
        return false;
    }

    for (u32 trackNo = 0; trackNo < tracks.size(); trackNo++)
    {
        Sound::StreamSoundInfo::Track& track = *static_cast<Sound::StreamSoundInfo::Track*>(tracks.nth(trackNo)->val());
        const Sound::StreamSoundInfo::Track& srcTrack = *static_cast<const Sound::StreamSoundInfo::Track*>(srcTracks.nth(trackNo)->val());

        track.getWaveFileRef().attach(const_cast<Item*>(srcTrack.getWaveFileRef().getItem()));
    }

    return true;
}

bool ReadStreamWaves(StreamWaveOwner& owner, Sound::StreamSoundInfo& soundInfo, const sead::SafeString& streamName, const void* strmFile, u32 strmFileSize)
{
    LOG_FUNC();

    if (soundInfo.getTrackList().isEmpty())
    {
        LOG("ReadStreamWaves: tracks is empty");
        return false;
    }

    StreamWaveReader reader(owner, soundInfo, streamName, strmFile, strmFileSize);
    return reader.read();
}

bool ReadStreamWaves(Bfsar& archive, Sound* sound, const void* strmFile, u32 strmFileSize, const Sound* srcSound)
{
    if (srcSound && !sound->getStreamSoundInfo().getTrackList().isEmpty())
        return AttachSharedStreamWaves(sound, srcSound);

    return ReadStreamWaves(archive, sound->getStreamSoundInfo(), sound->getName(), strmFile, strmFileSize);
}

void NameStreamTrackWave(WaveFile* wave, const sead::SafeString& streamName, u32 trackNo, u32 trackCount)
{
    wave->setEnableName(true);

    if (trackCount == 1)
        wave->getName().format("GUESS_%s", streamName.cstr());
    else
        wave->getName().format("GUESS_%s_TRACK_%u", streamName.cstr(), trackNo);
}

bool ReadStreamFileTracks(nw::snd::internal::StreamSoundFileReader& reader, Sound::StreamSoundInfo& soundInfo, sead::Heap* heap)
{
    u32 trackCount = reader.GetTrackCount();

    if (trackCount > cStrmTrackNum)
        trackCount = cStrmTrackNum;

    bool readAllTracks = true;

    for (u32 trackNo = 0; trackNo < trackCount; trackNo++)
    {
        Sound::StreamSoundInfo::Track* track = new(heap) Sound::StreamSoundInfo::Track();
        track->setId(trackNo);
        track->setDefaultName();

        nw::snd::internal::StreamSoundFileReader::TrackInfo trackInfo;

        if (reader.ReadStreamTrackInfo(&trackInfo, trackNo) && trackInfo.channelCount <= snd::cWaveChannelMax)
        {
            track->setFileValues(trackInfo.volume, trackInfo.pan, trackInfo.span, trackInfo.flags);

            for (u8 ch = 0; ch < trackInfo.channelCount; ch++)
                track->addChannel(trackInfo.globalChannelIndex[ch]);
        }
        else
        {
            sead::FormatFixedSafeString<128> msg(messages::stream::cTrackUnreadableFormat, trackNo);
            PopupMgr::instance()->pushCurrentItemWarning(msg, messages::stream::cAudioUnreadableDetail);

            readAllTracks = false;
        }

        soundInfo.getTrackList().pushBack(track);
    }

    return readAllTracks;
}

u32 GetDefaultStreamTrackCount(u32 channelCount)
{
    return (channelCount + snd::cWaveChannelMax - 1) / snd::cWaveChannelMax;
}

bool StreamWavesCoverLayout(const Sound::StreamSoundInfo& soundInfo)
{
    const Sound::StreamSoundInfo::Track::List& tracks = soundInfo.getTrackList();

    if (tracks.isEmpty())
        return false;

    for (s32 i = 0; i < tracks.size(); i++)
    {
        const Sound::StreamSoundInfo::Track& track = *static_cast<const Sound::StreamSoundInfo::Track*>(tracks.nth(i)->val());

        if (!track.getWaveFileRef().isAttached())
            return false;

        const WaveFile& wave = *static_cast<const WaveFile*>(track.getWaveFileRef().getItem());

        if (track.getChannels_().isEmpty() || wave.getChannels().size() != track.getChannels_().size())
            return false;
    }

    return true;
}

void ApplyStreamTrackLayout(Sound::StreamSoundInfo& soundInfo, u32 trackCount, u32 channelCount)
{
    Sound::StreamSoundInfo::Track::List& tracks = soundInfo.getTrackList();

    std::vector<Item*> removedTracks;

    while (static_cast<u32>(tracks.size()) > trackCount)
    {
        Item* last = tracks.nth(tracks.size() - 1)->val();

        tracks.erase(last);
        removedTracks.push_back(last);
    }

    ForgetRemovedItems(removedTracks);

    for (Item* track : removedTracks)
        delete track;

    while (static_cast<u32>(tracks.size()) < trackCount)
    {
        Sound::StreamSoundInfo::Track* track = new Sound::StreamSoundInfo::Track();
        track->setDefaultName();

        tracks.pushBack(track);
    }

    u32 nextChannel = 0;

    for (u32 trackNo = 0; trackNo < trackCount; trackNo++)
    {
        Sound::StreamSoundInfo::Track& track = *static_cast<Sound::StreamSoundInfo::Track*>(tracks.nth(trackNo)->val());

        track.setId(trackNo);
        track.clearChannels();

        const u32 trackChannelCount = channelCount / trackCount + (trackNo < channelCount % trackCount ? 1 : 0);

        for (u32 ch = 0; ch < trackChannelCount && ch < snd::cWaveChannelMax; ch++)
            track.addChannel(static_cast<u8>(nextChannel++));
    }
}
