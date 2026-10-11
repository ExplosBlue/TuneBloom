#include <bfsar/files/WaveFile.h>

#include <bfsar/audio/WavWriter.h>
#include <bfsar/io/FileIo.h>

#include <core/Debug.h>

#include <snd/SoundThread.h>

#include <ui/Messages.h>
#include <ui/app/PopupMgr.h>

#include <filedevice/seadFileDeviceMgr.h>
#include <stream/seadFileDeviceStream.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static constexpr u16 cRiffFormatPcm         = 1;
static constexpr u16 cRiffFormatFloat       = 3;
static constexpr u16 cRiffFormatExtensible  = 0xFFFE;
static constexpr u32 cRiffFmtSize           = 16;
static constexpr u32 cRiffFmtExtensibleSize = 26;
static constexpr u32 cRiffSmplSize          = 36;
static constexpr u32 cRiffSmplLoopSize      = 24;
static constexpr u32 cRiffFieldSize         = 4;
static constexpr u32 cRiffChunkHeaderSize   = 8;
static constexpr u32 cRiffCuePointSize      = 24;
static constexpr u32 cRiffLabeledTextSize   = 20;
static constexpr u32 cRiffLabelLengthMax    = 255;

static constexpr const char* cRiffConverterLabel       = "[NW]";
static constexpr size_t      cRiffConverterLabelLength = 4;

namespace
{

struct RiffReadContext
{
    sead::FileHandle& handle;
    sead::FileDeviceReadStream& stream;
    WaveFile::RiffWaveInfo* out;
    u32 actualFileSize;
    bool acceptAnyPcm;
};

struct RiffChunkHeader
{
    char id[4];
    u32 size;
    u64 start;

    bool is(const char* chunkId) const
    {
        return sead::MemUtil::compare(id, chunkId, 4) == 0;
    }

    u32 getPaddedSize() const
    {
        return size + (size & 1);
    }
};

struct RiffCuePoint
{
    u32 id = 0;
    u32 position = 0;
    u32 length = 0;
    std::string label;
};

}

static bool ReadRiffHeader(RiffReadContext& context)
{
    char riff[4];
    context.stream.readMemBlock(riff, 4);

    if (sead::MemUtil::compare(riff, "RIFF", 4) != 0)
    {
        PopupMgr::instance()->addPopup({ messages::wavefile::cWavInvalidRiff });
        return false;
    }

    context.out->fileSize = context.stream.readU32();

    char wave[4];
    context.stream.readMemBlock(wave, 4);

    if (sead::MemUtil::compare(wave, "WAVE", 4) != 0)
    {
        PopupMgr::instance()->addPopup({ messages::wavefile::cWavInvalidWave });
        return false;
    }

    return true;
}

static bool ReadRiffChunkHeader(RiffReadContext& context, RiffChunkHeader* chunk)
{
    context.stream.readMemBlock(chunk->id, 4);
    chunk->size = context.stream.readU32();
    chunk->start = context.handle.getCurrentSeekPos();

    if (chunk->start + static_cast<u64>(chunk->size) > static_cast<u64>(context.actualFileSize))
    {
        PopupMgr::instance()->addPopup({ messages::wavefile::cWavCorruptChunk });
        return false;
    }

    return true;
}

static bool FindRiffFmtChunk(RiffReadContext& context, u32* fmtChunkSize)
{
    for (;;)
    {
        if (context.stream.isEOF())
        {
            PopupMgr::instance()->addPopup({ messages::wavefile::cWavInvalidFmt });
            return false;
        }

        RiffChunkHeader chunk;
        if (!ReadRiffChunkHeader(context, &chunk))
            return false;

        if (chunk.is("fmt "))
        {
            *fmtChunkSize = chunk.size;
            break;
        }

        context.stream.skip(chunk.getPaddedSize());
    }

    if (*fmtChunkSize < cRiffFmtSize)
    {
        PopupMgr::instance()->addPopup({ messages::wavefile::cWavInvalidFmt });
        return false;
    }

    return true;
}

static bool ReadRiffFmtChunk(RiffReadContext& context, u32 fmtChunkSize)
{
    sead::FileDeviceReadStream& stream = context.stream;
    WaveFile::RiffWaveInfo* out = context.out;

    out->chunkSize = fmtChunkSize;

    out->format = stream.readU16();
    out->numChannels = stream.readU16();
    out->sampleRate = stream.readU32();
    out->byteRate = stream.readU32();
    out->blockAlign = stream.readU16();

    out->bitsPerSample = stream.readU16();

    u32 fmtConsumed = cRiffFmtSize;

    if (out->format == cRiffFormatExtensible && fmtChunkSize >= cRiffFmtExtensibleSize)
    {
        stream.skip(8);
        out->format = stream.readU16();
        fmtConsumed = cRiffFmtExtensibleSize;
    }

    const bool isFloat = context.acceptAnyPcm && out->format == cRiffFormatFloat;

    if (out->format != cRiffFormatPcm && !isFloat)
    {
        PopupMgr::instance()->addPopup({ messages::wavefile::cWavOnlyPcm });
        return false;
    }

    if (out->numChannels == 0 || (out->numChannels > snd::cWaveChannelMax && !context.acceptAnyPcm))
    {
        PopupMgr::instance()->addPopup({ messages::wavefile::cWavOnlyMonoStereo });
        return false;
    }

    if (out->sampleRate == 0)
    {
        PopupMgr::instance()->addPopup({ messages::wavefile::cWavInvalidFmt });
        return false;
    }

    LOG_U16("format", out->format);
    LOG_U16("numChannels", out->numChannels);
    LOG_U32("sampleRate", out->sampleRate);
    LOG_U32("byteRate", out->byteRate);
    LOG_U16("blockAlign", out->blockAlign);
    LOG_U16("bitsPerSample", out->bitsPerSample);

    bool validBits = out->bitsPerSample == 8 || out->bitsPerSample == 16;

    if (isFloat)
        validBits = out->bitsPerSample == 32;
    else if (context.acceptAnyPcm)
        validBits = validBits || out->bitsPerSample == 24 || out->bitsPerSample == 32;

    if (!validBits)
    {
        PopupMgr::instance()->addPopup({ context.acceptAnyPcm ? messages::wavefile::cWavOnlyPcmBits : messages::wavefile::cWavOnlyPcm8Pcm16 });
        return false;
    }

    u32 fmtRemaining = fmtChunkSize > fmtConsumed ? fmtChunkSize - fmtConsumed : 0;
    stream.skip(fmtRemaining + (fmtChunkSize & 1));

    return true;
}

static void ReadRiffSmplChunk(RiffReadContext& context, const RiffChunkHeader& chunk)
{
    sead::FileDeviceReadStream& stream = context.stream;

    if (chunk.size < cRiffSmplSize)
    {
        stream.skip(chunk.getPaddedSize());
        return;
    }

    stream.skip(28);
    u32 numLoops = stream.readU32();
    stream.skip(4);

    u32 consumed = cRiffSmplSize;

    if (numLoops > 0 && chunk.size >= cRiffSmplSize + cRiffSmplLoopSize)
    {
        stream.skip(8);
        u32 loopStart = stream.readU32();
        u32 loopEnd = stream.readU32();
        stream.skip(8);

        context.out->isLoop = true;
        context.out->loopStartFrame = loopStart;
        context.out->loopEndFrame = loopEnd;

        LOG_U32("smpl loopStart", loopStart);
        LOG_U32("smpl loopEnd", loopEnd);

        consumed += cRiffSmplLoopSize;
    }

    u32 remaining = chunk.size > consumed ? chunk.size - consumed : 0;
    stream.skip(remaining + (chunk.size & 1));
}

static bool ReadRiffDataChunks(RiffReadContext& context)
{
    while (!context.stream.isEOF())
    {
        RiffChunkHeader chunk;
        if (!ReadRiffChunkHeader(context, &chunk))
            return false;

        if (chunk.is("data"))
        {
            context.out->sampleBytes = chunk.size;
            context.out->dataStart = static_cast<u32>(chunk.start);
            context.stream.skip(chunk.getPaddedSize());
        }
        else if (chunk.is("smpl"))
        {
            ReadRiffSmplChunk(context, chunk);
        }
        else
        {
            context.stream.skip(chunk.getPaddedSize());
        }
    }

    return true;
}

static bool FinishRiffInfo(WaveFile::RiffWaveInfo* out)
{
    if (out->sampleBytes == 0)
    {
        PopupMgr::instance()->addPopup({ messages::wavefile::cWavNoData });
        return false;
    }

    LOG_U32("sampleBytes", out->sampleBytes);
    LOG_U32("dataStart", out->dataStart);

    out->sampleFormat = out->bitsPerSample == 8 ? snd::SampleFormat::PcmS8 : snd::SampleFormat::PcmS16;
    out->sampleCount = out->sampleBytes / out->numChannels / (out->bitsPerSample / 8);

    LOG_U32("sampleCount", out->sampleCount);

    if (!out->isLoop)
    {
        out->loopStartFrame = 0;
        out->loopEndFrame = out->sampleCount;
    }

    out->isValid = true;

    return true;
}

bool WaveFile::readRiffWavInfo(RiffWaveInfo* out, bool acceptAnyPcm)
{
    LOG_FUNC();
    SEAD_ASSERT(out);
    out->isValid = false;

    sead::FileDevice* device = sead::FileDeviceMgr::instance()->findDevice("native");
    SEAD_ASSERT(device);

    sead::FileHandle handle;
    device->tryOpen(&handle, out->path, sead::FileDevice::FileOpenFlag::eReadOnly, 0);

    if (!handle.getDevice())
    {
        sead::FormatFixedSafeString<512> msg(messages::wavefile::cWavOpenFailedFormat,out->path.cstr());
        PopupMgr::instance()->addPopup({ msg });
        return false;
    }

    out->endian = sead::Endian::eLittle;

    sead::FileDeviceReadStream stream(&handle, sead::Stream::Modes::eBinary);
    stream.setBinaryEndian(out->endian);

    RiffReadContext context = { handle, stream, out, device->getFileSize(&handle), acceptAnyPcm };

    u32 fmtChunkSize = 0;

    return ReadRiffHeader(context) &&
           FindRiffFmtChunk(context, &fmtChunkSize) &&
           ReadRiffFmtChunk(context, fmtChunkSize) &&
           ReadRiffDataChunks(context) &&
           FinishRiffInfo(out);
}

static RiffCuePoint* FindRiffCuePoint(std::vector<RiffCuePoint>& cues, u32 id)
{
    for (RiffCuePoint& cue : cues)
    {
        if (cue.id == id)
            return &cue;
    }

    return nullptr;
}

static void ReadRiffCueChunk(RiffReadContext& context, const RiffChunkHeader& chunk, std::vector<RiffCuePoint>* cues)
{
    sead::FileDeviceReadStream& stream = context.stream;

    if (chunk.size < cRiffFieldSize)
    {
        stream.skip(chunk.getPaddedSize());
        return;
    }

    const u32 pointMax = (chunk.size - cRiffFieldSize) / cRiffCuePointSize;
    const u32 pointCount = sead::Mathu::min(stream.readU32(), pointMax);

    for (u32 i = 0; i < pointCount; i++)
    {
        RiffCuePoint cue;
        cue.id = stream.readU32();
        cue.position = stream.readU32();
        stream.skip(cRiffCuePointSize - cRiffFieldSize * 2);

        cues->push_back(cue);
    }

    stream.skip(chunk.getPaddedSize() - cRiffFieldSize - pointCount * cRiffCuePointSize);
}

static void ReadRiffLabelChunk(RiffReadContext& context, u32 size, std::vector<RiffCuePoint>* cues)
{
    sead::FileDeviceReadStream& stream = context.stream;
    RiffCuePoint* cue = FindRiffCuePoint(*cues, stream.readU32());

    char text[cRiffLabelLengthMax + 1] = {};
    const u32 textBytes = sead::Mathu::min(size - cRiffFieldSize, cRiffLabelLengthMax);

    stream.readMemBlock(text, textBytes);
    stream.skip(size - cRiffFieldSize - textBytes);

    if (cue)
        cue->label = text;
}

static void ReadRiffLabeledTextChunk(RiffReadContext& context, u32 size, std::vector<RiffCuePoint>* cues)
{
    sead::FileDeviceReadStream& stream = context.stream;
    RiffCuePoint* cue = FindRiffCuePoint(*cues, stream.readU32());
    const u32 sampleLength = stream.readU32();

    char purpose[cRiffFieldSize];
    stream.readMemBlock(purpose, sizeof(purpose));
    stream.skip(size - cRiffFieldSize * 3);

    if (cue && sead::MemUtil::compare(purpose, "rgn ", sizeof(purpose)) == 0)
        cue->length = sampleLength;
}

static void ReadRiffAssociatedDataList(RiffReadContext& context, const RiffChunkHeader& chunk, std::vector<RiffCuePoint>* cues)
{
    sead::FileDeviceReadStream& stream = context.stream;

    char type[cRiffFieldSize] = {};
    u32 consumed = 0;

    if (chunk.size >= cRiffFieldSize)
    {
        stream.readMemBlock(type, sizeof(type));
        consumed = cRiffFieldSize;
    }

    const bool isAssociatedData = sead::MemUtil::compare(type, "adtl", sizeof(type)) == 0;

    while (isAssociatedData && chunk.size - consumed >= cRiffChunkHeaderSize)
    {
        char id[cRiffFieldSize];
        stream.readMemBlock(id, sizeof(id));

        const u32 size = stream.readU32();
        const u32 paddedSize = size + (size & 1);

        consumed += cRiffChunkHeaderSize;

        if (size > chunk.size - consumed)
            break;

        if (sead::MemUtil::compare(id, "labl", sizeof(id)) == 0 && size >= cRiffFieldSize)
            ReadRiffLabelChunk(context, size, cues);
        else if (sead::MemUtil::compare(id, "ltxt", sizeof(id)) == 0 && size >= cRiffLabeledTextSize)
            ReadRiffLabeledTextChunk(context, size, cues);
        else
            stream.skip(size);

        stream.skip(paddedSize - size);
        consumed += paddedSize;
    }

    if (consumed < chunk.getPaddedSize())
        stream.skip(chunk.getPaddedSize() - consumed);
}

static void SetWavRegionName(StreamRegion* region, const char* label)
{
    char name[StreamFileLayout::cRegionNameLengthMax + 1] = {};

    for (s32 i = 0; i < StreamFileLayout::cRegionNameLengthMax && label[i] != '\0'; i++)
        name[i] = label[i] >= 0x20 && label[i] <= 0x7E ? label[i] : '_';

    region->name = name;
}

static bool MakeConverterWavRegions(std::vector<StreamRegion>* regions, const std::vector<RiffCuePoint>& cues, const std::vector<StreamRegion>& ranges)
{
    static constexpr s32 cSlotNum = StreamFileLayout::cRegionCountMax + 1;

    s32 cueOfSlot[cSlotNum];
    std::fill(cueOfSlot, cueOfSlot + cSlotNum, -1);

    for (s32 pass = 0; pass < 2; pass++)
    {
        for (size_t i = 0; i < cues.size(); i++)
        {
            const char* label = cues[i].label.c_str();

            if (ranges[i].end == 0 || strncmp(label, cRiffConverterLabel, cRiffConverterLabelLength) != 0)
                continue;

            const char* number = label + cRiffConverterLabelLength;
            const bool hasNumber = number[0] == '#';

            if (hasNumber != (pass == 0))
                continue;

            s32 slot = 0;

            if (hasNumber)
            {
                char* numberEnd = nullptr;
                const long value = strtol(number + 1, &numberEnd, 10);

                if (numberEnd == number + 1 || *numberEnd != '\0' || value < 0 || value >= cSlotNum || cueOfSlot[value] != -1)
                    return false;

                slot = static_cast<s32>(value);
            }
            else
            {
                while (slot < cSlotNum && cueOfSlot[slot] != -1)
                    slot++;

                if (slot == cSlotNum)
                    return false;
            }

            cueOfSlot[slot] = static_cast<s32>(i);
        }
    }

    for (s32 slot = 0; slot < cSlotNum; slot++)
    {
        if (cueOfSlot[slot] != -1)
            regions->push_back(ranges[cueOfSlot[slot]]);
    }

    return true;
}

static bool MakeWavRegions(std::vector<StreamRegion>* regions, const std::vector<RiffCuePoint>& cues, u32 sampleCount)
{
    std::vector<StreamRegion> ranges(cues.size());
    bool hasConverterLabel = false;

    for (size_t i = 0; i < cues.size(); i++)
    {
        const RiffCuePoint& cue = cues[i];

        if (cue.length == 0 || cue.position >= sampleCount)
            continue;

        ranges[i].start = cue.position;
        ranges[i].end = static_cast<u32>(std::min<u64>(static_cast<u64>(cue.position) + cue.length, sampleCount));
        ranges[i].unknown68 = 1;

        const bool isConverterLabel = strncmp(cue.label.c_str(), cRiffConverterLabel, cRiffConverterLabelLength) == 0;

        if (!isConverterLabel)
            SetWavRegionName(&ranges[i], cue.label.c_str());

        hasConverterLabel |= isConverterLabel;
    }

    if (hasConverterLabel)
        return MakeConverterWavRegions(regions, cues, ranges);

    for (const StreamRegion& range : ranges)
    {
        if (range.end != 0)
            regions->push_back(range);
    }

    if (regions->size() > StreamFileLayout::cRegionCountMax)
        regions->resize(StreamFileLayout::cRegionCountMax);

    return true;
}

bool ReadWavRegions(WavRegions* out, const sead::SafeString& path)
{
    WaveFile::RiffWaveInfo info;
    info.path = path;

    sead::FileDevice* device = sead::FileDeviceMgr::instance()->findDevice("native");
    SEAD_ASSERT(device);

    sead::FileHandle handle;
    device->tryOpen(&handle, info.path, sead::FileDevice::FileOpenFlag::eReadOnly, 0);

    if (!handle.getDevice())
    {
        sead::FormatFixedSafeString<512> msg(messages::wavefile::cWavOpenFailedFormat, info.path.cstr());
        PopupMgr::instance()->addPopup({ msg });
        return false;
    }

    sead::FileDeviceReadStream stream(&handle, sead::Stream::Modes::eBinary);
    stream.setBinaryEndian(sead::Endian::eLittle);

    RiffReadContext context = { handle, stream, &info, device->getFileSize(&handle), true };
    std::vector<RiffCuePoint> cues;
    u32 fmtChunkSize = 0;

    if (!ReadRiffHeader(context) || !FindRiffFmtChunk(context, &fmtChunkSize) || !ReadRiffFmtChunk(context, fmtChunkSize))
        return false;

    while (!stream.isEOF())
    {
        RiffChunkHeader chunk;
        if (!ReadRiffChunkHeader(context, &chunk))
            return false;

        if (chunk.is("data"))
        {
            info.sampleBytes = chunk.size;
            stream.skip(chunk.getPaddedSize());
        }
        else if (chunk.is("cue "))
        {
            ReadRiffCueChunk(context, chunk, &cues);
        }
        else if (chunk.is("LIST"))
        {
            ReadRiffAssociatedDataList(context, chunk, &cues);
        }
        else
        {
            stream.skip(chunk.getPaddedSize());
        }
    }

    out->sampleRate = info.sampleRate;
    out->sampleCount = info.blockAlign != 0 ? info.sampleBytes / info.blockAlign : 0;
    out->regions.clear();

    if (MakeWavRegions(&out->regions, cues, out->sampleCount))
        return true;

    sead::FormatFixedSafeString<512> msg(messages::stream::cRegionImportInvalidFormat, info.path.cstr());
    PopupMgr::instance()->addPopup({ msg });

    return false;
}

static u32 CalcRiffLabelSize(const StreamRegion& region)
{
    return cRiffFieldSize + region.name.calcLength() + 1;
}

u32 CalcWavRegionsSize(const std::vector<StreamRegion>& regions)
{
    if (regions.empty())
        return 0;

    u32 size = cRiffChunkHeaderSize + cRiffFieldSize + cRiffCuePointSize * static_cast<u32>(regions.size()) + cRiffChunkHeaderSize + cRiffFieldSize;

    for (const StreamRegion& region : regions)
    {
        const u32 labelSize = CalcRiffLabelSize(region);

        size += cRiffChunkHeaderSize + cRiffLabeledTextSize + cRiffChunkHeaderSize + labelSize + (labelSize & 1);
    }

    return size;
}

void WriteWavRegions(sead::WriteStream* stream, const std::vector<StreamRegion>& regions)
{
    if (regions.empty())
        return;

    const u32 regionCount = static_cast<u32>(regions.size());
    const u32 cueSize = cRiffFieldSize + cRiffCuePointSize * regionCount;

    stream->writeMemBlock("cue ", cRiffFieldSize);
    stream->writeU32(cueSize);
    stream->writeU32(regionCount);

    for (u32 i = 0; i < regionCount; i++)
    {
        stream->writeU32(i + 1);
        stream->writeU32(regions[i].start);
        stream->writeMemBlock("data", cRiffFieldSize);
        stream->writeU32(0);
        stream->writeU32(0);
        stream->writeU32(regions[i].start);
    }

    stream->writeMemBlock("LIST", cRiffFieldSize);
    stream->writeU32(CalcWavRegionsSize(regions) - cRiffChunkHeaderSize - cueSize - cRiffChunkHeaderSize);
    stream->writeMemBlock("adtl", cRiffFieldSize);

    for (u32 i = 0; i < regionCount; i++)
    {
        const StreamRegion& region = regions[i];
        const u32 labelSize = CalcRiffLabelSize(region);
        const u8 zeros[cRiffLabeledTextSize - cRiffFieldSize * 3] = {};

        stream->writeMemBlock("ltxt", cRiffFieldSize);
        stream->writeU32(cRiffLabeledTextSize);
        stream->writeU32(i + 1);
        stream->writeU32(region.end - region.start);
        stream->writeMemBlock("rgn ", cRiffFieldSize);
        stream->writeMemBlock(zeros, sizeof(zeros));

        stream->writeMemBlock("labl", cRiffFieldSize);
        stream->writeU32(labelSize);
        stream->writeU32(i + 1);
        stream->writeMemBlock(region.name.cstr(), labelSize - cRiffFieldSize);

        if (labelSize & 1)
            stream->writeU8(0);
    }
}

static void ReadWavChannels(sead::ReadStream& stream, const WaveFile::RiffWaveInfo& info, u8** channels)
{
    u8* samples = new u8[info.sampleBytes];
    stream.readMemBlock(samples, info.sampleBytes);

    if (info.numChannels == 1)
    {
        if (info.sampleFormat == snd::SampleFormat::PcmS8)
        {
            for (u32 i = 0; i < info.sampleCount; i++)
            {
                samples[i] = samples[i] - 128;
            }
        }

        channels[0] = samples;
        return;
    }

    for (u32 i = 0; i < info.numChannels; i++)
    {
        channels[i] = new u8[info.sampleBytes / info.numChannels];
    }

    for (u32 i = 0; i < info.sampleCount; i++)
    {
        for (u32 ch = 0; ch < info.numChannels; ch++)
        {
            if (info.sampleFormat == snd::SampleFormat::PcmS8)
            {
                channels[ch][i] = samples[i * info.numChannels + ch] - 128;
            }
            else if (info.sampleFormat == snd::SampleFormat::PcmS16)
            {
                s16* channel = reinterpret_cast<s16*>(channels[ch]);
                const s16* samples16 = reinterpret_cast<const s16*>(samples);

                channel[i] = samples16[i * info.numChannels + ch];
            }
        }
    }

    delete[] samples;
}

bool WaveFile::readWavFile(const RiffWaveInfo& info, Encoding encoding)
{
    LOG_FUNC();
    LOG_STR("path");
    LOG_STR(info.path.cstr());
    LOG_U32("encoding", (u32)encoding);
    LOG_U32("sampleRate", info.sampleRate);
    LOG_U32("sampleCount", info.sampleCount);
    LOG_U16("numChannels", info.numChannels);
    LOG_U16("bitsPerSample", info.bitsPerSample);

    if (!info.isValid)
    {
        PopupMgr::instance()->addPopup({ messages::wavefile::cWavInvalidInfo });
        return false;
    }

    sead::FileDevice* device = sead::FileDeviceMgr::instance()->findDevice("native");
    SEAD_ASSERT(device);

    sead::FileHandle handle;
    device->tryOpen(&handle, info.path, sead::FileDevice::FileOpenFlag::eReadOnly, 0);

    if (!handle.getDevice())
    {
        sead::FormatFixedSafeString<512> msg(messages::wavefile::cWavOpenFailedFormat,info.path.cstr());
        PopupMgr::instance()->addPopup({ msg });
        return false;
    }

    sead::FileDeviceReadStream stream(&handle, sead::Stream::Modes::eBinary);
    stream.setBinaryEndian(info.endian);
    stream.rewind();
    stream.skip(info.dataStart);

    mIsLoopDirty = true;

    {
        snd::internal::driver::SoundThreadLock lock;

        mChannels.clear();
    }

    invalidateOriginalData_();

    u8* channels[snd::cWaveChannelMax] = { nullptr, nullptr };
    ReadWavChannels(stream, info, channels);

    LOG_U32("numChannels", info.numChannels);

    mDataEndian = info.endian;
    mEncoding = encoding;
    mSampleRate = info.sampleRate;
    mSampleCount = info.sampleCount;

    mIsLoop = info.isLoop;
    mLoopStartFrame = info.loopStartFrame;
    mLoopEndFrame = info.loopEndFrame;

    for (u32 i = 0; i < info.numChannels; i++)
    {
        Channel* channel = mChannels.birthBack();
        SEAD_ASSERT(channel);

        u32 dataSize = 0;
        u32 dataSizeMin = 0;
        channel->mOwnsData = true;
        const Encoding sourceEncoding = info.sampleFormat == snd::SampleFormat::PcmS8 ? Encoding::Pcm8 : Encoding::Pcm16;
        channel->mData = convertOwnChannel_(*channel, channels[i], info.endian, sourceEncoding, &dataSize, &dataSizeMin);

        if (channel->mData != channels[i])
        {
            delete[] channels[i];
        }

        if (dataSize == 0)
        {
            dataSize = info.sampleBytes / info.numChannels;
        }

        channel->mDataSize = dataSize;
        channel->mDataSizeMin = dataSizeMin;
    }

    mIsLoopDirty = false;
    mIsStreamExtended = true;

    refreshSerializedHash();

    return true;
}

static const WaveFile::Channel* GetWavExportChannel(const WaveFile& wave, s32 channelIdx, u32 ch)
{
    return wave.getChannels().nth(channelIdx == -1 ? ch : channelIdx);
}

static void WriteWavPcm8(sead::WriteStream& stream, const WaveFile& wave, s32 channelIdx, u32 numChannels)
{
    const u32 sampleCount = wave.getSampleCount();
    u8* interleavedSamples = new u8[sampleCount * numChannels];

    for (u32 ch = 0; ch < numChannels; ch++)
    {
        const s8* data = static_cast<const s8*>(GetWavExportChannel(wave, channelIdx, ch)->getData());

        for (u32 i = 0; i < sampleCount; i++)
        {
            interleavedSamples[i * numChannels + ch] = data[i] + 128;
        }
    }

    stream.writeMemBlock(interleavedSamples, sampleCount * numChannels * sizeof(u8));

    delete[] interleavedSamples;
}

static void WriteWavPcm16(sead::WriteStream& stream, const WaveFile& wave, s32 channelIdx, u32 numChannels, sead::Endian::Types endian)
{
    const u32 sampleCount = wave.getSampleCount();
    s16* interleavedSamples = new s16[sampleCount * numChannels];

    for (u32 ch = 0; ch < numChannels; ch++)
    {
        const s16* data = static_cast<const s16*>(GetWavExportChannel(wave, channelIdx, ch)->getData());

        for (u32 i = 0; i < sampleCount; i++)
        {
            interleavedSamples[i * numChannels + ch] = sead::Endian::convertS16(wave.getDataEndian(), endian, data[i]);
        }
    }

    stream.writeMemBlock(interleavedSamples, sampleCount * numChannels * sizeof(s16));

    delete[] interleavedSamples;
}

static void WriteWavDspAdpcm(sead::WriteStream& stream, const WaveFile& wave, s32 channelIdx, u32 numChannels)
{
    const u32 sampleCount = wave.getSampleCount();
    s16* interleavedSamples = new s16[sampleCount * numChannels];
    s16* data = new s16[sampleCount];

    for (u32 ch = 0; ch < numChannels; ch++)
    {
        const WaveFile::Channel* channel = GetWavExportChannel(wave, channelIdx, ch);

        ADPCMINFO adpcmInfo;
        sead::MemUtil::fillZero(&adpcmInfo, sizeof(adpcmInfo));

        FillAdpcmInfo(&adpcmInfo, channel->getAdpcmParam(false), channel->getAdpcmLoopParam(false));

        decode(static_cast<u8*>(const_cast<void*>(channel->getData())), data, &adpcmInfo, sampleCount);

        for (u32 i = 0; i < sampleCount; i++)
        {
            interleavedSamples[i * numChannels + ch] = data[i];
        }
    }

    stream.writeMemBlock(interleavedSamples, sampleCount * numChannels * sizeof(s16));

    delete[] data;
    delete[] interleavedSamples;
}

bool WaveFile::writeWavFile(const sead::SafeString& path, s32 channelIdx)
{
    LOG_FUNC();
    LOG_STR(path.cstr());
    LOG_S32("channelIdx", channelIdx);
    LOG_U32("sampleRate", mSampleRate);
    LOG_U32("sampleCount", mSampleCount);
    LOG_U32("encoding", static_cast<u32>(mEncoding));
    LOG_U32("channels", mChannels.size());

    fileio::StagedFile staged;

    if (!staged.open(path))
        return false;

    sead::FileHandle& handle = *staged.getHandle();
    sead::Endian::Types endian = sead::Endian::eLittle;

    CheckedFileStreamSrc streamSrc(&handle);
    sead::WriteStream stream(&streamSrc, sead::Stream::Modes::eBinary);
    stream.setBinaryEndian(endian);

    stream.writeString("RIFF", 4);
    stream.writeU32(0); // File Size, updated at the end
    stream.writeString("WAVE", 4);

    SEAD_ASSERT(channelIdx < static_cast<s32>(snd::cWaveChannelMax));

    u32 numChannels = channelIdx == -1 ? mChannels.size() : 1;
    u32 bitsPerSample = mEncoding == WaveFile::Encoding::Pcm8 ? 8 : 16;

    stream.writeString("fmt ", 4);
    stream.writeU32(0x10); // Chunk Size
    stream.writeU16(1); // Format (1 = Pcm)
    stream.writeU16(numChannels);
    stream.writeU32(mSampleRate);
    stream.writeU32(mSampleRate * numChannels * (bitsPerSample / 8)); // Byte Rate
    stream.writeU16(numChannels * (bitsPerSample / 8)); // Block Align
    stream.writeU16(bitsPerSample);

    stream.writeString("data", 4);
    stream.writeU32(mSampleCount * numChannels * (bitsPerSample / 8)); // Chunk Size

    switch (mEncoding)
    {
        case WaveFile::Encoding::Pcm8:
            WriteWavPcm8(stream, *this, channelIdx, numChannels);
            break;

        case WaveFile::Encoding::Pcm16:
            WriteWavPcm16(stream, *this, channelIdx, numChannels, endian);
            break;

        case WaveFile::Encoding::DspAdpcm:
            WriteWavDspAdpcm(stream, *this, channelIdx, numChannels);
            break;

        default:
            SEAD_ASSERT_MSG(false, "Invalid Encoding");
            break;
    }

    if (mSampleCount * numChannels * (bitsPerSample / 8) & 1)
        stream.writeU8(0);

    // Write loop chunk
    WavLoop loop;
    loop.start = getOriginalLoopStartFrame();
    loop.end = std::min(getOriginalLoopEndFrame(), mSampleCount);
    loop.enabled = getIsLoop() && loop.end > loop.start;

    if (loop.enabled)
        WriteWavLoop(&stream, loop);

    // Update File Size
    u32 fileSize = handle.getCurrentSeekPos() - 8;
    if (!handle.trySeek(4, sead::FileDevice::SeekOrigin::eBegin))
        return false;

    stream.writeU32(fileSize);

    return !streamSrc.hasFailed() && staged.commit();
}
