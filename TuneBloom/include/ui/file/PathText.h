#pragma once

#include <basis/seadTypes.h>

#include <string>

namespace pathtext
{

const char* FileName(const char* path);
std::string Folder(const std::string& path);
std::string FitToWidth(const std::string& path, f32 width);
void DrawTooltipFolder(const std::string& path, f32 contentWidth);

}
