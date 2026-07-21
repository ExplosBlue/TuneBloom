#include <bfsar/Cmpbin.h>

#include <cstring>

#include <lz4frame.h>
#include <lz4hc.h>
#include <zstd.h>

namespace cmpbin
{

    static const u8 cZstdMagic[4] = {0x28, 0xB5, 0x2F, 0xFD};
    static const u8 cLz4fMagic[4] = {0x04, 0x22, 0x4D, 0x18};

    static u32 ReadLE32(const u8 *p)
    {
        return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
    }

    bool IsCompressed(const void *data, size_t size)
    {
        if (!data || size < 8)
            return false;

        const u8 *payload = static_cast<const u8 *>(data) + 4;

        return std::memcmp(payload, cZstdMagic, 4) == 0 || std::memcmp(payload, cLz4fMagic, 4) == 0;
    }

    Codec DetectCodec(const void *data, size_t size)
    {
        if (data && size >= 8 && std::memcmp(static_cast<const u8 *>(data) + 4, cZstdMagic, 4) == 0)
            return Codec::Zstd;

        return Codec::Lz4Frame;
    }

    static bool DecompressZstd(const u8 *payload, size_t payloadSize, std::vector<u8> &out)
    {
        size_t r = ZSTD_decompress(out.data(), out.size(), payload, payloadSize);

        return !ZSTD_isError(r) && r == out.size();
    }

    static bool DecompressLz4f(const u8 *payload, size_t payloadSize, std::vector<u8> &out)
    {
        LZ4F_dctx *dctx = nullptr;

        if (LZ4F_isError(LZ4F_createDecompressionContext(&dctx, LZ4F_getVersion())))
            return false;

        size_t srcOffset = 0;
        size_t dstOffset = 0;
        bool ok = true;

        while (srcOffset < payloadSize)
        {
            size_t dstCap = out.size() - dstOffset;
            size_t srcCap = payloadSize - srcOffset;

            size_t hint = LZ4F_decompress(dctx, out.data() + dstOffset, &dstCap, payload + srcOffset, &srcCap, nullptr);

            if (LZ4F_isError(hint))
            {
                ok = false;
                break;
            }

            dstOffset += dstCap;
            srcOffset += srcCap;

            if (hint == 0)
                break;

            if (dstCap == 0 && srcCap == 0)
                break;
        }

        LZ4F_freeDecompressionContext(dctx);

        return ok && dstOffset == out.size();
    }

    bool Decompress(const void *data, size_t size, std::vector<u8> &out)
    {
        out.clear();

        if (!IsCompressed(data, size))
            return false;

        const u8 *p = static_cast<const u8 *>(data);
        u32 decompressedSize = ReadLE32(p);
        const u8 *payload = p + 4;
        size_t payloadSize = size - 4;

        if (decompressedSize == 0)
            return false;

        out.resize(decompressedSize);

        bool ok = (std::memcmp(payload, cZstdMagic, 4) == 0) ? DecompressZstd(payload, payloadSize, out) : DecompressLz4f(payload, payloadSize, out);

        if (!ok)
            out.clear();

        return ok;
    }

    static void WriteLE32(std::vector<u8> &out, size_t off, u32 v)
    {
        out[off + 0] = (u8)(v & 0xFF);
        out[off + 1] = (u8)((v >> 8) & 0xFF);
        out[off + 2] = (u8)((v >> 16) & 0xFF);
        out[off + 3] = (u8)((v >> 24) & 0xFF);
    }

    bool Compress(const void *data, size_t size, Codec codec, std::vector<u8> &out)
    {
        out.clear();

        if (!data || size == 0)
            return false;

        std::vector<u8> frame;

        if (codec == Codec::Zstd)
        {
            size_t bound = ZSTD_compressBound(size);
            frame.resize(bound);
            size_t r = ZSTD_compress(frame.data(), bound, data, size, ZSTD_CLEVEL_DEFAULT);

            if (ZSTD_isError(r))
                return false;

            frame.resize(r);
        }
        else
        {
            LZ4F_preferences_t prefs;

            std::memset(&prefs, 0, sizeof(prefs));
            prefs.frameInfo.blockMode = LZ4F_blockIndependent;
            prefs.frameInfo.blockSizeID = size <= 64 * 1024 ? LZ4F_max64KB : LZ4F_max1MB;
            prefs.compressionLevel = LZ4HC_CLEVEL_MAX;

            size_t bound = LZ4F_compressFrameBound(size, &prefs);
            frame.resize(bound);
            size_t r = LZ4F_compressFrame(frame.data(), bound, data, size, &prefs);

            if (LZ4F_isError(r))
                return false;

            frame.resize(r);
        }

        out.resize(4 + frame.size());
        WriteLE32(out, 0, (u32)size);
        std::memcpy(out.data() + 4, frame.data(), frame.size());

        return true;
    }
}