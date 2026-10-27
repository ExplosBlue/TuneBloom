#pragma once

#include <basis/seadTypes.h>

namespace playerfile
{

bool HasPlayerMagic(const void* data, u64 size);
bool IsPlayerFile(const char* path);

}
