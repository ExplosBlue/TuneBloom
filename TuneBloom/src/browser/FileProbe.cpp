#include <browser/FileProbe.h>

#include <bfsar/archive/Cmpbin.h>
#include <bfsar/model/ArchiveFormat.h>
#include <core/ByteView.h>
#include <snd/snd_Util.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

static constexpr u64 cHeaderReadBytes      = 512;
static constexpr u64 cCompressedReadBytes  = 64 * 1024;
static constexpr u64 cCompressedReadMax    = 8 * 1024 * 1024;
static constexpr u64 cCompressedReadGrowth = 4;
static constexpr u64 cArchiveHeadMax       = 64 * 1024 * 1024;
static constexpr size_t cPeekBytes         = 4096;
static constexpr u32 cMagicLength          = 4;

static constexpr u32 cNwHeaderByteOrder  = 0x04;
static constexpr u32 cNwHeaderVersion    = 0x08;
static constexpr u32 cNwHeaderBlockCount = 0x10;
static constexpr u32 cNwHeaderBlockRefs  = 0x14;
static constexpr u32 cNwBlockRefSize     = 0x0C;
static constexpr u32 cNwBlockRefOffset   = 0x04;
static constexpr u16 cNwInfoBlockType    = 0x7000;
static constexpr u32 cNwBlockBody        = 0x08;

static constexpr u32 cWaveInfoIsLoop       = 0x01;
static constexpr u32 cWaveInfoSampleRate   = 0x04;
static constexpr u32 cWaveInfoLoopEnd      = 0x0C;
static constexpr u32 cWaveInfoChannelCount = 0x14;

static constexpr u32 cBarsByteOrder  = 0x08;
static constexpr u32 cBarsVersion    = 0x0A;
static constexpr u32 cBarsAssetCount = 0x0C;

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
    const u32 majorShift = nw::snd::internal::Util::IsHighByteMajorVersion(version) ? 24 : 16;
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

static u64 FindNwInfoBlock(const ByteView& view)
{
    if (!view.has(cNwHeaderBlockCount, sizeof(u16)))
        return 0;

    const u32 blockCount = view.readU16(cNwHeaderBlockCount);

    for (u32 i = 0; i < blockCount; i++)
    {
        const u64 ref = cNwHeaderBlockRefs + static_cast<u64>(i) * cNwBlockRefSize;

        if (!view.has(ref, cNwBlockRefSize))
            return 0;

        if (view.readU16(ref) == cNwInfoBlockType)
            return view.readU32(ref + cNwBlockRefOffset);
    }

    return 0;
}

static void ProbeNwWave(const ByteView& view, FileProbe* out)
{
    const u64 infoBlock = FindNwInfoBlock(view);

    if (infoBlock == 0)
        return;

    const u64 body = infoBlock + cNwBlockBody;

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

    if (!format || !ByteView::IsByteOrderMark(data, size, cNwHeaderByteOrder))
        return false;

    bool isShown = false;
    out->kind = ToBrowserKind(format->kind, &isShown);

    if (!isShown)
        return false;

    const ByteView view(data, size, ByteView::IsBigEndianMark(data, size, cNwHeaderByteOrder));

    if (!view.has(cNwHeaderVersion, sizeof(u32)))
        return false;

    const u32 versionBytes = format->kind == InnerFileKind::SoundArchive ? GetArchiveVersionByteCount(format->format) : 3;

    CopyFormatName(format->displayName, out);
    FormatNwVersion(view.readU32(cNwHeaderVersion), versionBytes, out->versionText, sizeof(out->versionText));

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

static bool ProbeUncompressed(const void* data, u64 size, FileProbe* out)
{
    return ProbeNwFile(data, size, out) || ProbeBars(data, size, out) || ProbeBwav(data, size, out);
}

bool ProbeFileBytes(const void* data, u64 size, FileProbe* out)
{
    *out = FileProbe();

    if (!data || size < cMagicLength)
        return false;

    std::vector<u8> inner;

    if (!PeekCompressed(data, size, cPeekBytes, &inner))
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

static void ProbeArchiveFile(std::ifstream& stream, u64 fileSize, std::vector<u8>* bytes, FileProbe* out)
{
    const u64 headSize = GetArchiveHeadSize(bytes->data(), bytes->size());

    if (headSize == 0 || headSize > cArchiveHeadMax)
        return;

    if (headSize > bytes->size())
        ReadPrefix(stream, fileSize, headSize, bytes);

    ProbeArchiveContents(bytes->data(), bytes->size(), &out->contents);
}

static void ProbeCompressedArchiveFile(std::ifstream& stream, u64 fileSize, FileProbe* out)
{
    std::vector<u8> bytes;
    std::vector<u8> inner;

    ReadPrefix(stream, fileSize, cArchiveHeadMax, &bytes);

    if (!PeekCompressed(bytes.data(), bytes.size(), cPeekBytes, &inner))
        return;

    const u64 headSize = GetArchiveHeadSize(inner.data(), inner.size());

    if (headSize == 0 || headSize > cArchiveHeadMax || !PeekCompressed(bytes.data(), bytes.size(), headSize, &inner))
        return;

    ProbeArchiveContents(inner.data(), inner.size(), &out->contents);
}

bool ProbeFile(const std::filesystem::path& path, u64 fileSize, FileProbe* out)
{
    *out = FileProbe();

    std::ifstream stream(path, std::ios::binary);

    if (!stream)
        return false;

    std::vector<u8> bytes;
    ReadPrefix(stream, fileSize, cHeaderReadBytes, &bytes);

    const bool isCompressed = cmpbin::IsZstdFrame(bytes.data(), bytes.size()) || cmpbin::IsCompressed(bytes.data(), bytes.size());

    if (!isCompressed)
    {
        if (!ProbeFileBytes(bytes.data(), bytes.size(), out))
            return false;

        if (out->kind == BrowserFileKind::SoundArchive)
            ProbeArchiveFile(stream, fileSize, &bytes, out);

        return true;
    }

    for (u64 limit = cCompressedReadBytes; ; limit *= cCompressedReadGrowth)
    {
        ReadPrefix(stream, fileSize, limit, &bytes);

        const bool isProbed = ProbeFileBytes(bytes.data(), bytes.size(), out);

        if (isProbed && out->kind == BrowserFileKind::SoundArchive)
            ProbeCompressedArchiveFile(stream, fileSize, out);

        if (isProbed || bytes.size() >= fileSize || limit >= cCompressedReadMax)
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
