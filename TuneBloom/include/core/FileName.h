#pragma once

#include <cstring>
#include <string>

namespace filename
{

inline constexpr char cInvalidCharacters[] = "/\\:*?\"<>|";

inline bool HasExactExtension(const std::string& name, const char* extension)
{
    const size_t length = std::strlen(extension);

    return name.size() > length + 1 && name[name.size() - length - 1] == '.' && name.compare(name.size() - length, length, extension) == 0;
}

inline bool StripExactExtension(const std::string& name, const char* extension, std::string* outStem)
{
    if (!extension || !HasExactExtension(name, extension))
        return false;

    *outStem = name.substr(0, name.size() - std::strlen(extension) - 1);
    return true;
}

}
