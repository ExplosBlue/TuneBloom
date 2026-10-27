#pragma once

#include <basis/seadTypes.h>

#include <string>

namespace fileassoc
{

enum class Lock
{
    None,
    OnlyApp,
    UserChoice
};

struct TypeState
{
    const char* extension = nullptr;
    std::string typeName;
    std::string appName;
    bool isOn = false;
    Lock lock = Lock::None;
};

bool IsSupported();
u32 GetTypeCount();
TypeState GetTypeState(u32 index);
bool SetTypeOn(u32 index, bool on);

}
