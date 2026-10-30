#pragma once

#include <basis/seadTypes.h>

#include <core/Sarc.h>

#include <string>
#include <vector>

namespace aal
{

inline constexpr char cAmtaFileExtension[] = "bameta";
inline constexpr char cBarsFileExtension[] = "bars";

bool IsBarsSarc(const void* data, u64 size);
bool IsSoundPackIndex(const sarc::Index& index);
bool IsPackResourceName(const std::string& name);
u32 CountBarsSarcAssets(const sarc::Index& index);
u32 CountPackResources(const sarc::Index& index);

}
