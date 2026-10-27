#include <aal/Amta.h>

#include <core/ByteBuilder.h>
#include <core/ByteView.h>

#include <codec/seadHashCRC32.h>
#include <math/seadMathCalcCommon.h>

#include <cstring>

namespace aal
{

static constexpr u32 cHeaderByteOrder      = 0x04;
static constexpr u32 cHeaderVersion        = 0x06;
static constexpr u32 cHeaderFileSize       = 0x08;
static constexpr u32 cHeaderDataOffset     = 0x0C;
static constexpr u32 cHeaderMarkOffset     = 0x10;
static constexpr u32 cHeaderExtOffset      = 0x14;
static constexpr u32 cHeaderStringsV1      = 0x14;
static constexpr u32 cHeaderStringsV3      = 0x18;
static constexpr u32 cHeaderMinSize        = 0x1C;
static constexpr u32 cHeaderNamePointerV5  = 0x24;
static constexpr u32 cHeaderNameHashV5     = 0x28;
static constexpr u32 cHeaderMinSizeV5      = 0x34;
static constexpr u32 cHeaderFlagsV5        = 0x2C;
static constexpr u32 cHeaderChannelsV5     = 0x30;
static constexpr u32 cHeaderInfoOffsetV5   = 0x10;
static constexpr u32 cHeaderOffsetFirstV5  = 0x0C;
static constexpr u32 cHeaderOffsetLastV5   = 0x20;
static constexpr u32 cInfoPeak             = 0x04;
static constexpr u32 cInfoRms300           = 0x08;
static constexpr u32 cInfoMomentaryMax     = 0x0C;
static constexpr u32 cInfoIntegrated       = 0x10;
static constexpr u32 cInfoEnvelopeCount    = 0x14;
static constexpr u32 cInfoLoudnessRange    = 0x18;
static constexpr u32 cInfoFixedSize        = 0x1C;
static constexpr u32 cEnvelopePointSize    = 0x08;
static constexpr u8 cFlagsLoopV5           = 0x04;
static constexpr u8 cFlagsSpreadV5         = 0x80;
static constexpr u8 cChannelTableVersion   = 0x01;
static constexpr u8 cMonoPosition          = 0x04;
static constexpr u32 cDataRms300           = 0x0C;
static constexpr u32 cDataLoudness         = 0x1C;
static constexpr u32 cDataPeak             = 0x60;
static constexpr u32 cSectionHeaderSize    = 0x08;
static constexpr u32 cSectionBodySize      = 0x04;
static constexpr u32 cDataName             = 0x00;
static constexpr u32 cDataStoredSamples    = 0x04;
static constexpr u32 cDataChannelCount     = 0x09;
static constexpr u32 cDataFlags            = 0x0B;
static constexpr u32 cDataSampleRate       = 0x10;
static constexpr u32 cDataLoopStart        = 0x14;
static constexpr u32 cDataSampleCount      = 0x18;
static constexpr u32 cDataMinSize          = 0x1C;
static constexpr u32 cMarkEntrySize        = 0x10;
static constexpr u32 cMarkEntryName        = 0x04;
static constexpr u32 cExtEntrySize         = 0x08;
static constexpr u32 cExtEntryName         = 0x00;
static constexpr u32 cEntryListStart       = 0x04;
static constexpr u32 cFileAlignment        = 4;
static constexpr u8 cDataLoopFlag          = 0x04;
static constexpr u32 cVersion3             = 0x0300;

static constexpr char cAmtaMagic[] = "AMTA";

struct Amta5Parts
{
    bool isBigEndian = false;
    std::vector<u8> header;
    std::vector<u8> channelTable;
    std::vector<u8> info;
    std::vector<u8> envelope;
    std::vector<u8> tail;
    u32 infoOffset = 0;
    u32 tailOffset = 0;
    u32 nameOffset = 0;
};

struct StringTableLayout
{
    u32 dataBody = 0;
    u32 strings = 0;
    u32 stringsBody = 0;
    u32 stringsBodySize = 0;
};

static ByteView MakeView(const std::vector<u8>& meta)
{
    return ByteView(meta.data(), meta.size(), ByteView::IsBigEndianMark(meta.data(), meta.size(), cHeaderByteOrder));
}

static bool ReadCString(const ByteView& view, u64 offset, std::string* out)
{
    if (offset >= view.size())
        return false;

    const u8* start = view.at(offset);
    const void* end = std::memchr(start, 0, view.size() - offset);

    if (!end)
        return false;

    out->assign(reinterpret_cast<const char*>(start), static_cast<const u8*>(end) - start);
    return true;
}

static bool ReadStringTableLayout(const ByteView& view, u16 version, StringTableLayout* out)
{
    const u32 dataOffset = view.readU32(cHeaderDataOffset);
    const u32 stringsOffset = view.readU32(version >= cVersion3 ? cHeaderStringsV3 : cHeaderStringsV1);

    if (!view.has(dataOffset, cSectionHeaderSize + cDataMinSize) || !view.has(stringsOffset, cSectionHeaderSize))
        return false;

    out->dataBody = dataOffset + cSectionHeaderSize;
    out->strings = stringsOffset;
    out->stringsBody = stringsOffset + cSectionHeaderSize;
    out->stringsBodySize = view.readU32(stringsOffset + cSectionBodySize);

    return view.has(out->stringsBody, out->stringsBodySize);
}

static void CollectEntryNameOffsets(const ByteView& view, u32 sectionOffset, u32 entrySize, u32 nameField, std::vector<u64>* out)
{
    if (sectionOffset == 0 || !view.has(sectionOffset, cSectionHeaderSize + sizeof(u32)))
        return;

    const u64 body = sectionOffset + cSectionHeaderSize;
    const u32 count = view.readU32(body);

    for (u32 i = 0; i < count; i++)
    {
        const u64 field = body + cEntryListStart + static_cast<u64>(i) * entrySize + nameField;

        if (!view.has(field, sizeof(u32)))
            break;

        out->push_back(field);
    }
}

static bool RenameAmtaV5(std::vector<u8>* meta, const std::string& name)
{
    const ByteView view = MakeView(*meta);
    const u64 nameStart = cHeaderNamePointerV5 + static_cast<u64>(view.readU32(cHeaderNamePointerV5));

    if (nameStart < cHeaderMinSizeV5 || nameStart > meta->size())
        return false;

    ByteBuilder builder(std::move(*meta), view.isBigEndian());
    builder.resize(nameStart);
    builder.appendBytes(name.data(), name.size());
    builder.appendU8(0);
    builder.alignTo(cFileAlignment);
    builder.putU32(cHeaderFileSize, static_cast<u32>(builder.size()));
    builder.putU32(cHeaderNameHashV5, sead::HashCRC32::calcHash(name.data(), static_cast<u32>(name.size())));

    *meta = builder.take();
    return true;
}

static bool RenameAmtaWithStringTable(std::vector<u8>* meta, u16 version, const std::string& name)
{
    const ByteView view = MakeView(*meta);

    StringTableLayout layout;
    if (!ReadStringTableLayout(view, version, &layout))
        return false;

    const u64 stringsEnd = static_cast<u64>(layout.stringsBody) + layout.stringsBodySize;
    if (sead::Mathu::roundUpPow2(static_cast<u32>(stringsEnd), cFileAlignment) != meta->size())
        return false;

    const u64 nameField = layout.dataBody + cDataName;
    const u32 nameOffset = view.readU32(nameField);

    const u64 nameStart = static_cast<u64>(layout.stringsBody) + nameOffset;

    std::string oldName;
    if (!ReadCString(view, nameStart, &oldName) || nameStart + oldName.size() >= stringsEnd)
        return false;

    std::vector<u64> otherFields;
    CollectEntryNameOffsets(view, view.readU32(cHeaderMarkOffset), cMarkEntrySize, cMarkEntryName, &otherFields);

    if (version >= cVersion3)
        CollectEntryNameOffsets(view, view.readU32(cHeaderExtOffset), cExtEntrySize, cExtEntryName, &otherFields);

    std::vector<u32> otherOffsets;
    otherOffsets.reserve(otherFields.size());

    for (u64 field : otherFields)
    {
        const u32 offset = view.readU32(field);

        if (offset == nameOffset || field + sizeof(u32) > layout.strings)
            return false;

        otherOffsets.push_back(offset);
    }

    const s64 delta = static_cast<s64>(name.size()) - static_cast<s64>(oldName.size());
    const std::vector<u8> tail(meta->begin() + nameStart + oldName.size(), meta->begin() + stringsEnd);

    ByteBuilder builder(std::move(*meta), view.isBigEndian());
    builder.resize(nameStart);
    builder.appendBytes(name.data(), name.size());
    builder.appendBytes(tail.data(), tail.size());
    builder.alignTo(cFileAlignment);

    for (u32 i = 0; i < otherFields.size(); i++)
    {
        if (otherOffsets[i] > nameOffset)
            builder.putU32(otherFields[i], static_cast<u32>(otherOffsets[i] + delta));
    }

    builder.putU32(layout.strings + cSectionBodySize, static_cast<u32>(layout.stringsBodySize + delta));
    builder.putU32(cHeaderFileSize, static_cast<u32>(builder.size()));

    *meta = builder.take();
    return true;
}

static u32 FloatBits(f32 value)
{
    u32 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));

    return bits;
}

static bool ReadAmta5Parts(const std::vector<u8>& meta, Amta5Parts* out)
{
    const ByteView view = MakeView(meta);
    const u32 infoOffset = view.readU32(cHeaderInfoOffsetV5);
    const u64 nameOffset = cHeaderNamePointerV5 + static_cast<u64>(view.readU32(cHeaderNamePointerV5));

    if (infoOffset < cHeaderMinSizeV5 || !view.has(infoOffset, cInfoFixedSize))
        return false;

    const u64 envelopeEnd = infoOffset + cInfoFixedSize + static_cast<u64>(view.readU16(infoOffset + cInfoEnvelopeCount)) * cEnvelopePointSize;

    if (envelopeEnd > nameOffset || nameOffset > meta.size())
        return false;

    out->isBigEndian = view.isBigEndian();
    out->header.assign(meta.begin(), meta.begin() + cHeaderChannelsV5);
    out->channelTable.assign(meta.begin() + cHeaderChannelsV5, meta.begin() + infoOffset);
    out->info.assign(meta.begin() + infoOffset, meta.begin() + infoOffset + cInfoFixedSize);
    out->envelope.assign(meta.begin() + infoOffset + cInfoFixedSize, meta.begin() + envelopeEnd);
    out->tail.assign(meta.begin() + envelopeEnd, meta.end());
    out->infoOffset = infoOffset;
    out->tailOffset = static_cast<u32>(envelopeEnd);
    out->nameOffset = static_cast<u32>(nameOffset);

    return true;
}

static u32 MapAmta5Offset(const Amta5Parts& parts, u32 newInfoOffset, u32 newTailOffset, u32 offset)
{
    if (offset >= parts.tailOffset)
        return newTailOffset + (offset - parts.tailOffset);

    if (offset >= parts.infoOffset)
        return newInfoOffset + (offset - parts.infoOffset);

    return offset;
}

static std::vector<u8> WriteAmta5Parts(const Amta5Parts& parts)
{
    const u32 newInfoOffset = cHeaderChannelsV5 + static_cast<u32>(parts.channelTable.size());
    const u32 newTailOffset = newInfoOffset + cInfoFixedSize + static_cast<u32>(parts.envelope.size());

    ByteBuilder builder(parts.isBigEndian);
    builder.appendBytes(parts.header.data(), parts.header.size());
    builder.appendBytes(parts.channelTable.data(), parts.channelTable.size());
    builder.appendBytes(parts.info.data(), parts.info.size());
    builder.appendBytes(parts.envelope.data(), parts.envelope.size());
    builder.appendBytes(parts.tail.data(), parts.tail.size());

    const ByteView view(parts.header.data(), parts.header.size(), parts.isBigEndian);

    for (u32 field = cHeaderOffsetFirstV5; field <= cHeaderOffsetLastV5; field += sizeof(u32))
    {
        const u32 offset = view.readU32(field);

        if (offset != 0)
            builder.putU32(field, MapAmta5Offset(parts, newInfoOffset, newTailOffset, offset));
    }

    builder.putU32(cHeaderNamePointerV5, MapAmta5Offset(parts, newInfoOffset, newTailOffset, parts.nameOffset) - cHeaderNamePointerV5);
    builder.putU32(cHeaderFileSize, static_cast<u32>(builder.size()));

    return builder.take();
}

static std::vector<u8> MakeAmta5ChannelTable(u32 channelCount, bool isBigEndian)
{
    ByteBuilder builder(isBigEndian);
    builder.appendU8(cChannelTableVersion);
    builder.appendU8(static_cast<u8>(channelCount));

    for (u32 i = 0; i < channelCount; i++)
    {
        builder.appendU8(static_cast<u8>(i));
        builder.appendU8(channelCount == 1 ? cMonoPosition : static_cast<u8>(i));
    }

    builder.alignTo(cFileAlignment);
    return builder.take();
}

static bool UpdateAmta5WaveFields(std::vector<u8>* meta, const AmtaWaveFields& fields)
{
    Amta5Parts parts;

    if (!ReadAmta5Parts(*meta, &parts))
        return false;

    ByteBuilder header(std::move(parts.header), parts.isBigEndian);
    u8 flags = header.bytes()[cHeaderFlagsV5];

    flags = fields.isLoop ? flags | cFlagsLoopV5 : flags & ~cFlagsLoopV5;

    const u32 currentChannels = parts.channelTable.size() >= 2 ? parts.channelTable[1] : 0;

    if (fields.channelCount != currentChannels && (fields.channelCount == 1 || fields.channelCount == 2))
    {
        parts.channelTable = MakeAmta5ChannelTable(fields.channelCount, parts.isBigEndian);
        flags &= ~cFlagsSpreadV5;
    }

    header.putU8(cHeaderFlagsV5, flags);
    parts.header = header.take();

    *meta = WriteAmta5Parts(parts);
    return true;
}

static bool WriteAmta5Loudness(std::vector<u8>* meta, const AmtaLoudness& loudness)
{
    Amta5Parts parts;

    if (!ReadAmta5Parts(*meta, &parts) || loudness.envelope.size() > 0xFFFF)
        return false;

    ByteBuilder info(std::move(parts.info), parts.isBigEndian);
    info.putU32(cInfoPeak, FloatBits(loudness.peak));
    info.putU32(cInfoRms300, FloatBits(loudness.rms300));
    info.putU32(cInfoMomentaryMax, FloatBits(loudness.momentaryMax));
    info.putU32(cInfoIntegrated, FloatBits(loudness.integrated));
    info.putU16(cInfoEnvelopeCount, static_cast<u16>(loudness.envelope.size()));
    info.putU32(cInfoLoudnessRange, FloatBits(loudness.loudnessRange));
    parts.info = info.take();

    ByteBuilder envelope(parts.isBigEndian);

    for (const AmtaEnvelopePoint& point : loudness.envelope)
    {
        envelope.appendU32(point.position);
        envelope.appendU32(FloatBits(point.power));
    }

    parts.envelope = envelope.take();

    *meta = WriteAmta5Parts(parts);
    return true;
}

static bool FindAmta4Data(const ByteView& view, u64* outBody, u32* outBodySize)
{
    const u32 dataOffset = view.readU32(cHeaderDataOffset);

    if (!view.has(dataOffset, cSectionHeaderSize + cDataMinSize))
        return false;

    *outBody = dataOffset + cSectionHeaderSize;
    *outBodySize = view.readU32(dataOffset + cSectionBodySize);

    return view.has(*outBody, *outBodySize);
}

bool IsAmta(const void* data, u64 size)
{
    return data && size >= cHeaderMinSize && std::memcmp(data, cAmtaMagic, sizeof(u32)) == 0 && ByteView::IsByteOrderMark(data, size, cHeaderByteOrder);
}

u32 MeasureAmta(const void* data, u64 size)
{
    if (!IsAmta(data, size))
        return 0;

    const ByteView view(data, size, ByteView::IsBigEndianMark(data, size, cHeaderByteOrder));
    const u32 fileSize = view.readU32(cHeaderFileSize);
    const u32 minSize = view.readU16(cHeaderVersion) >= cAmtaVersion5 ? cHeaderMinSizeV5 : cHeaderMinSize;

    if (fileSize < minSize || fileSize > size)
        return 0;

    return fileSize;
}

static bool IsCompleteAmta(const std::vector<u8>& meta)
{
    return !meta.empty() && MeasureAmta(meta.data(), meta.size()) == meta.size();
}

u16 GetAmtaVersion(const std::vector<u8>& meta)
{
    if (!IsAmta(meta.data(), meta.size()))
        return 0;

    return MakeView(meta).readU16(cHeaderVersion);
}

bool ReadAmtaName(const std::vector<u8>& meta, std::string* outName)
{
    if (!IsCompleteAmta(meta))
        return false;

    const ByteView view = MakeView(meta);
    const u16 version = view.readU16(cHeaderVersion);

    if (version >= cAmtaVersion5)
        return ReadCString(view, cHeaderNamePointerV5 + static_cast<u64>(view.readU32(cHeaderNamePointerV5)), outName);

    StringTableLayout layout;
    if (!ReadStringTableLayout(view, version, &layout))
        return false;

    return ReadCString(view, static_cast<u64>(layout.stringsBody) + view.readU32(layout.dataBody + cDataName), outName);
}

bool RenameAmta(std::vector<u8>* meta, const std::string& name)
{
    if (!IsCompleteAmta(*meta))
        return false;

    const u16 version = GetAmtaVersion(*meta);

    if (version >= cAmtaVersion5)
        return RenameAmtaV5(meta, name);

    return RenameAmtaWithStringTable(meta, version, name);
}

bool UpdateAmtaWaveFields(std::vector<u8>* meta, const AmtaWaveFields& fields)
{
    if (!IsCompleteAmta(*meta))
        return false;

    const ByteView view = MakeView(*meta);
    const u16 version = view.readU16(cHeaderVersion);

    if (version >= cAmtaVersion5)
        return UpdateAmta5WaveFields(meta, fields);

    if (version != cAmtaVersion4 && version != cAmtaVersion1)
        return true;

    u64 body = 0;
    u32 bodySize = 0;

    if (!FindAmta4Data(view, &body, &bodySize))
        return false;

    const u8 flags = static_cast<u8>(fields.isLoop ? view.readU8(body + cDataFlags) | cDataLoopFlag : view.readU8(body + cDataFlags) & ~cDataLoopFlag);

    ByteBuilder builder(std::move(*meta), view.isBigEndian());

    if (version == cAmtaVersion4)
        builder.putU32(body + cDataStoredSamples, fields.storedSampleCount);

    builder.putU8(body + cDataChannelCount, static_cast<u8>(fields.channelCount));
    builder.putU8(body + cDataFlags, flags);
    builder.putU32(body + cDataSampleRate, fields.sampleRate);
    builder.putU32(body + cDataLoopStart, fields.isLoop ? fields.loopStartFrame : 0);
    builder.putU32(body + cDataSampleCount, fields.sampleCount);

    *meta = builder.take();
    return true;
}

bool HasAmtaLoudness(const std::vector<u8>& meta)
{
    if (!IsCompleteAmta(meta))
        return false;

    const ByteView view = MakeView(meta);
    const u16 version = view.readU16(cHeaderVersion);

    if (version >= cAmtaVersion5)
        return true;

    u64 body = 0;
    u32 bodySize = 0;

    if (version != cAmtaVersion4 || !FindAmta4Data(view, &body, &bodySize) || bodySize < cDataLoudness + sizeof(f32))
        return false;

    const bool hasPeak = bodySize >= cDataPeak + sizeof(f32) && view.readU32(body + cDataPeak) != 0;

    return view.readU32(body + cDataRms300) != 0 || view.readU32(body + cDataLoudness) != 0 || hasPeak;
}

bool WriteAmtaLoudness(std::vector<u8>* meta, const AmtaLoudness& loudness)
{
    if (!HasAmtaLoudness(*meta))
        return false;

    const ByteView view = MakeView(*meta);

    if (view.readU16(cHeaderVersion) >= cAmtaVersion5)
        return WriteAmta5Loudness(meta, loudness);

    u64 body = 0;
    u32 bodySize = 0;

    FindAmta4Data(view, &body, &bodySize);

    ByteBuilder builder(std::move(*meta), view.isBigEndian());
    builder.putU32(body + cDataRms300, FloatBits(loudness.rms300));
    builder.putU32(body + cDataLoudness, FloatBits(loudness.momentaryMax));

    if (bodySize >= cDataPeak + sizeof(f32))
        builder.putU32(body + cDataPeak, FloatBits(loudness.peak));

    *meta = builder.take();
    return true;
}

}
