#include <scan/FileProbe.h>

#include <aal/BarsList.h>
#include <aal/BarsSarc.h>
#include <bfsar/archive/Bea.h>
#include <bfsar/archive/Cmpbin.h>
#include <bfsar/files/WaveFile.h>
#include <bfsar/io/PackFile.h>
#include <bfsar/model/ArchiveFormat.h>
#include <core/ByteView.h>
#include <core/NwFile.h>
#include <core/Sarc.h>
#include <messages/Messages.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

static constexpr u64 cHeaderReadBytes      = 512;
static constexpr u64 cCompressedReadBytes  = 64 * 1024;
static constexpr u64 cCompressedReadMax    = 8 * 1024 * 1024;
static constexpr u64 cCompressedReadGrowth = 4;
static constexpr u64 cArchiveHeadMax       = 64 * 1024 * 1024;
static constexpr u64 cRiffHeadReadBytes    = 64 * 1024;
static constexpr size_t cPeekBytes         = 4096;
static constexpr u64 cBeaPeekReadBytes     = 132 * 1024;
static constexpr size_t cBeaProbeAssetMax  = 16;
static constexpr u32 cMagicLength          = 4;

static constexpr u16 cNwInfoBlockType = 0x7000;

static constexpr u32 cWaveInfoIsLoop       = 0x01;
static constexpr u32 cWaveInfoSampleRate   = 0x04;
static constexpr u32 cWaveInfoLoopEnd      = 0x0C;
static constexpr u32 cWaveInfoChannelCount = 0x14;

static constexpr u32 cBarsByteOrder  = 0x08;
static constexpr u32 cBarsVersion    = 0x0A;
static constexpr u32 cBarsAssetCount = 0x0C;

static constexpr u32 cBarsListByteOrder = 0x04;
static constexpr u32 cBarsListVersion   = 0x06;
static constexpr u32 cBarsListCount     = 0x0C;

static constexpr u32 cBwavByteOrder      = 0x04;
static constexpr u32 cBwavVersion        = 0x06;
static constexpr u32 cBwavPrefetch       = 0x0C;
static constexpr u32 cBwavChannelCount   = 0x0E;
static constexpr u32 cBwavChannelInfo    = 0x10;
static constexpr u32 cBwavSampleRate     = 0x04;
static constexpr u32 cBwavFullSamples    = 0x08;
static constexpr u32 cBwavLoopEnd        = 0x3C;
static constexpr u32 cBwavChannelInfoEnd = 0x4C;
static constexpr u32 cBwavNoLoop         = 0xFFFFFFFF;

static constexpr const char* cBarsMagic = "BARS";
static constexpr const char* cBwavMagic = "BWAV";
static constexpr const char* cRiffMagic = "RIFF";
static constexpr const char* cSarcFormatName = "SARC";
static constexpr const char* cWavFormatName  = "WAV";

static bool HasMagic(const void* data, u64 size, const char* magic)
{
    return size >= cMagicLength && std::memcmp(data, magic, cMagicLength) == 0;
}

static void CopyFormatName(const char* name, FileProbe* out)
{
    snprintf(out->formatName, sizeof(out->formatName), "%s", name);
}

static void FormatNwVersion(u32 version, u32 byteCount, char* out, size_t outSize)
{
    const u32 majorShift = nwfile::GetVersionMajorShift(version);
    const u32 major = (version >> majorShift) & 0xFF;
    const u32 minor = (version >> (majorShift - 8)) & 0xFF;
    const u32 micro = (version >> (majorShift - 16)) & 0xFF;

    if (byteCount >= 4 && majorShift == 24)
    {
        snprintf(out, outSize, "%u.%u.%u.%u", major, minor, micro, version & 0xFF);
        return;
    }

    snprintf(out, outSize, "%u.%u.%u", major, minor, micro);
}

static BrowserFileKind ToBrowserKind(InnerFileKind kind, bool* outShown)
{
    *outShown = true;

    switch (kind)
    {
        case InnerFileKind::SoundArchive:
            return BrowserFileKind::SoundArchive;

        case InnerFileKind::Wave:
            return BrowserFileKind::Wave;

        case InnerFileKind::Stream:
            return BrowserFileKind::Stream;

        case InnerFileKind::StreamPrefetch:
            return BrowserFileKind::StreamPrefetch;

        default:
            break;
    }

    *outShown = false;

    return BrowserFileKind::SoundArchive;
}

static void ProbeNwWave(const ByteView& view, FileProbe* out)
{
    nwfile::Block info;

    if (!nwfile::FindBlockRef(view, cNwInfoBlockType, &info) || info.offset == 0)
        return;

    const u64 body = info.offset + nwfile::cBlockHeaderSize;

    if (!view.has(body, cWaveInfoChannelCount + sizeof(u32)))
        return;

    out->isLoop = view.readU8(body + cWaveInfoIsLoop) != 0;
    out->sampleRate = view.readU32(body + cWaveInfoSampleRate);
    out->sampleCount = view.readU32(body + cWaveInfoLoopEnd);
    out->channelCount = view.readU32(body + cWaveInfoChannelCount);
}

static bool ProbeNwFile(const void* data, u64 size, FileProbe* out)
{
    const InnerFileFormatInfo* format = FindInnerFileFormat(data);

    if (!format || !ByteView::IsByteOrderMark(data, size, nwfile::cHeaderByteOrder))
        return false;

    bool isShown = false;
    out->kind = ToBrowserKind(format->kind, &isShown);

    if (!isShown)
        return false;

    const ByteView view(data, size, ByteView::IsBigEndianMark(data, size, nwfile::cHeaderByteOrder));

    if (!view.has(nwfile::cHeaderVersion, sizeof(u32)))
        return false;

    const u32 versionBytes = format->kind == InnerFileKind::SoundArchive ? GetArchiveVersionByteCount(format->format) : 3;

    CopyFormatName(format->displayName, out);
    FormatNwVersion(view.readU32(nwfile::cHeaderVersion), versionBytes, out->versionText, sizeof(out->versionText));

    if (format->kind == InnerFileKind::Wave)
        ProbeNwWave(view, out);

    return true;
}

static bool ProbeBars(const void* data, u64 size, FileProbe* out)
{
    if (!HasMagic(data, size, cBarsMagic) || !ByteView::IsByteOrderMark(data, size, cBarsByteOrder))
        return false;

    const ByteView view(data, size, ByteView::IsBigEndianMark(data, size, cBarsByteOrder));

    if (!view.has(cBarsAssetCount, sizeof(u32)))
        return false;

    const u16 version = view.readU16(cBarsVersion);

    out->kind = BrowserFileKind::AudioResource;
    CopyFormatName(GetResourceFormatInfo(ResourceFormat::Bars).displayName, out);
    out->hasItemCount = true;
    out->itemCount = view.readU32(cBarsAssetCount);
    snprintf(out->versionText, sizeof(out->versionText), "%u.%u", (version >> 8) & 0xFF, version & 0xFF);

    return true;
}

static bool ProbeBwav(const void* data, u64 size, FileProbe* out)
{
    if (!HasMagic(data, size, cBwavMagic) || !ByteView::IsByteOrderMark(data, size, cBwavByteOrder))
        return false;

    const ByteView view(data, size, ByteView::IsBigEndianMark(data, size, cBwavByteOrder));

    if (!view.has(cBwavChannelCount, sizeof(u16)))
        return false;

    out->kind = BrowserFileKind::Wave;
    CopyFormatName(GetResourceFormatInfo(ResourceFormat::Bwav).displayName, out);
    out->isPrefetch = view.readU16(cBwavPrefetch) != 0;
    out->channelCount = view.readU16(cBwavChannelCount);
    snprintf(out->versionText, sizeof(out->versionText), "%u", view.readU16(cBwavVersion));

    if (out->channelCount > 0 && view.has(cBwavChannelInfo, cBwavChannelInfoEnd))
    {
        out->sampleRate = view.readU32(cBwavChannelInfo + cBwavSampleRate);
        out->sampleCount = view.readU32(cBwavChannelInfo + cBwavFullSamples);
        out->isLoop = view.readU32(cBwavChannelInfo + cBwavLoopEnd) != cBwavNoLoop;
    }

    return true;
}

static bool PeekCompressed(const void* data, u64 size, u64 limit, std::vector<u8>* out)
{
    if (cmpbin::IsZstdFrame(data, size))
        return cmpbin::PeekZstdFrame(data, size, limit, *out);

    return cmpbin::IsCompressed(data, size) && cmpbin::Peek(data, size, limit, *out);
}

static bool PeekCompressedHead(const void* data, u64 size, std::vector<u8>* out)
{
    if (!PeekCompressed(data, size, cPeekBytes, out))
        return false;

    const u64 sarcIndexSize = sarc::GetIndexSize(out->data(), out->size());

    if (sarcIndexSize > out->size() && sarcIndexSize <= cArchiveHeadMax)
        PeekCompressed(data, size, sarcIndexSize, out);

    return true;
}

static bool IsSarcIndexRead(const void* data, u64 size)
{
    std::vector<u8> inner;

    if (!PeekCompressedHead(data, size, &inner))
        return false;

    const u64 indexSize = sarc::GetIndexSize(inner.data(), inner.size());

    return indexSize > 0 && inner.size() >= indexSize;
}

static bool ProbeSarc(const void* data, u64 size, FileProbe* out)
{
    sarc::Index index;

    if (!sarc::ReadIndex(data, size, &index))
        return false;

    const bool isPack = packfile::IsSoundPackIndex(index);
    const u32 count = isPack ? packfile::CountPackResources(index) : aal::CountBarsSarcAssets(index);

    if (count == 0)
        return false;

    out->kind = BrowserFileKind::AudioResource;
    CopyFormatName(isPack ? cSarcFormatName : GetResourceFormatInfo(ResourceFormat::Bars).displayName, out);
    out->hasItemCount = true;
    out->itemCount = count;
    out->isPack = isPack;
    snprintf(out->versionText, sizeof(out->versionText), "%u.%u", (index.version >> 8) & 0xFF, index.version & 0xFF);

    return true;
}

static bool ProbeRiff(const void* data, u64 size, u64 fileSize, FileProbe* out)
{
    if (!HasMagic(data, size, cRiffMagic) || fileSize > UINT32_MAX)
        return false;

    WaveFile::RiffWaveInfo info;

    if (!WaveFile::readRiffWavBytes(data, static_cast<u32>(size), static_cast<u32>(fileSize), &info, true))
        return false;

    out->kind = BrowserFileKind::Wav;
    CopyFormatName(cWavFormatName, out);
    out->channelCount = info.numChannels;
    out->sampleRate = info.sampleRate;
    out->sampleCount = info.sampleCount;
    out->isLoop = info.isLoop;

    return true;
}

static bool ProbeBarsList(const void* data, u64 size, FileProbe* out)
{
    if (!aal::IsBarsList(data, size))
        return false;

    const ByteView view(data, size, ByteView::IsBigEndianMark(data, size, cBarsListByteOrder));

    out->kind = BrowserFileKind::ResourceList;
    CopyFormatName(messages::barslist::cFormatName, out);
    out->hasItemCount = true;
    out->itemCount = view.readU32(cBarsListCount);
    snprintf(out->versionText, sizeof(out->versionText), "%u", view.readU16(cBarsListVersion));

    return true;
}

static bool ProbeUncompressed(const void* data, u64 size, FileProbe* out)
{
    return ProbeNwFile(data, size, out) || ProbeBars(data, size, out) || ProbeBarsList(data, size, out) || ProbeBwav(data, size, out) || ProbeSarc(data, size, out) || ProbeRiff(data, size, size, out);
}

bool ProbeFileBytes(const void* data, u64 size, FileProbe* out)
{
    *out = FileProbe();

    if (!data || size < cMagicLength)
        return false;

    std::vector<u8> inner;

    if (!PeekCompressedHead(data, size, &inner))
        return ProbeUncompressed(data, size, out);

    if (!ProbeUncompressed(inner.data(), inner.size(), out))
        return false;

    out->isCompressed = true;

    return true;
}

static void ReadPrefix(std::ifstream& stream, u64 fileSize, u64 limit, std::vector<u8>* out)
{
    out->resize(static_cast<size_t>(fileSize < limit ? fileSize : limit));

    stream.clear();
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(out->data()), static_cast<std::streamsize>(out->size()));
    out->resize(static_cast<size_t>(stream.gcount()));
}

static void ReadRange(std::ifstream& stream, u64 offset, u64 size, std::vector<u8>* out)
{
    out->resize(static_cast<size_t>(size));

    stream.clear();
    stream.seekg(static_cast<std::streamoff>(offset));
    stream.read(reinterpret_cast<char*>(out->data()), static_cast<std::streamsize>(out->size()));
    out->resize(static_cast<size_t>(stream.gcount()));
}

static bool PeekBeaAsset(std::ifstream& stream, const bea::Asset& asset, u64 readLimit, u64 peekLimit, std::vector<u8>* out)
{
    std::vector<u8> frame;
    ReadRange(stream, asset.dataOffset, std::min<u64>(asset.compressedSize, readLimit), &frame);

    return cmpbin::PeekZstdFrame(frame.data(), frame.size(), peekLimit, *out);
}

static void ProbeBeaArchiveContents(std::ifstream& stream, const bea::Asset& asset, const std::vector<u8>& head, FileProbe* out)
{
    const u64 headSize = GetArchiveHeadSize(head.data(), head.size());

    if (headSize == 0 || headSize > cArchiveHeadMax)
        return;

    std::vector<u8> inner;

    for (u64 limit = cCompressedReadBytes; ; limit *= cCompressedReadGrowth)
    {
        if (PeekBeaAsset(stream, asset, limit, headSize, &inner) && inner.size() >= headSize)
        {
            ProbeArchiveContents(inner.data(), inner.size(), &out->contents);
            return;
        }

        if (limit >= asset.compressedSize || limit >= cArchiveHeadMax)
            return;
    }
}

static bool ProbeBeaFile(std::ifstream& stream, u64 fileSize, const std::vector<u8>& header, ProbeDepth depth, FileProbe* out)
{
    const u64 metadataSize = bea::GetMetadataSize(header.data(), header.size());

    if (metadataSize == 0 || metadataSize > cArchiveHeadMax)
        return false;

    std::vector<u8> metadata;
    std::vector<bea::Asset> assets;
    ReadPrefix(stream, fileSize, metadataSize, &metadata);

    if (!bea::ReadAssetTable(metadata.data(), metadata.size(), fileSize, &assets))
        return false;

    std::vector<u8> head;

    for (size_t i = 0; i < assets.size() && i < cBeaProbeAssetMax; i++)
    {
        if (!PeekBeaAsset(stream, assets[i], cBeaPeekReadBytes, cPeekBytes, &head) || !ProbeUncompressed(head.data(), head.size(), out) || out->kind != BrowserFileKind::SoundArchive)
        {
            *out = FileProbe();
            continue;
        }

        out->isCompressed = true;

        if (depth == ProbeDepth::Contents)
            ProbeBeaArchiveContents(stream, assets[i], head, out);

        return true;
    }

    return false;
}

static void ProbeArchiveFile(std::ifstream& stream, u64 fileSize, std::vector<u8>* bytes, FileProbe* out)
{
    const u64 headSize = GetArchiveHeadSize(bytes->data(), bytes->size());

    if (headSize == 0 || headSize > cArchiveHeadMax)
        return;

    if (headSize > bytes->size())
        ReadPrefix(stream, fileSize, headSize, bytes);

    ProbeArchiveContents(bytes->data(), bytes->size(), &out->contents);
}

static bool ProbeSarcFile(std::ifstream& stream, u64 fileSize, std::vector<u8>* bytes, FileProbe* out)
{
    const u64 indexSize = sarc::GetIndexSize(bytes->data(), bytes->size());

    if (indexSize == 0 || indexSize > cArchiveHeadMax)
        return false;

    if (indexSize > bytes->size())
        ReadPrefix(stream, fileSize, indexSize, bytes);

    return ProbeSarc(bytes->data(), bytes->size(), out);
}

static void ProbeCompressedArchiveFile(std::ifstream& stream, u64 fileSize, FileProbe* out)
{
    std::vector<u8> bytes;
    std::vector<u8> inner;

    for (u64 limit = cCompressedReadBytes; ; limit *= cCompressedReadGrowth)
    {
        ReadPrefix(stream, fileSize, std::min(limit, cArchiveHeadMax), &bytes);

        if (!PeekCompressed(bytes.data(), bytes.size(), cPeekBytes, &inner))
            return;

        const u64 headSize = GetArchiveHeadSize(inner.data(), inner.size());

        if (headSize == 0 || headSize > cArchiveHeadMax || !PeekCompressed(bytes.data(), bytes.size(), headSize, &inner))
            return;

        if (inner.size() >= headSize || bytes.size() >= fileSize || limit >= cArchiveHeadMax)
        {
            ProbeArchiveContents(inner.data(), inner.size(), &out->contents);
            return;
        }
    }
}

bool ProbeFile(const std::filesystem::path& path, u64 fileSize, ProbeDepth depth, FileProbe* out)
{
    *out = FileProbe();

    std::ifstream stream(path, std::ios::binary);

    if (!stream)
        return false;

    std::vector<u8> bytes;
    ReadPrefix(stream, fileSize, cHeaderReadBytes, &bytes);

    if (bea::IsBea(bytes.data(), bytes.size()))
        return ProbeBeaFile(stream, fileSize, bytes, depth, out);

    if (sarc::IsSarc(bytes.data(), bytes.size()))
        return ProbeSarcFile(stream, fileSize, &bytes, out);

    if (HasMagic(bytes.data(), bytes.size(), cRiffMagic))
    {
        ReadPrefix(stream, fileSize, cRiffHeadReadBytes, &bytes);
        return ProbeRiff(bytes.data(), bytes.size(), fileSize, out);
    }

    const bool isCompressed = cmpbin::IsZstdFrame(bytes.data(), bytes.size()) || cmpbin::IsCompressed(bytes.data(), bytes.size());
    const bool isContentsRead = depth == ProbeDepth::Contents;

    if (!isCompressed)
    {
        if (!ProbeFileBytes(bytes.data(), bytes.size(), out))
            return false;

        if (isContentsRead && out->kind == BrowserFileKind::SoundArchive)
            ProbeArchiveFile(stream, fileSize, &bytes, out);

        return true;
    }

    for (u64 limit = cCompressedReadBytes; ; limit *= cCompressedReadGrowth)
    {
        ReadPrefix(stream, fileSize, limit, &bytes);

        const bool isProbed = ProbeFileBytes(bytes.data(), bytes.size(), out);

        if (isProbed && isContentsRead && out->kind == BrowserFileKind::SoundArchive)
            ProbeCompressedArchiveFile(stream, fileSize, out);

        if (isProbed || bytes.size() >= fileSize || limit >= cCompressedReadMax || IsSarcIndexRead(bytes.data(), bytes.size()))
            return isProbed;
    }
}

bool ReadFileContents(const std::filesystem::path& path, std::vector<u8>* out)
{
    std::error_code error;
    const u64 fileSize = std::filesystem::file_size(path, error);
    std::ifstream stream(path, std::ios::binary);

    if (error || !stream)
        return false;

    std::vector<u8> bytes;
    ReadPrefix(stream, fileSize, fileSize, &bytes);

    if (bytes.size() != fileSize)
        return false;

    if (cmpbin::IsZstdFrame(bytes.data(), bytes.size()))
        return cmpbin::DecompressZstdFrame(bytes.data(), bytes.size(), *out);

    if (cmpbin::IsCompressed(bytes.data(), bytes.size()))
        return cmpbin::Decompress(bytes.data(), bytes.size(), *out);

    *out = std::move(bytes);

    return true;
}
