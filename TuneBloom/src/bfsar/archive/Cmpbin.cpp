#include <bfsar/archive/Cmpbin.h>

#include <core/ByteBuilder.h>
#include <core/ByteView.h>

#include <lz4frame.h>
#include <lz4hc.h>
#include <zstd.h>

#include <cstring>

namespace cmpbin
{

static constexpr u32 cSizePrefixBytes = sizeof(u32);
static constexpr u32 cMagicLength     = 4;
static constexpr u32 cLz4SmallBlock   = 64 * 1024;
static constexpr u64 cFrameSizeMax    = 512ull * 1024 * 1024;

static constexpr u8 cZstdMagic[cMagicLength] = { 0x28, 0xB5, 0x2F, 0xFD };
static constexpr u8 cLz4fMagic[cMagicLength] = { 0x04, 0x22, 0x4D, 0x18 };

static bool HasMagic(const void* data, size_t size, size_t offset, const u8* magic)
{
    return data && size >= offset + cMagicLength && std::memcmp(static_cast<const u8*>(data) + offset, magic, cMagicLength) == 0;
}

static bool DecompressZstd(const u8* payload, size_t payloadSize, std::vector<u8>& out)
{
    const size_t written = ZSTD_decompress(out.data(), out.size(), payload, payloadSize);

    return !ZSTD_isError(written) && written == out.size();
}

static bool DecompressZstdStream(const u8* payload, size_t payloadSize, std::vector<u8>& out)
{
    ZSTD_DStream* stream = ZSTD_createDStream();

    if (!stream)
        return false;

    std::vector<u8> chunk(ZSTD_DStreamOutSize());
    ZSTD_inBuffer input = { payload, payloadSize, 0 };
    size_t remaining = 1;

    while (input.pos < input.size && remaining != 0)
    {
        ZSTD_outBuffer output = { chunk.data(), chunk.size(), 0 };
        remaining = ZSTD_decompressStream(stream, &output, &input);

        if (ZSTD_isError(remaining))
            break;

        out.insert(out.end(), chunk.data(), chunk.data() + output.pos);

        if (out.size() > cFrameSizeMax)
            break;
    }

    ZSTD_freeDStream(stream);

    return remaining == 0 && out.size() <= cFrameSizeMax;
}

static bool DecompressLz4f(const u8* payload, size_t payloadSize, std::vector<u8>& out)
{
    LZ4F_dctx* context = nullptr;

    if (LZ4F_isError(LZ4F_createDecompressionContext(&context, LZ4F_getVersion())))
        return false;

    size_t srcOffset = 0;
    size_t dstOffset = 0;
    bool ok = true;

    while (srcOffset < payloadSize)
    {
        size_t dstCapacity = out.size() - dstOffset;
        size_t srcCapacity = payloadSize - srcOffset;

        const size_t hint = LZ4F_decompress(context, out.data() + dstOffset, &dstCapacity, payload + srcOffset, &srcCapacity, nullptr);

        if (LZ4F_isError(hint))
        {
            ok = false;
            break;
        }

        dstOffset += dstCapacity;
        srcOffset += srcCapacity;

        if (hint == 0 || (dstCapacity == 0 && srcCapacity == 0))
            break;
    }

    LZ4F_freeDecompressionContext(context);

    return ok && dstOffset == out.size();
}

static bool CompressLz4f(const void* data, size_t size, std::vector<u8>& out)
{
    LZ4F_preferences_t preferences;

    std::memset(&preferences, 0, sizeof(preferences));
    preferences.frameInfo.blockMode = LZ4F_blockIndependent;
    preferences.frameInfo.blockSizeID = size <= cLz4SmallBlock ? LZ4F_max64KB : LZ4F_max1MB;
    preferences.compressionLevel = LZ4HC_CLEVEL_MAX;

    const size_t bound = LZ4F_compressFrameBound(size, &preferences);
    out.resize(bound);

    const size_t written = LZ4F_compressFrame(out.data(), bound, data, size, &preferences);

    if (LZ4F_isError(written))
        return false;

    out.resize(written);
    return true;
}

bool IsCompressed(const void* data, size_t size)
{
    return HasMagic(data, size, cSizePrefixBytes, cZstdMagic) || HasMagic(data, size, cSizePrefixBytes, cLz4fMagic);
}

Codec DetectCodec(const void* data, size_t size)
{
    return HasMagic(data, size, cSizePrefixBytes, cZstdMagic) ? Codec::Zstd : Codec::Lz4Frame;
}

bool Decompress(const void* data, size_t size, std::vector<u8>& out)
{
    out.clear();

    if (!IsCompressed(data, size))
        return false;

    const u8* payload = static_cast<const u8*>(data) + cSizePrefixBytes;
    const size_t payloadSize = size - cSizePrefixBytes;
    const u32 decompressedSize = ByteView(data, size, false).readU32(0);

    if (decompressedSize == 0)
        return false;

    out.resize(decompressedSize);

    const bool ok = DetectCodec(data, size) == Codec::Zstd ? DecompressZstd(payload, payloadSize, out) : DecompressLz4f(payload, payloadSize, out);

    if (!ok)
        out.clear();

    return ok;
}

bool Compress(const void* data, size_t size, Codec codec, std::vector<u8>& out)
{
    out.clear();

    if (!data || size == 0)
        return false;

    std::vector<u8> frame;
    const bool ok = codec == Codec::Zstd ? CompressZstdFrame(data, size, frame) : CompressLz4f(data, size, frame);

    if (!ok)
        return false;

    ByteBuilder builder(false);
    builder.reserve(cSizePrefixBytes + frame.size());
    builder.appendU32(static_cast<u32>(size));
    builder.appendBytes(frame.data(), frame.size());

    out = builder.take();
    return true;
}

bool IsZstdFrame(const void* data, size_t size)
{
    return HasMagic(data, size, 0, cZstdMagic);
}

bool DecompressZstdFrame(const void* data, size_t size, std::vector<u8>& out)
{
    out.clear();

    if (!IsZstdFrame(data, size))
        return false;

    const u8* payload = static_cast<const u8*>(data);
    const unsigned long long contentSize = ZSTD_getFrameContentSize(payload, size);

    if (contentSize == ZSTD_CONTENTSIZE_ERROR || (contentSize != ZSTD_CONTENTSIZE_UNKNOWN && contentSize > cFrameSizeMax))
        return false;

    bool ok = false;

    if (contentSize == ZSTD_CONTENTSIZE_UNKNOWN)
    {
        ok = DecompressZstdStream(payload, size, out);
    }
    else
    {
        out.resize(static_cast<size_t>(contentSize));
        ok = DecompressZstd(payload, size, out);
    }

    if (!ok)
        out.clear();

    return ok;
}

bool CompressZstdFrame(const void* data, size_t size, std::vector<u8>& out)
{
    const size_t bound = ZSTD_compressBound(size);
    out.resize(bound);

    const size_t written = ZSTD_compress(out.data(), bound, data, size, ZSTD_CLEVEL_DEFAULT);

    if (ZSTD_isError(written))
    {
        out.clear();
        return false;
    }

    out.resize(written);
    return true;
}

}
