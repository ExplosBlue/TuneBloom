#pragma once

#include <basis/seadTypes.h>

struct MenuTree;

namespace nativemenu
{

struct Texts
{
    const char* appName = "";
    const char* about = "";
    const char* settings = "";
    const char* services = "";
    const char* hide = "";
    const char* hideOthers = "";
    const char* showAll = "";
    const char* quit = "";
    const char* window = "";
    const char* minimize = "";
    const char* zoom = "";
    const char* bringAllToFront = "";
};

struct Setup
{
    void (*record)(MenuTree* tree) = nullptr;
    void (*activate)(u32 id) = nullptr;
    Texts texts;
};

bool IsSupported();
bool IsInstalled();
void Install(const Setup& setup);
void Refresh();

}
