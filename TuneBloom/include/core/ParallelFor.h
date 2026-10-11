#pragma once

#include <basis/seadTypes.h>

#include <functional>

void ParallelFor(u32 count, const std::function<void(u32)>& func);
