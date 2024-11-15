#pragma once

#include <heap/seadDisposer.h>

#include <vector>
#include <string>

class Item;

class PopupMgr
{
    SEAD_SINGLETON_DISPOSER(PopupMgr);

public:
    struct PopupInfo
    {
        std::string text;
        Item* item;
    };

private:
    PopupMgr();

public:
    void addPopup(const PopupInfo& info);
    void update();

private:
    std::vector<PopupInfo> mPopups;
    bool mPopupOpen;
};
