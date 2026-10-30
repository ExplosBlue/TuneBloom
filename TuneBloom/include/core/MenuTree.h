#pragma once

#include <basis/seadTypes.h>

#include <string>
#include <vector>

enum class MenuNodeKind
{
    Menu,
    Item,
    Separator,
};

enum class MenuRole
{
    None,
    About,
    Settings,
    Quit,
    Help,
};

struct MenuNode
{
    MenuNodeKind kind = MenuNodeKind::Item;
    MenuRole role = MenuRole::None;
    u32 id = 0;
    s32 parent = -1;
    std::string label;
    std::string tooltip;
    s32 keyChord = 0;
    bool checked = false;
    bool enabled = true;
    bool clickable = false;
};

struct MenuTree
{
    std::vector<MenuNode> nodes;
};
