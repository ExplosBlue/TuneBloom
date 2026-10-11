#include <ui/settings/ConfigFile.h>

#include <ui/settings/LegacyConfig.h>
#include <core/TestHooks.h>

#include <bfsar/io/FileIo.h>

#include <cstdlib>
#include <filesystem>
#include <vector>

static constexpr const char* cVersionKey   = "version";
static constexpr const char* cBrokenSuffix = ".bak";

static constexpr s64 cVersion = 1;

std::string GetConfigDir()
{
#if defined(SEAD_PLATFORM_WINDOWS)
    const char* appdata = getenv("APPDATA");
    if (appdata && appdata[0])
        return std::string(appdata) + "\\TuneBloom";
    return ".\\TuneBloom";
#elif defined(SEAD_PLATFORM_LINUX)
    const char* xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0])
        return std::string(xdg) + "/TuneBloom";
    const char* home = getenv("HOME");
    if (home && home[0])
        return std::string(home) + "/.config/TuneBloom";
    return "./.config/TuneBloom";
#else
    const char* home = getenv("HOME");
    if (home && home[0])
        return std::string(home) + "/Library/Application Support/TuneBloom";
    return "./TuneBloom";
#endif
}

std::string GetConfigPath(const char* fileName)
{
    return GetConfigDir() + "/" + fileName;
}

bool WriteTextFileAtomic(const std::string& path, const std::string& text)
{
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);

    return fileio::WriteWholeFileAtomic(path.c_str(), text.data(), text.size());
}

static bool ReadConfigRoot(const char* fileName, json::Value* root, bool* isBroken)
{
    const std::filesystem::path path(GetConfigPath(fileName));

    *root = json::Value();
    *isBroken = false;

    std::error_code ec;
    const bool hasFile = std::filesystem::is_regular_file(path, ec) && std::filesystem::file_size(path, ec) != 0;

    if (hasFile)
    {
        std::vector<u8> data;

        if (!fileio::ReadWholeFile(path.string().c_str(), &data))
            return false;

        if (!json::Parse(reinterpret_cast<const char*>(data.data()), data.size(), root) || !root->isObject())
        {
            *root = json::Value();
            *isBroken = true;
        }
    }

    if (!root->find(cVersionKey))
        root->setInt(cVersionKey, cVersion);

    MergeLegacyConfig(fileName, root);

    return true;
}

static bool WriteConfigRoot(const char* fileName, const char* name, const json::Value* section)
{
    json::Value root;
    bool isBroken;

    if (!ReadConfigRoot(fileName, &root, &isBroken))
        return false;

    const std::string path = GetConfigPath(fileName);

    if (isBroken)
        fileio::CopyFileOver(path.c_str(), (path + cBrokenSuffix).c_str());

    if (section)
        root.member(name) = *section;

    std::string text;
    json::Write(root, &text);

    return WriteTextFileAtomic(path, text);
}

bool LoadConfigSection(const char* fileName, const char* name, json::Value* section)
{
    json::Value root;
    bool isBroken;

    ReadConfigRoot(fileName, &root, &isBroken);

    const json::Value* found = root.find(name);

    *section = found ? *found : json::Value();

    return found != nullptr;
}

bool SaveConfigSection(const char* fileName, const char* name, const json::Value& section)
{
    return WriteConfigRoot(fileName, name, &section);
}

void MigrateLegacyConfig()
{
    if (testhooks::IsEnabled() || !HasLegacyConfig())
        return;

    for (const char* fileName : { cSettingsFile, cStateFile })
    {
        if (!WriteConfigRoot(fileName, nullptr, nullptr))
            return;
    }

    RemoveLegacyConfig();
}
