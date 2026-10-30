#pragma once

#include <aal/Amta.h>

#include <core/ByteView.h>

class ByteBuilder;

namespace aal
{

namespace amta
{

inline constexpr u32 cHeaderByteOrder      = 0x04;
inline constexpr u32 cHeaderVersion        = 0x06;
inline constexpr u32 cHeaderFileSize       = 0x08;
inline constexpr u32 cHeaderDataOffset     = 0x0C;
inline constexpr u32 cHeaderMarkOffset     = 0x10;
inline constexpr u32 cHeaderExtOffset      = 0x14;
inline constexpr u32 cHeaderStringsV1      = 0x14;
inline constexpr u32 cHeaderStringsV3      = 0x18;
inline constexpr u32 cHeaderMinSize        = 0x1C;
inline constexpr u32 cHeaderMinSizeV1      = 0x18;
inline constexpr u32 cHeaderNamePointerV5  = 0x24;
inline constexpr u32 cHeaderNameHashV5     = 0x28;
inline constexpr u32 cHeaderMinSizeV5      = 0x34;
inline constexpr u32 cHeaderFlagsV5        = 0x2C;
inline constexpr u32 cHeaderChannelsV5     = 0x30;
inline constexpr u32 cHeaderInfoOffsetV5   = 0x10;
inline constexpr u32 cHeaderMarkersV5      = 0x14;
inline constexpr u32 cHeaderTagsV5         = 0x1C;
inline constexpr u32 cHeaderExtOffsetV5    = 0x20;
inline constexpr u32 cInfoMaskKnown        = 0x7F;
inline constexpr u32 cInfoMaskNew          = 0x6F;
inline constexpr u32 cInfoBitPeak          = 0;
inline constexpr u32 cInfoBitRms300        = 1;
inline constexpr u32 cInfoBitMomentaryMax  = 2;
inline constexpr u32 cInfoBitIntegrated    = 3;
inline constexpr u32 cInfoBitQuietTail     = 4;
inline constexpr u32 cInfoBitEnvelope      = 5;
inline constexpr u32 cInfoBitLoudnessRange = 6;
inline constexpr u32 cEnvelopeKindField    = 0x02;
inline constexpr u16 cEnvelopeKindNew      = 2;
inline constexpr u32 cEnvelopePointSize    = 0x08;
inline constexpr u32 cEnvelopeCountMax     = 0xFFFF;
inline constexpr u8 cFlagsStreamV5         = 0x01;
inline constexpr u8 cFlagsLoopV5           = 0x04;
inline constexpr u8 cFlagsMidSideV5        = 0x80;
inline constexpr u8 cFlagsOpusV5           = 0x10;
inline constexpr u8 cFlagsOpusSamplesV5    = 0x40;
inline constexpr u8 cFlagsSamplesV5        = 0x02;
inline constexpr u8 cFlagsNewV5            = cFlagsSamplesV5;
inline constexpr u16 cByteOrderMark        = 0xFEFF;
inline constexpr u32 cChannelEntrySize     = 0x02;
inline constexpr u32 cChannelCountMax      = 0xFF;
inline constexpr u8 cPositionFrontLeft     = 0x00;
inline constexpr u8 cPositionFrontRight    = 0x01;
inline constexpr u8 cPositionCenter        = 0x04;
inline constexpr u8 cSurroundPositions[]   = { 0x00, 0x01, 0x04, 0x05, 0x02, 0x03 };
inline constexpr u32 cSurroundChannels     = sizeof(cSurroundPositions);
inline constexpr u32 cHeaderMinfOffsetV5   = 0x18;
inline constexpr u32 cMinfByteOrder        = 0x04;
inline constexpr u32 cMinfSizeField        = 0x08;
inline constexpr u32 cMinfHeaderSize       = 0x0C;
inline constexpr u32 cMinfSampleRate       = 0x14;
inline constexpr u32 cMinfLoopStart        = 0x18;
inline constexpr u32 cMinfSampleCount      = 0x1C;
inline constexpr u32 cMinfWaveFieldsEnd    = 0x20;
inline constexpr u32 cMinfTempo            = 0x24;
inline constexpr u32 cTagEntrySize         = 0x04;
inline constexpr u32 cAmta1DataBodySize    = 0x60;
inline constexpr u32 cAmta4DataBodySize    = 0x64;
inline constexpr f32 cAmta1DataVolume      = 100.0f / 127.0f;
inline constexpr u32 cDataStreamPairs      = 0x20;
inline constexpr u32 cDataStreamPairNum    = 8;
inline constexpr f32 cDataStreamPairVolume = 1.0f;
inline constexpr u8 cDataFlagsBfwav        = 0x02;
inline constexpr u8 cDataFlagsBwav         = 0x22;
inline constexpr u8 cDataFlagStream        = 0x01;
inline constexpr u8 cDataFlagBwavAudio     = 0x20;
inline constexpr u32 cDataIsStream         = 0x08;
inline constexpr u32 cDataTrackCount       = 0x0A;
inline constexpr u32 cDataRms300           = 0x0C;
inline constexpr u32 cDataVolumeV1         = 0x0C;
inline constexpr u32 cDataLoudness         = 0x1C;
inline constexpr u32 cDataPeak             = 0x60;
inline constexpr u32 cSectionHeaderSize    = 0x08;
inline constexpr u32 cSectionBodySize      = 0x04;
inline constexpr u32 cDataName             = 0x00;
inline constexpr u32 cDataStoredSamples    = 0x04;
inline constexpr u32 cDataChannelCount     = 0x09;
inline constexpr u32 cDataFlags            = 0x0B;
inline constexpr u32 cDataSampleRate       = 0x10;
inline constexpr u32 cDataLoopStart        = 0x14;
inline constexpr u32 cDataSampleCount      = 0x18;
inline constexpr u32 cDataMinSize          = 0x1C;
inline constexpr u32 cMarkEntrySize        = 0x10;
inline constexpr u32 cMarkEntryName        = 0x04;
inline constexpr u32 cMarkEntryStart       = 0x08;
inline constexpr u32 cMarkEntryLength      = 0x0C;
inline constexpr u32 cExtEntrySize         = 0x08;
inline constexpr u32 cExtEntryName         = 0x00;
inline constexpr u32 cExtEntryValue        = 0x04;
inline constexpr u32 cEntryListStart       = 0x04;
inline constexpr u32 cFileAlignment        = 4;
inline constexpr u8 cDataLoopFlag          = 0x04;
inline constexpr u32 cVersion3             = 0x0300;

inline constexpr char cAmtaMagic[] = "AMTA";
inline constexpr char cDataMagic[] = "DATA";
inline constexpr char cMarkMagic[] = "MARK";
inline constexpr char cExtMagic[]  = "EXT_";
inline constexpr char cStrgMagic[] = "STRG";
inline constexpr char cMinfMagic[] = "MINF";

}

struct Amta5Parts
{
    bool isBigEndian = false;
    std::vector<u8> header;
    std::vector<u8> channelTable;
    std::vector<u8> info;
    std::vector<u8> envelope;
    std::vector<u8> minf;
    AmtaLabels labels;
    std::string name;
};

struct StringTableLayout
{
    u32 dataBody = 0;
    u32 strings = 0;
    u32 stringsBody = 0;
    u32 stringsBodySize = 0;
};

struct AmtaData
{
    ByteView view = ByteView(nullptr, 0, false);
    u16 version = 0;
    u64 body = 0;
    u32 bodySize = 0;
};

ByteView MakeView(const std::vector<u8>& meta);
bool IsCompleteAmta(const std::vector<u8>& meta);
bool IsAmta4Layout(u16 version);
bool FindAmta4Data(const ByteView& view, u64* outBody, u32* outBodySize);
bool ReadAmtaData(const std::vector<u8>& meta, AmtaData* out);
bool ReadStringTableLayout(const ByteView& view, u16 version, StringTableLayout* out);
bool ReadAmtaMarkerList(const ByteView& view, u64 body, u64 stringsBase, std::vector<AmtaMarker>* out);
u32 FloatBits(f32 value);
f32 FloatFromBits(u32 bits);
void AppendAmtaHeader(ByteBuilder* builder, u16 version, u32 size);

bool ParseAmta5Parts(const std::vector<u8>& meta, Amta5Parts* out);
bool ReadAmta5Parts(const std::vector<u8>& meta, Amta5Parts* out);
std::vector<u8> WriteAmta5Parts(const Amta5Parts& parts);
bool RenameAmtaV5(std::vector<u8>* meta, const std::string& name);
bool UpdateAmta5WaveFields(std::vector<u8>* meta, const AmtaWaveFields& fields);
bool WriteAmta5Loudness(std::vector<u8>* meta, const AmtaLoudness& loudness);
bool ReadMinfLength(const ByteView& view, AmtaSourceAudio* out);

bool ReadAmta4Labels(const std::vector<u8>& meta, u16 version, AmtaLabels* out);
bool CanWriteAmta4Labels(const std::vector<u8>& meta);
bool WriteAmta4Labels(std::vector<u8>* meta, const AmtaLabels& labels);
bool RenameAmtaWithStringTable(std::vector<u8>* meta, u16 version, const std::string& name);

}
