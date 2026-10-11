#pragma once

#include <basis/seadTypes.h>
#include <cstddef>
#include <vector>

namespace cmpbin
{

    enum class Codec
    {
        Lz4Frame,
        Zstd,
    };

    bool IsCompressed(const void *data, size_t size);
    Codec DetectCodec(const void *data, size_t size);
    bool Decompress(const void *data, size_t size, std::vector<u8> &out);
    bool Compress(const void *data, size_t size, Codec codec, std::vector<u8> &out);

}