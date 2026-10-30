#pragma once

#include <heap/seadDisposer.h>

#include <map>
#include <set>
#include <string>

#include <bfsar/model/MessageLog.h>

class PopupMgr
{
    SEAD_SINGLETON_DISPOSER(PopupMgr);

private:
    PopupMgr();

public:
    void update();
    void clearIgnoredWarnings();

    size_t getIgnoredWarningArchiveCount() const
    {
        return mIgnoredWarnings.size();
    }

private:
    void updateMessage();
    void updateErrors();

    void loadIgnoredWarnings();
    void saveIgnoredWarnings() const;
    void removeIgnoredWarnings(MessageLog::ItemErrors* errors);
    void ignoreProcessedWarnings(const MessageLog::ItemErrors& errors);

    bool mPopupOpen;
    std::map<std::string, std::set<std::string>> mIgnoredWarnings;
};
