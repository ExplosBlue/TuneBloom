#include <bfsar/archive/GroupFilePaths.h>

#include <ui/settings/ConfigFile.h>

#include <libs/md5.h>

#include <filesystem>
#include <vector>

namespace groupfilepaths
{

static constexpr const char* cConfigSection = "groupFiles";

namespace
{

struct Entry
{
    std::string key;
    std::string path;
    bool isSessionOnly = false;
};

}

static std::vector<Entry>& GetSessionEntries()
{
    static std::vector<Entry> sEntries;

    return sEntries;
}

static std::string MakeKey(const sead::SafeString& archivePath, const sead::SafeString& groupName)
{
    const std::string archive = std::filesystem::path(archivePath.cstr()).lexically_normal().generic_string();

    return md5(archive + "\n" + groupName.cstr());
}

static Entry* FindSessionEntry(const std::string& key)
{
    for (Entry& entry : GetSessionEntries())
    {
        if (entry.key == key)
            return &entry;
    }

    return nullptr;
}

static Entry& SetSessionEntry(const std::string& key, const std::string& path)
{
    if (Entry* entry = FindSessionEntry(key))
    {
        entry->path = path;
        return *entry;
    }

    GetSessionEntries().push_back({ key, path });

    return GetSessionEntries().back();
}

static void Store(const std::string& key, const std::string& path)
{
    json::Value section;
    LoadConfigSection(cStateFile, cConfigSection, &section);

    const std::string* stored = section.findString(key.c_str());

    if (stored && path == *stored)
        return;

    section.setString(key.c_str(), path.c_str());
    SaveConfigSection(cStateFile, cConfigSection, section);
}

void Select(const sead::SafeString& archivePath, const sead::SafeString& groupName, const std::string& path, bool keep)
{
    const std::string key = MakeKey(archivePath, groupName);

    SetSessionEntry(key, path).isSessionOnly = !keep;

    if (keep)
        Store(key, path);
}

void Remember(const sead::SafeString& archivePath, const sead::SafeString& groupName, const std::string& path, bool persist)
{
    const std::string key = MakeKey(archivePath, groupName);

    if (!SetSessionEntry(key, path).isSessionOnly && persist)
        Store(key, path);
}

std::string Find(const sead::SafeString& archivePath, const sead::SafeString& groupName, bool persisted)
{
    const std::string key = MakeKey(archivePath, groupName);

    if (const Entry* entry = FindSessionEntry(key))
        return entry->path;

    if (!persisted)
        return std::string();

    json::Value section;

    if (!LoadConfigSection(cStateFile, cConfigSection, &section))
        return std::string();

    const std::string* stored = section.findString(key.c_str());

    return stored ? *stored : std::string();
}

}
