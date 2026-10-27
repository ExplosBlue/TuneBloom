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

enum class Origin
{
    App,
    Browser,
    System
};

struct Request
{
    std::string path;
    Mode mode = Mode::Pin;
    Origin origin = Origin::App;
};

void PushExternal(const char* path);
void Push(const char* path, Mode mode = Mode::Pin, Origin origin = Origin::App);
bool Take(Request* outRequest);
bool IsEmpty();
bool Peek(Request* outRequest);
void Clear();

}
