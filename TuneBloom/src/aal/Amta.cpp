#include <aal/Amta.h>

#include <aal/AmtaInternal.h>
#include <core/ByteBuilder.h>

#include <algorithm>
#include <cstring>

namespace aal
{

using namespace amta;

static constexpr const char* cExtStringNames[] = { "Lang", "VCT" };

ByteView MakeView(const std::vector<u8>& meta)
{
    return ByteView(meta.data(), meta.size(), ByteView::IsBigEndianMark(meta.data(), meta.size(), cHeaderByteOrder));
}

bool ReadStringTableLayout(const ByteView& view, u16 version, StringTableLayout* out)
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

bool IsAmtaTextKey(const std::string& name)
{
    return std::find(std::begin(cExtStringNames), std::end(cExtStringNames), name) != std::end(cExtStringNames);
}

u32 FloatBits(f32 value)
{
    u32 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));

    return bits;
}

f32 FloatFromBits(u32 bits)
{
    f32 value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));

    return value;
}

bool ReadAmtaMarkerList(const ByteView& view, u64 body, u64 stringsBase, std::vector<AmtaMarker>* out)
{
    if (!view.has(body, sizeof(u32)))
        return false;

    const u32 count = view.readU32(body);
    const u64 entries = body + cEntryListStart;

    if (!view.has(entries, static_cast<u64>(count) * cMarkEntrySize))
        return false;

    out->resize(count);

    for (u32 i = 0; i < count; i++)
    {
        const u64 entry = entries + static_cast<u64>(i) * cMarkEntrySize;
        const u64 nameField = entry + cMarkEntryName;
        AmtaMarker& marker = (*out)[i];

        if (!view.readCString((stringsBase != 0 ? stringsBase : nameField) + view.readU32(nameField), &marker.name))
            return false;

        marker.startFrame = view.readU32(entry + cMarkEntryStart);
        marker.frameCount = view.readU32(entry + cMarkEntryLength);
    }

    return true;
}

bool FindAmta4Data(const ByteView& view, u64* outBody, u32* outBodySize)
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

bool IsCompleteAmta(const std::vector<u8>& meta)
{
    return !meta.empty() && MeasureAmta(meta.data(), meta.size()) == meta.size();
}

bool IsAmta4Layout(u16 version)
{
    return version == cAmtaVersion4 || version == cAmtaVersion1;
}

bool ReadAmtaData(const std::vector<u8>& meta, AmtaData* out)
{
    if (!IsCompleteAmta(meta))
        return false;

    out->view = MakeView(meta);
    out->version = out->view.readU16(cHeaderVersion);

    return out->version >= cAmtaVersion5 || (IsAmta4Layout(out->version) && FindAmta4Data(out->view, &out->body, &out->bodySize));
}

static bool ReadAmta4WaveData(const std::vector<u8>& meta, AmtaData* out)
{
    return ReadAmtaData(meta, out) && out->version == cAmtaVersion4 && out->bodySize >= cDataMinSize;
}

u16 GetAmtaVersion(const std::vector<u8>& meta)
{
    if (!IsAmta(meta.data(), meta.size()))
        return 0;

    return MakeView(meta).readU16(cHeaderVersion);
}

bool IsAmtaBigEndian(const std::vector<u8>& meta)
{
    return IsAmta(meta.data(), meta.size()) && MakeView(meta).isBigEndian();
}

bool ReadAmtaName(const std::vector<u8>& meta, std::string* outName)
{
    if (!IsCompleteAmta(meta))
        return false;

    const ByteView view = MakeView(meta);
    const u16 version = view.readU16(cHeaderVersion);

    if (version >= cAmtaVersion5)
        return view.readCString(cHeaderNamePointerV5 + static_cast<u64>(view.readU32(cHeaderNamePointerV5)), outName);

    StringTableLayout layout;
    if (!ReadStringTableLayout(view, version, &layout))
        return false;

    return view.readCString(static_cast<u64>(layout.stringsBody) + view.readU32(layout.dataBody + cDataName), outName);
}

void AppendAmtaHeader(ByteBuilder* builder, u16 version, u32 size)
{
    builder->appendBytes(cAmtaMagic, sizeof(u32));
    builder->appendU16(cByteOrderMark);
    builder->appendU16(version);
    builder->appendZeros(size - builder->size());
}

bool ReadAmtaLabels(const std::vector<u8>& meta, AmtaLabels* out)
{
    *out = AmtaLabels();

    if (!IsCompleteAmta(meta))
        return false;

    const u16 version = GetAmtaVersion(meta);

    if (IsAmta4Layout(version))
        return ReadAmta4Labels(meta, version, out);

    if (version < cAmtaVersion5)
        return false;

    Amta5Parts parts;

    if (!ParseAmta5Parts(meta, &parts))
        return false;

    *out = std::move(parts.labels);
    return true;
}

bool CanAddAmtaKeys(const std::vector<u8>& meta)
{
    const u16 version = GetAmtaVersion(meta);

    return version == cAmtaVersion4 || version >= cAmtaVersion5;
}

bool CanWriteAmtaLabels(const std::vector<u8>& meta)
{
    if (!IsCompleteAmta(meta))
        return false;

    const u16 version = GetAmtaVersion(meta);
    Amta5Parts amta5;

    return IsAmta4Layout(version) ? CanWriteAmta4Labels(meta) : version >= cAmtaVersion5 && ReadAmta5Parts(meta, &amta5);
}

bool WriteAmtaLabels(std::vector<u8>* meta, const AmtaLabels& labels)
{
    if (!IsCompleteAmta(*meta))
        return false;

    const u16 version = GetAmtaVersion(*meta);

    if (IsAmta4Layout(version))
        return WriteAmta4Labels(meta, labels);

    Amta5Parts parts;

    if (version < cAmtaVersion5 || !ReadAmta5Parts(*meta, &parts))
        return false;

    parts.labels = labels;
    *meta = WriteAmta5Parts(parts);

    return true;
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

bool CanRenameAmta(const std::vector<u8>& meta)
{
    if (meta.empty())
        return true;

    std::string name;
    std::vector<u8> renamed = meta;

    return ReadAmtaName(meta, &name) && RenameAmta(&renamed, name);
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

static u32 ScaleFramesToRate(u32 frames, u32 fromRate, u32 toRate)
{
    return static_cast<u32>((static_cast<u64>(frames) * toRate + fromRate / 2) / fromRate);
}

bool HasAmtaSourceRate(const std::vector<u8>& meta, u32 audioSampleRate)
{
    AmtaData data;

    return ReadAmta4WaveData(meta, &data) && data.view.readU32(data.body + cDataSampleRate) != audioSampleRate;
}

static u32 ChooseSourceSampleCount(u32 scaledCount, const AmtaSourceAudio& source, u32 audioSampleRate)
{
    const u32 tolerance = (source.sampleRate + audioSampleRate - 1) / audioSampleRate;
    const u32 distance = scaledCount > source.sampleCount ? scaledCount - source.sampleCount : source.sampleCount - scaledCount;

    return distance <= tolerance ? source.sampleCount : scaledCount;
}

static u32 CalcSourceSampleCount(const AmtaSourceAudio& source, u32 audioSampleRate, u32 audioSampleCount)
{
    return ChooseSourceSampleCount(ScaleFramesToRate(audioSampleCount, audioSampleRate, source.sampleRate), source, audioSampleRate);
}

u32 CalcAmtaMarkerLimit(bool keepsSourceRate, const AmtaSourceAudio& source, u32 audioSampleRate, u32 audioSampleCount)
{
    if (!keepsSourceRate || source.sampleRate == 0 || audioSampleRate == 0)
        return audioSampleCount;

    return CalcSourceSampleCount(source, audioSampleRate, audioSampleCount);
}

bool WriteAmtaSourceRate(std::vector<u8>* meta, const AmtaSourceAudio& source)
{
    AmtaData data;

    if (source.sampleRate == 0 || !ReadAmta4WaveData(*meta, &data))
        return false;

    const ByteView& view = data.view;
    const u64 body = data.body;
    const u32 audioSampleRate = view.readU32(body + cDataSampleRate);

    if (audioSampleRate == 0)
        return false;

    const u32 loopStartFrame = ScaleFramesToRate(view.readU32(body + cDataLoopStart), audioSampleRate, source.sampleRate);
    const u32 sampleCount = CalcSourceSampleCount(source, audioSampleRate, view.readU32(body + cDataSampleCount));

    ByteBuilder builder(std::move(*meta), view.isBigEndian());
    builder.putU32(body + cDataSampleRate, source.sampleRate);
    builder.putU32(body + cDataLoopStart, loopStartFrame);
    builder.putU32(body + cDataSampleCount, sampleCount);

    *meta = builder.take();
    return true;
}

bool ClearAmtaMarkers(std::vector<u8>* meta)
{
    AmtaLabels labels;

    if (!ReadAmtaLabels(*meta, &labels) || labels.markers.empty())
        return false;

    labels.markers.clear();

    return WriteAmtaLabels(meta, labels);
}

bool ClampAmtaMarkers(std::vector<u8>* meta, u32 sampleCount)
{
    AmtaLabels labels;

    if (!ReadAmtaLabels(*meta, &labels))
        return false;

    bool isChanged = false;

    for (AmtaMarker& marker : labels.markers)
    {
        if (!marker.endsAfter(sampleCount))
            continue;

        marker.startFrame = std::min(marker.startFrame, sampleCount);
        marker.frameCount = std::min(marker.frameCount, sampleCount - marker.startFrame);
        isChanged = true;
    }

    return isChanged && WriteAmtaLabels(meta, labels);
}

bool IsAmtaLoop(const std::vector<u8>& meta)
{
    AmtaData data;

    if (!ReadAmtaData(meta, &data))
        return false;

    if (data.version >= cAmtaVersion5)
        return (data.view.readU8(cHeaderFlagsV5) & cFlagsLoopV5) != 0;

    return (data.view.readU8(data.body + cDataFlags) & cDataLoopFlag) != 0;
}

bool ReadAmtaLength(const std::vector<u8>& meta, AmtaSourceAudio* out)
{
    AmtaData data;

    if (!ReadAmtaData(meta, &data))
        return false;

    if (data.version >= cAmtaVersion5)
        return ReadMinfLength(data.view, out);

    out->sampleRate = data.view.readU32(data.body + cDataSampleRate);
    out->sampleCount = data.view.readU32(data.body + cDataSampleCount);

    return out->sampleRate != 0;
}

bool IsAmtaStream(const std::vector<u8>& meta, AmtaAudioKind* outKind)
{
    AmtaData data;
    *outKind = AmtaAudioKind::Bwav;

    if (!ReadAmtaData(meta, &data))
        return false;

    if (data.version >= cAmtaVersion5)
        return (data.view.readU8(cHeaderFlagsV5) & cFlagsStreamV5) != 0;

    if (data.version != cAmtaVersion4 || data.bodySize <= cDataFlags)
        return false;

    *outKind = (data.view.readU8(data.body + cDataFlags) & cDataFlagBwavAudio) != 0 ? AmtaAudioKind::Bwav : AmtaAudioKind::Bfwav;
    return data.view.readU8(data.body + cDataIsStream) != 0;
}

bool HasAmtaLoudness(const std::vector<u8>& meta)
{
    AmtaData data;

    if (!ReadAmtaData(meta, &data))
        return false;

    if (data.version >= cAmtaVersion5)
        return true;

    if (data.version != cAmtaVersion4 || data.bodySize < cDataLoudness + sizeof(f32))
        return false;

    const bool hasPeak = data.bodySize >= cDataPeak + sizeof(f32) && data.view.readU32(data.body + cDataPeak) != 0;

    return data.view.readU32(data.body + cDataRms300) != 0 || data.view.readU32(data.body + cDataLoudness) != 0 || hasPeak;
}

bool WriteAmtaLoudness(std::vector<u8>* meta, const AmtaLoudness& loudness)
{
    AmtaData data;

    if (!ReadAmtaData(*meta, &data))
        return false;

    if (data.version >= cAmtaVersion5)
        return WriteAmta5Loudness(meta, loudness);

    if (data.version != cAmtaVersion4 || data.bodySize < cDataLoudness + sizeof(f32))
        return false;

    const u64 body = data.body;
    const u32 bodySize = data.bodySize;

    ByteBuilder builder(std::move(*meta), data.view.isBigEndian());
    builder.putU32(body + cDataRms300, FloatBits(loudness.rms300));
    builder.putU32(body + cDataLoudness, FloatBits(loudness.momentaryMax));

    if (bodySize >= cDataPeak + sizeof(f32))
        builder.putU32(body + cDataPeak, FloatBits(loudness.peak));

    *meta = builder.take();
    return true;
}

}
