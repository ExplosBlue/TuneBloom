#include <core/PlayerFile.h>

#include <cstdio>
#include <cstring>

namespace playerfile
{

static constexpr u32 cMagicSize = 4;

static const char* const cPlayerMagics[] = { "FSTM", "CSTM", "FWAV", "CWAV", "BWAV" };

bool HasPlayerMagic(const void* data, u64 size)
{
    if (!data || size < cMagicSize)
        return false;

    for (const char* magic : cPlayerMagics)
    {
        if (std::memcmp(data, magic, cMagicSize) == 0)
            return true;
    }

    return false;
}

bool IsPlayerFile(const char* path)
{
    std::FILE* file = path ? std::fopen(path, "rb") : nullptr;

    if (!file)
        return false;

    u8 head[cMagicSize] = {};
    const size_t read = std::fread(head, 1, cMagicSize, file);
    std::fclose(file);

    return HasPlayerMagic(head, read);
}

}
