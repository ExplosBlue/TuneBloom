#pragma once

#include <basis/seadTypes.h>

#include <core/Sarc.h>

#include <string>
#include <vector>

namespace aal
{

inline constexpr char cAmtaFileExtension[] = "bameta";

bool IsBarsSarc(const void* data, u64 size);
u32 CountBarsSarcAssets(const sarc::Index& index);

}
