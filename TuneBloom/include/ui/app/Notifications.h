#pragma once

#include <basis/seadTypes.h>

namespace notify
{

enum class Kind
{
    Info,
    Success,
    Warning,
    Error
};

void Add(Kind kind, const char* title, const char* text = nullptr);

f32 DrawBell(f32 right, f32 top, f32 bottom);

void Draw();

}
