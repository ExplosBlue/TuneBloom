#pragma once

#include <string>

namespace macos
{

std::string getDefaultAppName(const char* extension);
bool isDefaultApp(const char* extension);
bool hasOtherApp(const char* extension);
bool setDefaultToThisApp(const char* extension);
bool setDefaultToOtherApp(const char* extension);

}
