#pragma once

#include <string>

namespace openqueue
{

enum class Mode
{
    Pin,
    Preview,
    Reload
};

struct Request
{
    std::string path;
    Mode mode = Mode::Pin;
};

void PushExternal(const char* path);
void Push(const char* path, Mode mode = Mode::Pin);
bool Take(Request* outRequest);
bool IsEmpty();
void Clear();

}
