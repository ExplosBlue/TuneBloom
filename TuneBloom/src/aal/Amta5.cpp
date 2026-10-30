#include <aal/AmtaInternal.h>

#include <core/ByteBuilder.h>

#include <codec/seadHashCRC32.h>
#include <math/seadMathCalcCommon.h>

#include <algorithm>
#include <bit>
#include <cstring>

namespace aal
{

using namespace amta;

static u32 GetInfoSize(u32 mask)
{
    return sizeof(u32) + sizeof(u32) * static_cast<u32>(std::popcount(mask));
}

static u32 GetInfoFieldOffset(u32 mask, u32 bit)
{
    return sizeof(u32) + sizeof(u32) * static_cast<u32>(std::popcount(mask & ((1u << bit) - 1)));
}

static bool HasInfoField(u32 mask, u32 bit)
{
    return (mask & (1u << bit)) != 0;
}

static u32 GetInfoMask(const Amta5Parts& parts)
{
    return ByteView(parts.info.data(), parts.info.size(), parts.isBigEndian).readU32(0);
}

static bool ReadRelativeString(const ByteView& view, u64 field, std::string* out)
{
    return view.has(field, sizeof(u32)) && view.readCString(field + view.readU32(field), out);
}

static bool ReadAmta5Info(const std::vector<u8>& meta, const ByteView& view, u32 offset, Amta5Parts* out)
{
    if (!view.has(offset, sizeof(u32)))
        return false;

    const u32 mask = view.readU32(offset);

    if ((mask & ~cInfoMaskKnown) != 0 || !view.has(offset, GetInfoSize(mask)))
        return false;

    const u64 envelopeStart = offset + GetInfoSize(mask);
    const u64 envelopeSize = HasInfoField(mask, cInfoBitEnvelope) ? static_cast<u64>(view.readU16(offset + GetInfoFieldOffset(mask, cInfoBitEnvelope))) * cEnvelopePointSize : 0;

    if (!view.has(envelopeStart, envelopeSize))
        return false;

    out->info.assign(meta.begin() + offset, meta.begin() + envelopeStart);
    out->envelope.assign(meta.begin() + envelopeStart, meta.begin() + envelopeStart + envelopeSize);

    return true;
}

static bool ReadAmta5Tags(const ByteView& view, u32 offset, std::vector<std::string>* out)
{
    if (offset == 0)
        return true;

    if (!view.has(offset, sizeof(u32)))
        return false;

    const u32 count = view.readU32(offset);
    const u64 entries = offset + cEntryListStart;

    if (!view.has(entries, static_cast<u64>(count) * cTagEntrySize))
        return false;

    out->resize(count);

    for (u32 i = 0; i < count; i++)
    {
        if (!ReadRelativeString(view, entries + static_cast<u64>(i) * cTagEntrySize, &(*out)[i]))
            return false;
    }

    return true;
}

static bool ReadAmta5Ext(const ByteView& view, u32 offset, std::vector<AmtaExtEntry>* out)
{
    if (offset == 0)
        return true;

    if (!view.has(offset, sizeof(u32)))
        return false;

    const u32 count = view.readU32(offset);
    const u64 entries = offset + cEntryListStart;

    if (!view.has(entries, static_cast<u64>(count) * cExtEntrySize))
        return false;

    out->resize(count);

    for (u32 i = 0; i < count; i++)
    {
        const u64 entry = entries + static_cast<u64>(i) * cExtEntrySize;
        AmtaExtEntry& ext = (*out)[i];

        if (!ReadRelativeString(view, entry + cExtEntryName, &ext.name))
            return false;

        ext.isText = IsAmtaTextKey(ext.name);
        ext.value = ext.isText ? 0 : view.readU32(entry + cExtEntryValue);

        if (ext.isText && !ReadRelativeString(view, entry + cExtEntryValue, &ext.text))
            return false;
    }

    return true;
}

static bool MeasureChannelTable(const ByteView& view, u64 start, u32* outSize, u32* outChannelCount)
{
    if (!view.has(start, sizeof(u8)))
        return false;

    const u32 sourceCount = view.readU8(start);
    u64 offset = start + sizeof(u8);
    u32 channelCount = 0;

    for (u32 i = 0; i < sourceCount; i++)
    {
        if (!view.has(offset, sizeof(u8)))
            return false;

        const u32 sourceChannels = view.readU8(offset);
        offset += sizeof(u8) + static_cast<u64>(sourceChannels) * cChannelEntrySize;
        channelCount += sourceChannels;
    }

    const u64 size = sead::Mathu::roundUpPow2(offset - start, cFileAlignment);

    if (!view.has(start, size))
        return false;

    *outSize = static_cast<u32>(size);
    *outChannelCount = channelCount;

    return true;
}

static u32 CountTableChannels(const std::vector<u8>& table)
{
    u32 size = 0;
    u32 channelCount = 0;

    return MeasureChannelTable(ByteView(table.data(), table.size(), false), 0, &size, &channelCount) ? channelCount : 0;
}

static bool ReadAmta5Minf(const std::vector<u8>& meta, const ByteView& view, u32 offset, std::vector<u8>* out)
{
    if (offset == 0)
        return true;

    if (!view.has(offset, cMinfHeaderSize) || std::memcmp(view.at(offset), cMinfMagic, sizeof(u32)) != 0)
        return false;

    const u32 size = view.readU32(offset + cMinfSizeField);

    if (size < cMinfHeaderSize || !view.has(offset, size))
        return false;

    out->assign(meta.begin() + offset, meta.begin() + offset + size);

    return true;
}

bool ParseAmta5Parts(const std::vector<u8>& meta, Amta5Parts* out)
{
    const ByteView view = MakeView(meta);

    u32 channelTableSize = 0;
    u32 channelCount = 0;

    if (!MeasureChannelTable(view, cHeaderChannelsV5, &channelTableSize, &channelCount))
        return false;

    const u32 infoOffset = view.readU32(cHeaderInfoOffsetV5);
    const u32 markersOffset = view.readU32(cHeaderMarkersV5);

    Amta5Parts parts;
    parts.isBigEndian = view.isBigEndian();
    parts.header.assign(meta.begin(), meta.begin() + cHeaderChannelsV5);
    parts.channelTable.assign(meta.begin() + cHeaderChannelsV5, meta.begin() + cHeaderChannelsV5 + channelTableSize);

    if (infoOffset != 0 && !ReadAmta5Info(meta, view, infoOffset, &parts))
        return false;

    if (markersOffset != 0 && !ReadAmtaMarkerList(view, markersOffset, 0, &parts.labels.markers))
        return false;

    if (!ReadAmta5Minf(meta, view, view.readU32(cHeaderMinfOffsetV5), &parts.minf) || !ReadAmta5Ext(view, view.readU32(cHeaderExtOffsetV5), &parts.labels.keys))
        return false;

    if (!ReadAmta5Tags(view, view.readU32(cHeaderTagsV5), &parts.labels.tags) || !ReadRelativeString(view, cHeaderNamePointerV5, &parts.name))
        return false;

    *out = std::move(parts);
    return true;
}

static std::vector<std::string> CollectAmta5Strings(const Amta5Parts& parts)
{
    std::vector<std::string> strings = parts.labels.tags;
    strings.push_back(parts.name);

    for (const AmtaMarker& marker : parts.labels.markers)
        strings.push_back(marker.name);

    for (const AmtaExtEntry& ext : parts.labels.keys)
    {
        strings.push_back(ext.name);

        if (ext.isText)
            strings.push_back(ext.text);
    }

    std::sort(strings.begin(), strings.end());
    strings.erase(std::unique(strings.begin(), strings.end()), strings.end());

    return strings;
}

static u32 AppendEntryList(ByteBuilder* builder, u64 count, u32 entrySize)
{
    if (count == 0)
        return 0;

    const u32 offset = static_cast<u32>(builder->size());
    builder->appendU32(static_cast<u32>(count));
    builder->appendZeros(count * entrySize);

    return offset;
}

std::vector<u8> WriteAmta5Parts(const Amta5Parts& parts)
{
    const std::vector<AmtaMarker>& markers = parts.labels.markers;
    const std::vector<std::string>& tags = parts.labels.tags;
    const std::vector<AmtaExtEntry>& keys = parts.labels.keys;

    ByteBuilder builder(parts.isBigEndian);
    builder.appendBytes(parts.header.data(), parts.header.size());
    builder.appendBytes(parts.channelTable.data(), parts.channelTable.size());

    const u32 infoOffset = parts.info.empty() ? 0 : static_cast<u32>(builder.size());
    builder.appendBytes(parts.info.data(), parts.info.size());
    builder.appendBytes(parts.envelope.data(), parts.envelope.size());

    const u32 markersOffset = AppendEntryList(&builder, markers.size(), cMarkEntrySize);
    const u32 extOffset = AppendEntryList(&builder, keys.size(), cExtEntrySize);
    const u32 minfOffset = parts.minf.empty() ? 0 : static_cast<u32>(builder.size());
    builder.appendBytes(parts.minf.data(), parts.minf.size());
    builder.alignTo(cFileAlignment);

    const u32 tagsOffset = AppendEntryList(&builder, tags.size(), cTagEntrySize);

    const std::vector<std::string> strings = CollectAmta5Strings(parts);
    std::vector<u32> stringOffsets;
    stringOffsets.reserve(strings.size());

    for (const std::string& text : strings)
    {
        stringOffsets.push_back(static_cast<u32>(builder.size()));
        builder.appendBytes(text.data(), text.size());
        builder.appendU8(0);
    }

    builder.alignTo(cFileAlignment);

    const auto putString = [&](u64 field, const std::string& text)
    {
        const u64 index = std::lower_bound(strings.begin(), strings.end(), text) - strings.begin();
        builder.putU32(field, static_cast<u32>(stringOffsets[index] - field));
    };

    for (u32 i = 0; i < markers.size(); i++)
    {
        const u64 entry = markersOffset + cEntryListStart + static_cast<u64>(i) * cMarkEntrySize;

        builder.putU32(entry, i);
        putString(entry + cMarkEntryName, markers[i].name);
        builder.putU32(entry + cMarkEntryStart, markers[i].startFrame);
        builder.putU32(entry + cMarkEntryLength, markers[i].frameCount);
    }

    for (u32 i = 0; i < tags.size(); i++)
        putString(tagsOffset + cEntryListStart + static_cast<u64>(i) * cTagEntrySize, tags[i]);

    for (u32 i = 0; i < keys.size(); i++)
    {
        const AmtaExtEntry& ext = keys[i];
        const u64 entry = extOffset + cEntryListStart + static_cast<u64>(i) * cExtEntrySize;

        putString(entry + cExtEntryName, ext.name);

        if (ext.isText)
            putString(entry + cExtEntryValue, ext.text);
        else
            builder.putU32(entry + cExtEntryValue, ext.value);
    }

    putString(cHeaderNamePointerV5, parts.name);
    builder.putU32(cHeaderNameHashV5, sead::HashCRC32::calcHash(parts.name.data(), static_cast<u32>(parts.name.size())));
    builder.putU32(cHeaderInfoOffsetV5, infoOffset);
    builder.putU32(cHeaderMarkersV5, markersOffset);
    builder.putU32(cHeaderMinfOffsetV5, minfOffset);
    builder.putU32(cHeaderTagsV5, tagsOffset);
    builder.putU32(cHeaderExtOffsetV5, extOffset);
    builder.putU32(cHeaderFileSize, static_cast<u32>(builder.size()));

    return builder.take();
}

bool ReadAmta5Parts(const std::vector<u8>& meta, Amta5Parts* out)
{
    return ParseAmta5Parts(meta, out) && WriteAmta5Parts(*out) == meta;
}

static bool RenameAmtaV5AtEnd(std::vector<u8>* meta, const std::string& name)
{
    const ByteView view = MakeView(*meta);
    const u64 nameStart = cHeaderNamePointerV5 + static_cast<u64>(view.readU32(cHeaderNamePointerV5));

    std::string oldName;

    if (nameStart < cHeaderMinSizeV5 || !view.readCString(nameStart, &oldName))
        return false;

    if (sead::Mathu::roundUpPow2(static_cast<u32>(nameStart + oldName.size() + 1), cFileAlignment) != meta->size())
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

bool RenameAmtaV5(std::vector<u8>* meta, const std::string& name)
{
    Amta5Parts parts;

    if (!ReadAmta5Parts(*meta, &parts))
        return RenameAmtaV5AtEnd(meta, name);

    parts.name = name;
    *meta = WriteAmta5Parts(parts);

    return true;
}

static void AppendChannelSource(ByteBuilder* builder, u32 firstChannel, const u8* positions, u32 channelCount)
{
    builder->appendU8(static_cast<u8>(channelCount));

    for (u32 i = 0; i < channelCount; i++)
    {
        builder->appendU8(static_cast<u8>(firstChannel + i));
        builder->appendU8(positions[i]);
    }
}

static std::vector<u8> MakeAmta5ChannelTable(u32 channelCount, bool isBigEndian)
{
    static constexpr u8 cMono[] = { cPositionCenter };
    static constexpr u8 cStereo[] = { cPositionFrontLeft, cPositionFrontRight };

    ByteBuilder builder(isBigEndian);

    if (channelCount == 1 || channelCount == 2 || channelCount == cSurroundChannels)
    {
        builder.appendU8(1);
        AppendChannelSource(&builder, 0, channelCount == 1 ? cMono : channelCount == 2 ? cStereo : cSurroundPositions, channelCount);
    }
    else if (channelCount % 2 == 0)
    {
        builder.appendU8(static_cast<u8>(channelCount / 2));

        for (u32 i = 0; i < channelCount; i += 2)
            AppendChannelSource(&builder, i, cStereo, 2);
    }
    else
    {
        builder.appendU8(static_cast<u8>(channelCount));

        for (u32 i = 0; i < channelCount; i++)
        {
            const u8 position = i < cSurroundChannels ? static_cast<u8>(i) : cPositionCenter;
            AppendChannelSource(&builder, i, &position, 1);
        }
    }

    builder.alignTo(cFileAlignment);
    return builder.take();
}

static void UpdateMinfWaveFields(std::vector<u8>* minf, const AmtaWaveFields& fields)
{
    if (minf->size() < cMinfWaveFieldsEnd)
        return;

    const bool isBigEndian = ByteView::IsBigEndianMark(minf->data(), minf->size(), cMinfByteOrder);
    ByteBuilder builder(std::move(*minf), isBigEndian);

    builder.putU32(cMinfSampleRate, fields.sampleRate);
    builder.putU32(cMinfLoopStart, fields.isLoop ? fields.loopStartFrame : fields.sampleCount);
    builder.putU32(cMinfSampleCount, fields.sampleCount);

    *minf = builder.take();
}

bool UpdateAmta5WaveFields(std::vector<u8>* meta, const AmtaWaveFields& fields)
{
    Amta5Parts parts;

    if (!ReadAmta5Parts(*meta, &parts))
        return false;

    ByteBuilder header(std::move(parts.header), parts.isBigEndian);
    u8 flags = header.bytes()[cHeaderFlagsV5];

    flags = fields.isLoop ? flags | cFlagsLoopV5 : flags & ~cFlagsLoopV5;
    flags = fields.isStream ? flags | cFlagsStreamV5 : flags & ~cFlagsStreamV5;

    flags &= ~(cFlagsOpusV5 | cFlagsOpusSamplesV5);

    if (fields.isOpus)
        flags |= cFlagsOpusV5 | ((flags & cFlagsSamplesV5) != 0 ? cFlagsOpusSamplesV5 : 0);

    if (fields.channelCount != CountTableChannels(parts.channelTable) && fields.channelCount >= 1 && fields.channelCount <= cChannelCountMax)
    {
        parts.channelTable = MakeAmta5ChannelTable(fields.channelCount, parts.isBigEndian);
        flags &= ~cFlagsMidSideV5;
    }

    header.putU8(cHeaderFlagsV5, flags);
    parts.header = header.take();
    UpdateMinfWaveFields(&parts.minf, fields);

    *meta = WriteAmta5Parts(parts);
    return true;
}

bool CanWriteAmta5Loudness(const std::vector<u8>& meta)
{
    Amta5Parts parts;

    return ReadAmta5Parts(meta, &parts) && !parts.info.empty();
}

bool WriteAmta5Loudness(std::vector<u8>* meta, const AmtaLoudness& loudness)
{
    Amta5Parts parts;

    if (!ReadAmta5Parts(*meta, &parts) || parts.info.empty() || loudness.envelope.size() > cEnvelopeCountMax)
        return false;

    const u32 mask = GetInfoMask(parts);
    ByteBuilder info(std::move(parts.info), parts.isBigEndian);

    const auto putField = [&info, mask](u32 bit, f32 value)
    {
        if (HasInfoField(mask, bit))
            info.putU32(GetInfoFieldOffset(mask, bit), FloatBits(value));
    };

    putField(cInfoBitPeak, loudness.peak);
    putField(cInfoBitRms300, loudness.rms300);
    putField(cInfoBitMomentaryMax, loudness.momentaryMax);
    putField(cInfoBitIntegrated, loudness.integrated);
    putField(cInfoBitLoudnessRange, loudness.loudnessRange);

    if (HasInfoField(mask, cInfoBitQuietTail))
        info.putU32(GetInfoFieldOffset(mask, cInfoBitQuietTail), HasInfoField(mask, cInfoBitEnvelope) ? loudness.quietTailFrames : loudness.summedQuietTailFrames);

    if (!HasInfoField(mask, cInfoBitEnvelope))
    {
        parts.info = info.take();
        *meta = WriteAmta5Parts(parts);
        return true;
    }

    info.putU16(GetInfoFieldOffset(mask, cInfoBitEnvelope), static_cast<u16>(loudness.envelope.size()));
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

bool ReadMinfLength(const ByteView& view, AmtaSourceAudio* out)
{
    const u32 offset = view.readU32(cHeaderMinfOffsetV5);

    if (offset == 0 || !view.has(offset, cMinfWaveFieldsEnd) || std::memcmp(view.at(offset), cMinfMagic, sizeof(u32)) != 0)
        return false;

    const u32 size = view.readU32(offset + cMinfSizeField);

    if (size < cMinfWaveFieldsEnd || !view.has(offset, size))
        return false;

    const ByteView minf(view.at(offset), size, ByteView::IsBigEndianMark(view.at(offset), size, cMinfByteOrder));

    out->sampleRate = minf.readU32(cMinfSampleRate);
    out->sampleCount = minf.readU32(cMinfSampleCount);

    return out->sampleRate != 0;
}

u32 GetNewAmta5InfoMask(const std::vector<u8>& meta)
{
    if (!IsCompleteAmta(meta) || GetAmtaVersion(meta) < cAmtaVersion5)
        return 0;

    const ByteView view = MakeView(meta);
    const u32 infoOffset = view.readU32(cHeaderInfoOffsetV5);

    if (infoOffset == 0 || !view.has(infoOffset, sizeof(u32)))
        return 0;

    return view.readU32(infoOffset) & cInfoMaskNew;
}

std::vector<u8> MakeAmta5(const std::string& name, u32 channelCount, u32 infoMask, bool isBigEndian)
{
    ByteBuilder header(isBigEndian);
    AppendAmtaHeader(&header, cAmtaVersion5, cHeaderChannelsV5);
    header.putU8(cHeaderFlagsV5, cFlagsNewV5);

    const u32 mask = infoMask != 0 ? infoMask & cInfoMaskNew : cInfoMaskNew;

    ByteBuilder info(isBigEndian);
    info.appendU32(mask);
    info.appendZeros(GetInfoSize(mask) - sizeof(u32));

    if (HasInfoField(mask, cInfoBitEnvelope))
        info.putU16(GetInfoFieldOffset(mask, cInfoBitEnvelope) + cEnvelopeKindField, cEnvelopeKindNew);

    Amta5Parts parts;
    parts.isBigEndian = isBigEndian;
    parts.header = header.take();
    parts.channelTable = MakeAmta5ChannelTable(channelCount, isBigEndian);
    parts.info = info.take();
    parts.name = name;

    return WriteAmta5Parts(parts);
}

bool ClearAmtaMidSide(std::vector<u8>* meta)
{
    if (!IsCompleteAmta(*meta) || GetAmtaVersion(*meta) < cAmtaVersion5 || ((*meta)[cHeaderFlagsV5] & cFlagsMidSideV5) == 0)
        return false;

    (*meta)[cHeaderFlagsV5] &= static_cast<u8>(~cFlagsMidSideV5);
    return true;
}

bool IsAmtaMidSide(const std::vector<u8>& meta)
{
    return IsCompleteAmta(meta) && GetAmtaVersion(meta) >= cAmtaVersion5 && (meta[cHeaderFlagsV5] & cFlagsMidSideV5) != 0;
}

bool HasAmtaMusicInfo(const std::vector<u8>& meta)
{
    return IsCompleteAmta(meta) && GetAmtaVersion(meta) >= cAmtaVersion5 && MakeView(meta).readU32(cHeaderMinfOffsetV5) != 0;
}

f32 ReadAmtaTempo(const std::vector<u8>& meta)
{
    if (!HasAmtaMusicInfo(meta))
        return 0.0f;

    const ByteView view = MakeView(meta);
    const u32 offset = view.readU32(cHeaderMinfOffsetV5);

    if (!view.has(offset, cMinfTempo + sizeof(f32)) || view.readU32(offset + cMinfSizeField) < cMinfTempo + sizeof(f32))
        return 0.0f;

    return FloatFromBits(view.readU32(offset + cMinfTempo));
}

bool ClearAmtaMusicInfo(std::vector<u8>* meta)
{
    Amta5Parts parts;

    if (!HasAmtaMusicInfo(*meta) || !ReadAmta5Parts(*meta, &parts))
        return false;

    parts.minf.clear();
    *meta = WriteAmta5Parts(parts);

    return true;
}

bool ReadAmtaQuietTail(const std::vector<u8>& meta, u32* outFrames, std::vector<AmtaEnvelopePoint>* outEnvelope)
{
    Amta5Parts parts;

    if (GetAmtaVersion(meta) < cAmtaVersion5 || !ParseAmta5Parts(meta, &parts) || parts.info.empty())
        return false;

    const u32 mask = GetInfoMask(parts);

    if (!HasInfoField(mask, cInfoBitQuietTail) || !HasInfoField(mask, cInfoBitEnvelope))
        return false;

    const ByteView info(parts.info.data(), parts.info.size(), parts.isBigEndian);
    const ByteView envelope(parts.envelope.data(), parts.envelope.size(), parts.isBigEndian);

    *outFrames = info.readU32(GetInfoFieldOffset(mask, cInfoBitQuietTail));
    outEnvelope->clear();

    for (u64 offset = 0; offset + cEnvelopePointSize <= parts.envelope.size(); offset += cEnvelopePointSize)
        outEnvelope->push_back({ envelope.readU32(offset), FloatFromBits(envelope.readU32(offset + sizeof(u32))) });

    return true;
}

}
