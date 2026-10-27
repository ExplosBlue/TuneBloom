#pragma once

#include <ui/app/PopupMgr.h>
#include <ui/file/Document.h>
#include <ui/items/Selection.h>

#include <chrono>
#include <string>
#include <vector>

class FileWindow;

u64 NewEditGeneration();
u32 NewDocumentSerial();

struct DocumentState
{
    static constexpr size_t cTabCount = static_cast<size_t>(UIType::Max) + 1;
    static constexpr size_t cFilterPaneCount = 2;

    struct SavedItem
    {
        u64 uid;
        Item::ItemType itemType;
        u32 id;
        std::string name;
    };

    struct TabFilter
    {
        sead::FixedSafeString<256> filter;
        bool caseSensitive = false;
    };

    struct ItemLocator
    {
        s32 list = -1;
        s32 index = -1;
    };

    struct TabLocators
    {
        ItemLocator selected;
        ItemLocator anchor;
        std::vector<ItemLocator> multiSelected;
    };

    struct UnloadedLayout
    {
        TabLocators tabs[cTabCount];
        std::vector<ItemLocator> fileWindows;
        ItemLocator shownFile;
    };

    struct Backup
    {
        using Clock = std::chrono::steady_clock;

        bool timerRunning = false;
        Clock::time_point timerStart;
        Clock::time_point lastInput;
        Clock::time_point retryAt;

        bool hasLastBackup = false;
        u64 lastBackupGeneration = 0;
        bool hasFailure = false;
        u64 failedGeneration = 0;
        Bfsar::BackupResult failure = Bfsar::BackupResult::Ok;
        Bfsar::BackupResult noticeShown = Bfsar::BackupResult::Ok;

        std::string untitledKey;
        std::string pinnedDir;
    };

    bool unsaved = false;
    u64 editGeneration = NewEditGeneration();
    std::vector<SavedItem> savedItems;

    UIType tab = UIType::ProjectInfo;
    SelectionState selections[cTabCount];
    Item* scrollItem = nullptr;
    TabFilter filters[cTabCount][cFilterPaneCount];

    ItemList fileWindows;
    FileWindow* shownFileWindow = nullptr;
    Backup backup;

    PopupMgr::ItemErrors itemErrors;

    u32 serial = NewDocumentSerial();
    std::string canonicalPath;
    bool isPreview = false;
    bool reloadFailed = false;
    u64 lastActive = 0;
    u64 memoryEstimate = 0;
    UnloadedLayout unloadedLayout;
};

DocumentState& ActiveDocumentState();
