#include <core/NwFile.h>

#include <snd/snd_Util.h>

namespace nwfile
{

bool FindBlockRef(const ByteView& view, u16 type, Block* out)
{
    if (!view.has(cHeaderBlockCount, sizeof(u16)))
        return false;

    const u32 count = view.readU16(cHeaderBlockCount);

    for (u32 i = 0; i < count; i++)
    {
        const u64 ref = cHeaderBlockRefs + static_cast<u64>(i) * cBlockRefSize;

        if (!view.has(ref, cBlockRefSize))
            return false;

        if (view.readU16(ref) != type)
            continue;

        out->offset = view.readU32(ref + cBlockRefOffset);
        out->size = view.readU32(ref + cBlockRefSizeField);

        return true;
    }

    return false;
}

bool FindBlock(const ByteView& view, u16 type, Block* out)
{
    return FindBlockRef(view, type, out) && out->offset != cNullOffset && out->size >= cBlockHeaderSize;
}

u32 GetVersionMajorShift(u32 version)
{
    return nw::snd::internal::Util::IsHighByteMajorVersion(version) ? 24 : 16;
}

}
