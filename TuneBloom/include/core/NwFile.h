#pragma once

#include <core/ByteView.h>

namespace nwfile
{

inline constexpr u32 cHeaderByteOrder   = 0x04;
inline constexpr u32 cHeaderVersion     = 0x08;
inline constexpr u32 cHeaderFileSize    = 0x0C;
inline constexpr u32 cHeaderBlockCount  = 0x10;
inline constexpr u32 cHeaderBlockRefs   = 0x14;
inline constexpr u32 cHeaderSize        = cHeaderBlockRefs;
inline constexpr u32 cBlockRefSize      = 0x0C;
inline constexpr u32 cBlockRefOffset    = 0x04;
inline constexpr u32 cBlockRefSizeField = 0x08;
inline constexpr u32 cBlockHeaderSize   = 0x08;
inline constexpr u32 cReferenceSize     = 0x08;
inline constexpr u32 cReferenceOffset   = 0x04;
inline constexpr u32 cNullOffset        = 0xFFFFFFFF;

struct Block
{
    u64 offset = 0;
    u64 size = 0;
};

bool FindBlockRef(const ByteView& view, u16 type, Block* out);
bool FindBlock(const ByteView& view, u16 type, Block* out);
u32 GetVersionMajorShift(u32 version);

}
