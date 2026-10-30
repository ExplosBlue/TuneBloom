#include <aal/AmtaInternal.h>

#include <core/ByteBuilder.h>

#include <math/seadMathCalcCommon.h>

#include <algorithm>
#include <cstring>

namespace aal
{

using namespace amta;

namespace
{

enum class Amta4SectionKind
{
    Copied,
    Markers,
    Keys,
    Strings
};

struct Amta4Section
{
    u32 headerField = 0;
    Amta4SectionKind kind = Amta4SectionKind::Copied;
    std::vector<u8> bytes;
};

struct Amta4Parts
{
    bool isBigEndian = false;
    std::vector<u8> header;
    std::vector<Amta4Section> sections;
    std::vector<AmtaMarker> markers;
    std::vector<u32> markerNameOffsets;
    std::vector<AmtaExtEntry> keys;
    std::vector<u32> keyNameOffsets;
    std::vector<u32> keyValues;
    std::vector<u8> strings;
};

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

static bool ReadExtEntries(const ByteView& view, u32 extOffset, u32 stringsBody, std::vector<AmtaExtEntry>* out)
{
    if (extOffset == 0)
        return true;

    if (!view.has(extOffset, cSectionHeaderSize + sizeof(u32)))
        return false;

    const u64 entries = extOffset + cSectionHeaderSize + cEntryListStart;
    const u32 count = view.readU32(extOffset + cSectionHeaderSize);

    if (!view.has(entries, static_cast<u64>(count) * cExtEntrySize))
        return false;

    out->resize(count);

    for (u32 i = 0; i < count; i++)
    {
        const u64 entry = entries + static_cast<u64>(i) * cExtEntrySize;
        AmtaExtEntry& ext = (*out)[i];

        if (!view.readCString(static_cast<u64>(stringsBody) + view.readU32(entry + cExtEntryName), &ext.name))
            return false;

        const u32 value = view.readU32(entry + cExtEntryValue);
        ext.isText = IsAmtaTextKey(ext.name) && view.readCString(static_cast<u64>(stringsBody) + value, &ext.text);
        ext.value = ext.isText ? 0 : value;
    }

    return true;
}

static void CollectExtStringValueFields(const ByteView& view, u32 extOffset, u32 stringsBody, std::vector<u64>* out)
{
    std::vector<AmtaExtEntry> entries;

    if (!ReadExtEntries(view, extOffset, stringsBody, &entries))
        return;

    for (u32 i = 0; i < entries.size(); i++)
    {
        if (entries[i].isText)
            out->push_back(extOffset + cSectionHeaderSize + cEntryListStart + static_cast<u64>(i) * cExtEntrySize + cExtEntryValue);
    }
}

bool RenameAmtaWithStringTable(std::vector<u8>* meta, u16 version, const std::string& name)
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
    if (!view.readCString(nameStart, &oldName) || nameStart + oldName.size() >= stringsEnd)
        return false;

    std::vector<u64> otherFields;
    CollectEntryNameOffsets(view, view.readU32(cHeaderMarkOffset), cMarkEntrySize, cMarkEntryName, &otherFields);

    if (version >= cVersion3)
    {
        CollectEntryNameOffsets(view, view.readU32(cHeaderExtOffset), cExtEntrySize, cExtEntryName, &otherFields);
        CollectExtStringValueFields(view, view.readU32(cHeaderExtOffset), layout.stringsBody, &otherFields);
    }

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

static void AppendSectionHeader(ByteBuilder* builder, const char* magic, u32 bodySize)
{
    builder->appendBytes(magic, sizeof(u32));
    builder->appendU32(bodySize);
}

static std::vector<u8> MakeAmtaDataLayout(const std::string& name, AmtaAudioKind kind, u16 version, bool isBigEndian)
{
    const bool isVersion1 = version == cAmtaVersion1;
    const u32 dataBodySize = isVersion1 ? cAmta1DataBodySize : cAmta4DataBodySize;

    ByteBuilder builder(isBigEndian);
    AppendAmtaHeader(&builder, version, isVersion1 ? cHeaderMinSizeV1 : cHeaderMinSize);

    builder.putU32(cHeaderDataOffset, static_cast<u32>(builder.size()));
    AppendSectionHeader(&builder, cDataMagic, dataBodySize);

    const u64 body = builder.size();
    builder.appendZeros(dataBodySize);
    builder.putU8(body + cDataFlags, kind == AmtaAudioKind::Bwav ? cDataFlagsBwav : cDataFlagsBfwav);

    if (isVersion1)
        builder.putU32(body + cDataVolumeV1, FloatBits(cAmta1DataVolume));

    for (u32 i = 0; kind == AmtaAudioKind::Bfwav && i < cDataStreamPairNum; i++)
        builder.putU32(body + cDataStreamPairs + i * sizeof(u64) + sizeof(u32), FloatBits(cDataStreamPairVolume));

    builder.putU32(cHeaderMarkOffset, static_cast<u32>(builder.size()));
    AppendSectionHeader(&builder, cMarkMagic, sizeof(u32));
    builder.appendU32(0);

    if (!isVersion1)
    {
        builder.putU32(cHeaderExtOffset, static_cast<u32>(builder.size()));
        AppendSectionHeader(&builder, cExtMagic, sizeof(u32));
        builder.appendU32(0);
    }

    builder.putU32(isVersion1 ? cHeaderStringsV1 : cHeaderStringsV3, static_cast<u32>(builder.size()));
    AppendSectionHeader(&builder, cStrgMagic, static_cast<u32>(name.size() + 1));
    builder.appendBytes(name.data(), name.size());
    builder.appendU8(0);

    builder.alignTo(cFileAlignment);
    builder.putU32(cHeaderFileSize, static_cast<u32>(builder.size()));

    return builder.take();
}

std::vector<u8> MakeAmta1(const std::string& name, bool isBigEndian)
{
    return MakeAmtaDataLayout(name, AmtaAudioKind::Bfwav, cAmtaVersion1, isBigEndian);
}

std::vector<u8> MakeAmta4(const std::string& name, AmtaAudioKind kind, bool isBigEndian)
{
    return MakeAmtaDataLayout(name, kind, cAmtaVersion4, isBigEndian);
}

bool WriteAmta4StreamLayout(std::vector<u8>* meta, u32 channelCount)
{
    if (!IsCompleteAmta(*meta))
        return false;

    const ByteView view = MakeView(*meta);
    const u32 trackCount = (channelCount + 1) / 2;
    u64 body = 0;
    u32 bodySize = 0;

    if (view.readU16(cHeaderVersion) != cAmtaVersion4 || !FindAmta4Data(view, &body, &bodySize) || bodySize < cDataStreamPairs + cDataStreamPairNum * sizeof(u64) || trackCount > cDataStreamPairNum)
        return false;

    const u8 flags = view.readU8(body + cDataFlags) | cDataFlagStream;
    const bool isBwav = (flags & cDataFlagBwavAudio) != 0;

    ByteBuilder builder(std::move(*meta), view.isBigEndian());
    builder.putU8(body + cDataIsStream, 1);
    builder.putU8(body + cDataTrackCount, static_cast<u8>(trackCount));
    builder.putU8(body + cDataFlags, flags);

    for (u32 i = 0; !isBwav && i < cDataStreamPairNum; i++)
        builder.putU32(body + cDataStreamPairs + i * sizeof(u64), i < trackCount ? std::min(2u, channelCount - i * 2) : 0);

    for (u32 i = 0; isBwav && i < cDataStreamPairNum * 2; i++)
        builder.putU32(body + cDataStreamPairs + i * sizeof(u32), 0);

    for (u32 i = 0; isBwav && trackCount > 1 && i < trackCount; i++)
        builder.putU32(body + cDataStreamPairs + i * sizeof(u32), FloatBits(cDataStreamPairVolume));

    for (u32 i = 0; isBwav && trackCount > 1 && i < channelCount; i++)
        builder.putU8(body + cDataStreamPairs + trackCount * sizeof(u32) + i, static_cast<u8>(i / 2));

    *meta = builder.take();
    return true;
}

static u32 GetAmta4StringsField(u16 version)
{
    return version >= cVersion3 ? cHeaderStringsV3 : cHeaderStringsV1;
}

static bool ReadAmta4Section(const std::vector<u8>& meta, const ByteView& view, u16 version, u32 field, Amta4Section* out)
{
    const u32 offset = view.readU32(field);
    const char* magic = field == cHeaderDataOffset ? cDataMagic : field == cHeaderMarkOffset ? cMarkMagic : field == GetAmta4StringsField(version) ? cStrgMagic : cExtMagic;

    if (offset < cHeaderMinSizeV1 || !view.has(offset, cSectionHeaderSize) || std::memcmp(view.at(offset), magic, sizeof(u32)) != 0)
        return false;

    const u64 size = cSectionHeaderSize + static_cast<u64>(view.readU32(offset + cSectionBodySize));

    if (!view.has(offset, size))
        return false;

    out->headerField = field;
    out->kind = field == cHeaderMarkOffset              ? Amta4SectionKind::Markers
              : field == GetAmta4StringsField(version) ? Amta4SectionKind::Strings
              : field == cHeaderExtOffset              ? Amta4SectionKind::Keys
                                                       : Amta4SectionKind::Copied;
    out->bytes.assign(meta.begin() + offset, meta.begin() + offset + size);

    return true;
}

static bool ParseAmta4Parts(const std::vector<u8>& meta, Amta4Parts* out)
{
    const ByteView view = MakeView(meta);
    const u16 version = view.readU16(cHeaderVersion);

    std::vector<u32> fields = { cHeaderDataOffset, cHeaderMarkOffset, GetAmta4StringsField(version) };

    if (version >= cVersion3)
        fields.push_back(cHeaderExtOffset);

    std::sort(fields.begin(), fields.end(), [&view](u32 left, u32 right)
    {
        return view.readU32(left) < view.readU32(right);
    });

    StringTableLayout layout;
    if (!ReadStringTableLayout(view, version, &layout))
        return false;

    Amta4Parts parts;
    parts.isBigEndian = view.isBigEndian();
    parts.header.assign(meta.begin(), meta.begin() + std::min<u64>(view.readU32(fields.front()), meta.size()));
    parts.sections.resize(fields.size());

    for (u32 i = 0; i < fields.size(); i++)
    {
        if (!ReadAmta4Section(meta, view, version, fields[i], &parts.sections[i]))
            return false;
    }

    const u32 markOffset = view.readU32(cHeaderMarkOffset);

    if (!ReadAmtaMarkerList(view, markOffset + cSectionHeaderSize, layout.stringsBody, &parts.markers))
        return false;

    for (u32 i = 0; i < parts.markers.size(); i++)
        parts.markerNameOffsets.push_back(view.readU32(markOffset + cSectionHeaderSize + cEntryListStart + i * cMarkEntrySize + cMarkEntryName));

    const u32 extOffset = version >= cVersion3 ? view.readU32(cHeaderExtOffset) : 0;

    if (!ReadExtEntries(view, extOffset, layout.stringsBody, &parts.keys))
        return false;

    for (u32 i = 0; i < parts.keys.size(); i++)
    {
        const u64 entry = extOffset + cSectionHeaderSize + cEntryListStart + static_cast<u64>(i) * cExtEntrySize;

        parts.keyNameOffsets.push_back(view.readU32(entry + cExtEntryName));
        parts.keyValues.push_back(view.readU32(entry + cExtEntryValue));
    }

    parts.strings.assign(meta.begin() + layout.stringsBody, meta.begin() + layout.stringsBody + layout.stringsBodySize);

    *out = std::move(parts);
    return true;
}

static void AppendAmta4Markers(ByteBuilder* builder, const Amta4Parts& parts)
{
    AppendSectionHeader(builder, cMarkMagic, static_cast<u32>(sizeof(u32) + parts.markers.size() * cMarkEntrySize));
    builder->appendU32(static_cast<u32>(parts.markers.size()));

    for (u32 i = 0; i < parts.markers.size(); i++)
    {
        builder->appendU32(i);
        builder->appendU32(parts.markerNameOffsets[i]);
        builder->appendU32(parts.markers[i].startFrame);
        builder->appendU32(parts.markers[i].frameCount);
    }
}

static void AppendAmta4Keys(ByteBuilder* builder, const Amta4Parts& parts)
{
    AppendSectionHeader(builder, cExtMagic, static_cast<u32>(sizeof(u32) + parts.keys.size() * cExtEntrySize));
    builder->appendU32(static_cast<u32>(parts.keys.size()));

    for (u32 i = 0; i < parts.keys.size(); i++)
    {
        builder->appendU32(parts.keyNameOffsets[i]);
        builder->appendU32(parts.keyValues[i]);
    }
}

static std::vector<u8> WriteAmta4Parts(const Amta4Parts& parts)
{
    ByteBuilder builder(parts.isBigEndian);
    builder.appendBytes(parts.header.data(), parts.header.size());

    for (const Amta4Section& section : parts.sections)
    {
        builder.putU32(section.headerField, static_cast<u32>(builder.size()));

        if (section.kind == Amta4SectionKind::Markers)
        {
            AppendAmta4Markers(&builder, parts);
        }
        else if (section.kind == Amta4SectionKind::Keys)
        {
            AppendAmta4Keys(&builder, parts);
        }
        else if (section.kind == Amta4SectionKind::Strings)
        {
            AppendSectionHeader(&builder, cStrgMagic, static_cast<u32>(parts.strings.size()));
            builder.appendBytes(parts.strings.data(), parts.strings.size());
        }
        else
        {
            builder.appendBytes(section.bytes.data(), section.bytes.size());
        }
    }

    builder.alignTo(cFileAlignment);
    builder.putU32(cHeaderFileSize, static_cast<u32>(builder.size()));

    return builder.take();
}

static bool ReadAmta4Parts(const std::vector<u8>& meta, Amta4Parts* out)
{
    return ParseAmta4Parts(meta, out) && WriteAmta4Parts(*out) == meta;
}

static u32 FindOrAppendString(std::vector<u8>* strings, const std::string& text)
{
    u64 start = 0;

    while (start < strings->size())
    {
        const u8* begin = strings->data() + start;
        const void* end = std::memchr(begin, 0, strings->size() - start);

        if (!end)
            break;

        const u64 length = static_cast<const u8*>(end) - begin;

        if (length == text.size() && std::memcmp(begin, text.data(), length) == 0)
            return static_cast<u32>(start);

        start += length + 1;
    }

    const u32 offset = static_cast<u32>(strings->size());
    strings->insert(strings->end(), text.begin(), text.end());
    strings->push_back(0);

    return offset;
}

static std::vector<u32> CollectAmta4StringUses(const Amta4Parts& parts)
{
    std::vector<u32> uses = parts.markerNameOffsets;
    uses.insert(uses.end(), parts.keyNameOffsets.begin(), parts.keyNameOffsets.end());

    for (u32 i = 0; i < parts.keys.size(); i++)
    {
        if (parts.keys[i].isText)
            uses.push_back(parts.keyValues[i]);
    }

    for (const Amta4Section& section : parts.sections)
    {
        if (section.bytes.size() >= cSectionHeaderSize + cDataName + sizeof(u32) && std::memcmp(section.bytes.data(), cDataMagic, sizeof(u32)) == 0)
            uses.push_back(ByteView(section.bytes.data(), section.bytes.size(), parts.isBigEndian).readU32(cSectionHeaderSize + cDataName));
    }

    return uses;
}

static void DropUnusedLastStrings(Amta4Parts* parts)
{
    const std::vector<u32> uses = CollectAmta4StringUses(*parts);
    std::vector<u8>& strings = parts->strings;

    while (!strings.empty() && strings.back() == 0)
    {
        const auto previous = std::find(strings.rbegin() + 1, strings.rend(), 0);
        const u32 start = static_cast<u32>(strings.rend() - previous);

        if (std::any_of(uses.begin(), uses.end(), [start](u32 use) { return use >= start; }))
            return;

        strings.resize(start);
    }
}

static void SetAmta4Markers(Amta4Parts* parts, const std::vector<AmtaMarker>& markers)
{
    std::vector<u32> offsets;
    offsets.reserve(markers.size());

    for (u32 i = 0; i < markers.size(); i++)
    {
        const bool isKept = i < parts->markers.size() && parts->markers[i].name == markers[i].name;
        offsets.push_back(isKept ? parts->markerNameOffsets[i] : FindOrAppendString(&parts->strings, markers[i].name));
    }

    parts->markers = markers;
    parts->markerNameOffsets = std::move(offsets);
}

static void SetAmta4Keys(Amta4Parts* parts, const std::vector<AmtaExtEntry>& keys)
{
    std::vector<u32> nameOffsets;
    std::vector<u32> values;

    for (u32 i = 0; i < keys.size(); i++)
    {
        const AmtaExtEntry* old = i < parts->keys.size() ? &parts->keys[i] : nullptr;
        const bool isNameKept = old && old->name == keys[i].name;
        const bool isTextKept = old && old->isText && keys[i].isText && old->text == keys[i].text;

        nameOffsets.push_back(isNameKept ? parts->keyNameOffsets[i] : FindOrAppendString(&parts->strings, keys[i].name));

        if (!keys[i].isText)
            values.push_back(keys[i].value);
        else
            values.push_back(isTextKept ? parts->keyValues[i] : FindOrAppendString(&parts->strings, keys[i].text));
    }

    parts->keys = keys;
    parts->keyNameOffsets = std::move(nameOffsets);
    parts->keyValues = std::move(values);
}

bool ReadAmta4Labels(const std::vector<u8>& meta, u16 version, AmtaLabels* out)
{
    const ByteView view = MakeView(meta);
    const u32 markOffset = view.readU32(cHeaderMarkOffset);
    const u32 extOffset = version >= cVersion3 ? view.readU32(cHeaderExtOffset) : 0;

    StringTableLayout layout;
    if (!ReadStringTableLayout(view, version, &layout))
        return false;

    if (markOffset != 0 && !ReadAmtaMarkerList(view, markOffset + cSectionHeaderSize, layout.stringsBody, &out->markers))
        return false;

    return ReadExtEntries(view, extOffset, layout.stringsBody, &out->keys);
}

bool CanWriteAmta4Labels(const std::vector<u8>& meta)
{
    Amta4Parts parts;

    return ReadAmta4Parts(meta, &parts);
}

bool WriteAmta4Labels(std::vector<u8>* meta, const AmtaLabels& labels)
{
    Amta4Parts parts;

    if (!labels.tags.empty() || !ReadAmta4Parts(*meta, &parts))
        return false;

    const bool hasKeySection = std::any_of(parts.sections.begin(), parts.sections.end(), [](const Amta4Section& section) { return section.kind == Amta4SectionKind::Keys; });

    if (!labels.keys.empty() && !hasKeySection)
        return false;

    if (parts.markers == labels.markers && parts.keys == labels.keys)
        return true;

    SetAmta4Markers(&parts, labels.markers);
    SetAmta4Keys(&parts, labels.keys);
    DropUnusedLastStrings(&parts);
    *meta = WriteAmta4Parts(parts);

    return true;
}

}
