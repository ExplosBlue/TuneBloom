#pragma once

namespace fileassoc
{

enum class State
{
    None,
    ThisCopy,
    OtherCopy
};

bool IsSupported();
State GetState();
bool Register();
bool Remove();

}
