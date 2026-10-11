#include <bfsar/files/StreamRegion.h>

#include <snd/snd_StreamSoundFileReader.h>

#include <prim/seadMemUtil.h>

using nw::snd::internal::StreamSoundFile;

static constexpr u32 cSamplesPerFrame = 14;
static constexpr u32 cBytesPerFrame   = 8;

static constexpr u32 cRegionLengthMinAdpcm = 28672;
static constexpr u32 cRegionLengthMinPcm16 = 8192;
static constexpr u32 cRegionLengthMinPcm8  = 16384;

static constexpr u32 cRegionUnknown68Default = 1;
static constexpr const char* cRegionDefaultName = "Region";

void StreamFileLayout::mixInto(StateHash& hash) const
{
    hash.mixValue<u32>(static_cast<u32>(regions.size()));
    hash.mixValue<u32>(regionRecordBytes);
    hash.mixValue<u32>(hasUnknown4C ? 1u : 0u);
    hash.mixValue<u32>(unknown4C);

    for (const StreamRegion& region : regions)
    {
        hash.mixValue<u32>(region.start);
        hash.mixValue<u32>(region.end);
        hash.mixValue<u32>(region.unknown68);
        hash.mixText(region.name);
    }
}

bool StreamFileLayout::canAddRegion() const
{
    return regions.size() < cRegionCountMax;
}

bool StreamFileLayout::canStoreRegionNames() const
{
    return regionRecordBytes != sizeof(StreamSoundFile::ConverterRegionInfo);
}

s32 StreamFileLayout::addRegion(s32 afterNo, u32 sampleCount)
{
    if (!canAddRegion() || sampleCount == 0)
        return -1;

    const s32 regionCount = static_cast<s32>(regions.size());
    const s32 regionNo = afterNo >= 0 && afterNo < regionCount ? afterNo + 1 : regionCount;
    const u32 previousEnd = regionNo > 0 ? regions[regionNo - 1].end : 0;

    StreamRegion region;
    region.start = previousEnd < sampleCount ? previousEnd : 0;
    region.end = sampleCount;
    region.unknown68 = cRegionUnknown68Default;

    for (const StreamRegion& other : regions)
    {
        if (other.start > region.start && other.start < region.end)
            region.end = other.start;
    }

    if (canStoreRegionNames())
        region.name.format("%s%d", cRegionDefaultName, regionNo);

    regions.insert(regions.begin() + regionNo, region);

    return regionNo;
}

void StreamFileLayout::removeRegion(s32 regionNo)
{
    if (regionNo < 0 || regionNo >= static_cast<s32>(regions.size()))
        return;

    regions.erase(regions.begin() + regionNo);
}

void StreamFileLayout::moveRegion(s32 fromNo, s32 toNo)
{
    const s32 regionCount = static_cast<s32>(regions.size());

    if (fromNo < 0 || fromNo >= regionCount || toNo < 0 || toNo >= regionCount || fromNo == toNo)
        return;

    const StreamRegion region = regions[fromNo];

    regions.erase(regions.begin() + fromNo);
    regions.insert(regions.begin() + toNo, region);
}

bool StreamFileLayout::setRegionRange(s32 regionNo, u32 start, u32 end, u32 sampleCount)
{
    if (regionNo < 0 || regionNo >= static_cast<s32>(regions.size()) || sampleCount == 0)
        return false;

    if (end > sampleCount)
        end = sampleCount;

    if (end < 1)
        end = 1;

    if (start >= end)
        start = end - 1;

    StreamRegion& region = regions[regionNo];

    if (region.start == start && region.end == end)
        return false;

    region.start = start;
    region.end = end;

    return true;
}

bool StreamFileLayout::setRegionName(s32 regionNo, const sead::SafeString& name)
{
    if (regionNo < 0 || regionNo >= static_cast<s32>(regions.size()))
        return false;

    if (!canStoreRegionNames() || !IsRegionNameValid(name) || regions[regionNo].name == name)
        return false;

    regions[regionNo].name = name;

    return true;
}

void StreamFileLayout::setRegions(std::vector<StreamRegion>&& newRegions)
{
    regions = std::move(newRegions);

    if (regions.size() > cRegionCountMax)
        regions.resize(cRegionCountMax);
}

u32 StreamFileLayout::removeRegionsAfter(u32 sampleCount)
{
    const size_t countBefore = regions.size();

    for (size_t i = regions.size(); i > 0; i--)
    {
        if (regions[i - 1].end > sampleCount)
            regions.erase(regions.begin() + (i - 1));
    }

    return static_cast<u32>(countBefore - regions.size());
}

bool StreamFileLayout::IsRegionNameValid(const sead::SafeString& name)
{
    const s32 length = name.calcLength();

    if (length > cRegionNameLengthMax)
        return false;

    for (s32 i = 0; i < length; i++)
    {
        const char c = name.cstr()[i];

        if (c < 0x20 || c > 0x7E)
            return false;
    }

    return true;
}

u32 StreamFileLayout::GetRegionLengthMin(bool isDspAdpcm, bool isPcm8)
{
    if (isDspAdpcm)
        return cRegionLengthMinAdpcm;

    return isPcm8 ? cRegionLengthMinPcm8 : cRegionLengthMinPcm16;
}

static void ReadRegionRecord(StreamRegion* region, const u8* record, u32 recordBytes)
{
    const StreamSoundFile::ConverterRegionInfo& range = *reinterpret_cast<const StreamSoundFile::ConverterRegionInfo*>(record);

    region->start = range.start;
    region->end = range.end;

    if (recordBytes != sizeof(StreamSoundFile::RegionInfo))
        return;

    const StreamSoundFile::RegionInfo& info = *reinterpret_cast<const StreamSoundFile::RegionInfo*>(record);

    char name[sizeof(info.name)];
    sead::MemUtil::copy(name, info.name, sizeof(name));
    name[sizeof(name) - 1] = '\0';

    region->unknown68 = info.unknown68;
    region->name.copy(name);
}

StreamRegionReadResult ReadStreamFileLayout(StreamFileLayout* layout, const nw::snd::internal::StreamSoundFileReader& reader, u32 strmFileSize)
{
    const StreamSoundFile::FileHeader& header = *reader.mHeader;
    const StreamSoundFile::StreamSoundInfo& info = *reader.mInfoBlockBody->GetStreamSoundInfo();

    layout->version = header.version;
    layout->hasUnknown4C = reader.GetStreamSoundInfoSize() >= sizeof(StreamSoundFile::StreamSoundInfo);
    layout->unknown4C = layout->hasUnknown4C ? static_cast<u32>(info.unknown4C) : 0;

    if (!header.HasRegionBlock() || info.regionCount == 0)
        return StreamRegionReadResult::None;

    const u64 blockOffset = header.GetRegionBlockOffset();
    const u64 blockEnd = blockOffset + header.GetRegionBlockSize();
    const u64 recordsOffset = reader.GetRegionDataOffset();

    if (blockEnd > strmFileSize || recordsOffset < blockOffset + sizeof(nw::ut::BinaryBlockHeader) || recordsOffset > blockEnd)
        return StreamRegionReadResult::Invalid;

    const u32 recordBytes = static_cast<u32>((blockEnd - recordsOffset) / info.regionCount);

    if (recordBytes != sizeof(StreamSoundFile::RegionInfo) && recordBytes != sizeof(StreamSoundFile::ConverterRegionInfo))
        return StreamRegionReadResult::Invalid;

    const u8* records = static_cast<const u8*>(sead::PtrUtil::addOffset(&header, recordsOffset));
    std::vector<StreamRegion> regions(info.regionCount);

    for (u32 i = 0; i < info.regionCount; i++)
    {
        StreamRegion& region = regions[i];

        ReadRegionRecord(&region, records + recordBytes * i, recordBytes);

        if (region.start >= region.end || region.end > info.frameCount)
            return StreamRegionReadResult::Invalid;
    }

    layout->regions = std::move(regions);
    layout->regionRecordBytes = recordBytes;

    return StreamRegionReadResult::Read;
}

static s16 DecodeSampleWithoutCoefs(const u8* data, u32 sample)
{
    const u8* frame = data + sample / cSamplesPerFrame * cBytesPerFrame;
    const u32 index = sample % cSamplesPerFrame;
    const u8 pair = frame[1 + index / 2];

    s32 nibble = index % 2 == 0 ? pair >> 4 : pair & 0xF;

    if (nibble >= 8)
        nibble -= 16;

    return static_cast<s16>(nibble * (1 << (frame[0] & 0xF)));
}

static void WriteRegionContext(sead::WriteStream* stream, const StreamRegionSampleData& sampleData, u32 channel, u32 start)
{
    u16 predScale = 0;
    s16 yn1 = 0;
    s16 yn2 = 0;

    const u64 frameEnd = (static_cast<u64>(start) / cSamplesPerFrame + 1) * cBytesPerFrame;

    if (sampleData.isDspAdpcm && channel < sampleData.channelCount && frameEnd <= sampleData.channelBytes)
    {
        const u8* data = sampleData.channels[channel];

        predScale = data[frameEnd - cBytesPerFrame];

        if (start >= 1)
            yn1 = DecodeSampleWithoutCoefs(data, start - 1);

        if (start >= 2)
            yn2 = DecodeSampleWithoutCoefs(data, start - 2);
    }

    stream->writeU16(predScale);
    stream->writeS16(yn1);
    stream->writeS16(yn2);
}

static void WriteRegionRecord(sead::WriteStream* stream, const StreamRegion& region, u32 recordBytes, const StreamRegionSampleData& sampleData)
{
    const bool isConverterRecord = recordBytes == sizeof(StreamSoundFile::ConverterRegionInfo);
    const u32 contextCount = isConverterRecord ? sizeof(StreamSoundFile::ConverterRegionInfo::adpcmContext) / sizeof(nw::snd::internal::DspAdpcmLoopParam)
                                               : sizeof(StreamSoundFile::RegionInfo::adpcmContext) / sizeof(nw::snd::internal::DspAdpcmLoopParam);

    stream->writeU32(region.start);
    stream->writeU32(region.end);

    for (u32 channel = 0; channel < contextCount; channel++)
        WriteRegionContext(stream, sampleData, channel, region.start);

    if (isConverterRecord)
    {
        const u8 tail[sizeof(StreamSoundFile::ConverterRegionInfo::padding) + sizeof(StreamSoundFile::ConverterRegionInfo::name)] = {};
        stream->writeMemBlock(tail, sizeof(tail));
        return;
    }

    const u8 padding[sizeof(StreamSoundFile::RegionInfo::padding)] = {};
    char name[sizeof(StreamSoundFile::RegionInfo::name)] = {};

    sead::MemUtil::copy(name, region.name.cstr(), region.name.calcLength());

    stream->writeU32(region.unknown68);
    stream->writeMemBlock(padding, sizeof(padding));
    stream->writeMemBlock(name, sizeof(name));
}

void WriteStreamRegions(sead::WriteStream* stream, const StreamFileLayout& layout, const StreamRegionSampleData& sampleData)
{
    for (const StreamRegion& region : layout.regions)
        WriteRegionRecord(stream, region, layout.regionRecordBytes, sampleData);
}
