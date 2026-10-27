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

using ActionCallback = void (*)(u32 token);

struct Action
{
    const char* label = nullptr;
    ActionCallback callback = nullptr;
    u32 token = 0;
};

void Add(Kind kind, const char* title, const char* text = nullptr, const Action& action = Action());

f32 DrawBell(f32 right, f32 top, f32 bottom);

void Draw(bool showToasts);

}
