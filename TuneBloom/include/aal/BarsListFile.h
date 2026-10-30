#pragma once

#include <aal/BarsList.h>
#include <bfsar/model/Item.h>
#include <core/SaveResult.h>

#include <prim/seadSafeString.h>

#include <string>
#include <unordered_map>
#include <vector>

class BarsListEntry : public Item
{
public:
    BarsListEntry();
};

struct FoundBars
{
    std::string path;
    std::string location;
    u64 size = 0;
};

class BarsListFile
{
public:
    BarsListFile();

    bool open(const void* data, u64 size, const sead::SafeString& path);

    const sead::SafeString& getFilePath() const
    {
        return mFilePath;
    }

    const aal::BarsList& getSavedList() const
    {
        return mSaved;
    }

    Item::List& getEntryList()
    {
        return mEntries;
    }

    const Item::List& getEntryList() const
    {
        return mEntries;
    }

    std::string getListName() const;
    bool isModified() const;

    static bool IsValidEntryName(const sead::SafeString& name);
    bool isUniqueEntryName(const sead::SafeString& name, const Item* ignoreItem) const;

    BarsListEntry* createEntry(const sead::SafeString& name) const;
    void renumber();

    void findBarsFiles();
    const FoundBars* findBars(const Item* entry) const;
    u32 countFound() const;

    bool writeTo(const sead::SafeString& path) const;
    SaveResult save(const char** outError);
    SaveResult saveAs(const sead::SafeString& path, const char** outError);

private:
    aal::BarsList makeList(const sead::SafeString& path) const;

    sead::FixedSafeString<512> mFilePath;
    aal::BarsList mSaved;
    Item::List mEntries;
    std::unordered_map<std::string, FoundBars> mFound;
};
