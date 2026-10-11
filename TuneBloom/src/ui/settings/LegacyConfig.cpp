#include <ui/settings/LegacyConfig.h>

#include <ui/settings/ConfigFile.h>

#include <bfsar/io/FileIo.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace
{

enum class LegacyType
{
    Bool,
    Int,
    Float,
    String
};

struct LegacyValue
{
    const char* source;
    const char* key;
    LegacyType type;
    const char* target;
    const char* section;
    const char* name;
};

}

static constexpr const char* cThemeFile     = "theme.cfg";
static constexpr const char* cMidiFile      = "midi.cfg";
static constexpr const char* cAudioFile     = "audio.cfg";
static constexpr const char* cMetadataFile  = "metadata.cfg";
static constexpr const char* cSaveFile      = "save.cfg";
static constexpr const char* cBackupFile    = "backup.cfg";
static constexpr const char* cInterfaceFile = "interface.cfg";
static constexpr const char* cStartupFile   = "startup.cfg";
static constexpr const char* cRecentFile    = "recent.cfg";
static constexpr const char* cPinnedFile    = "pinned.cfg";
static constexpr const char* cWarningsFile  = "warnings.cfg";

static constexpr const char* cLegacyFiles[] =
{
    cThemeFile, cMidiFile, cAudioFile, cMetadataFile, cSaveFile, cBackupFile,
    cInterfaceFile, cStartupFile, cRecentFile, cPinnedFile, cWarningsFile
};

static constexpr LegacyValue cLegacyValues[] =
{
    { cAudioFile,     "samplerate",           LegacyType::Int,    cSettingsFile, "audio",     "sampleRate"          },
    { cAudioFile,     "volume",               LegacyType::Float,  cSettingsFile, "audio",     "volume"              },
    { cAudioFile,     "device",               LegacyType::String, cSettingsFile, "audio",     "device"              },
    { cAudioFile,     "mono",                 LegacyType::Bool,   cSettingsFile, "audio",     "mono"                },
    { cAudioFile,     "linear_interpolation", LegacyType::Bool,   cSettingsFile, "audio",     "linearInterpolation" },
    { cMetadataFile,  "save",                 LegacyType::Bool,   cSettingsFile, "archive",   "saveMetadata"        },
    { cSaveFile,      "force_when_in_use",    LegacyType::Bool,   cSettingsFile, "archive",   "forceSaveWhenInUse"  },
    { cBackupFile,    "enabled",              LegacyType::Bool,   cSettingsFile, "backup",    "enabled"             },
    { cBackupFile,    "interval_minutes",     LegacyType::Int,    cSettingsFile, "backup",    "intervalMinutes"     },
    { cBackupFile,    "max_backups",          LegacyType::Int,    cSettingsFile, "backup",    "maxBackups"          },
    { cInterfaceFile, "animations",           LegacyType::Bool,   cSettingsFile, "interface", "animations"          },
    { cInterfaceFile, "smooth_scrolling",     LegacyType::Bool,   cSettingsFile, "interface", "smoothScrolling"     },
    { cInterfaceFile, "window_shadows",       LegacyType::Bool,   cSettingsFile, "interface", "windowShadows"       },
    { cInterfaceFile, "tooltip_delay",        LegacyType::Float,  cSettingsFile, "interface", "tooltipDelay"        },
    { cStartupFile,   "reopen_last_archive",  LegacyType::Bool,   cSettingsFile, "startup",   "reopenLastArchive"   },
    { cStartupFile,   "recent_file_count",    LegacyType::Int,    cSettingsFile, "startup",   "recentFileCount"     },
    { cStartupFile,   "last_archive",         LegacyType::String, cStateFile,    "session",   "lastArchive"         }
};

static bool sLegacyChecked = false;
static bool sHasLegacy = false;

bool HasLegacyConfig()
{
    if (sLegacyChecked)
        return sHasLegacy;

    sLegacyChecked = true;

    for (const char* fileName : cLegacyFiles)
    {
        std::error_code ec;

        if (std::filesystem::is_regular_file(std::filesystem::path(GetConfigPath(fileName)), ec))
            sHasLegacy = true;
    }

    return sHasLegacy;
}

void RemoveLegacyConfig()
{
    for (const char* fileName : cLegacyFiles)
        fileio::RemoveFile(GetConfigPath(fileName).c_str());

    sHasLegacy = false;
}

static bool ReadLines(const char* fileName, std::vector<std::string>* lines)
{
    lines->clear();

    std::vector<u8> data;

    if (!fileio::ReadWholeFile(GetConfigPath(fileName).c_str(), &data))
        return false;

    data.push_back('\n');

    std::string line;

    for (const u8 c : data)
    {
        if (c == '\r')
            continue;

        if (c != '\n')
        {
            line += static_cast<char>(c);
            continue;
        }

        if (!line.empty())
            lines->push_back(line);

        line.clear();
    }

    return true;
}

static const char* FindValue(const std::vector<std::string>& lines, const char* key)
{
    const size_t length = std::strlen(key);

    for (const std::string& line : lines)
    {
        if (line.size() > length && line[length] == '=' && line.compare(0, length, key) == 0)
            return line.c_str() + length + 1;
    }

    return nullptr;
}

static bool IsMissing(const json::Value& root, const char* section, const char* name)
{
    const json::Value* object = root.find(section);

    return !object || !object->find(name);
}

static void MergeLegacyValues(const char* fileName, json::Value* root)
{
    std::vector<std::string> lines;

    for (const LegacyValue& entry : cLegacyValues)
    {
        if (std::strcmp(entry.target, fileName) != 0 || !IsMissing(*root, entry.section, entry.name) || !ReadLines(entry.source, &lines))
            continue;

        const char* text = FindValue(lines, entry.key);

        if (!text)
            continue;

        json::Value& section = root->member(entry.section);

        switch (entry.type)
        {
        case LegacyType::Bool:
            section.setBool(entry.name, std::atoi(text) != 0);
            break;
        case LegacyType::Int:
            section.setInt(entry.name, std::atoi(text));
            break;
        case LegacyType::Float:
            section.setFloat(entry.name, static_cast<f32>(std::atof(text)));
            break;
        case LegacyType::String:
            section.setString(entry.name, text);
            break;
        }
    }
}

static void MergeLegacyTheme(json::Value* root)
{
    static constexpr const char* cSection = "theme";
    static constexpr const char* cNames[] = { "brightness", "saturation", "contrast", "dark", "gradient" };
    static constexpr s32 cAccentCount = 4;
    static constexpr s32 cDarkIndex   = 7;

    std::vector<std::string> lines;

    if (!ReadLines(cThemeFile, &lines) || lines.empty())
        return;

    f32 values[9] = {};
    s32 isDark = 1;

    const s32 count = std::sscanf(lines[0].c_str(), "%f %f %f %f %f %f %f %d %f", &values[0], &values[1], &values[2], &values[3], &values[4], &values[5], &values[6], &isDark, &values[8]);

    if (count >= cAccentCount && IsMissing(*root, cSection, "accent"))
    {
        json::Value& accent = root->member(cSection).setArray("accent");

        for (s32 i = 0; i < cAccentCount; i++)
            accent.pushFloat(values[i]);
    }

    for (s32 i = cAccentCount; i < count; i++)
    {
        const char* name = cNames[i - cAccentCount];

        if (!IsMissing(*root, cSection, name))
            continue;

        if (i == cDarkIndex)
            root->member(cSection).setBool(name, isDark != 0);
        else
            root->member(cSection).setFloat(name, values[i]);
    }
}

static void MergeLegacyList(json::Value* root, const char* source, const char* prefix, const char* section, const char* name)
{
    std::vector<std::string> lines;

    if (!IsMissing(*root, section, name) || !ReadLines(source, &lines))
        return;

    const size_t length = std::strlen(prefix);

    json::Value& list = root->member(section).setArray(name);

    for (const std::string& line : lines)
    {
        if (line.size() > length && line.compare(0, length, prefix) == 0)
            list.pushString(line.c_str() + length);
    }
}

static void MergeLegacyWarnings(json::Value* root)
{
    static constexpr const char* cSection = "ignoredWarnings";

    std::vector<std::string> lines;

    if (root->find(cSection) || !ReadLines(cWarningsFile, &lines))
        return;

    json::Value& archives = root->setArray(cSection);
    json::Value* warnings = nullptr;

    for (const std::string& line : lines)
    {
        if (line.front() == '[' && line.back() == ']')
        {
            json::Value& archive = archives.pushObject();

            archive.setString("archive", line.substr(1, line.size() - 2).c_str());
            warnings = &archive.setArray("warnings");
            continue;
        }

        if (warnings)
            warnings->pushString(line.c_str());
    }
}

void MergeLegacyConfig(const char* fileName, json::Value* root)
{
    if (!HasLegacyConfig())
        return;

    if (std::strcmp(fileName, cSettingsFile) == 0)
    {
        MergeLegacyTheme(root);
        MergeLegacyValues(fileName, root);
        MergeLegacyList(root, cMidiFile, "device=", "midi", "devices");
    }
    else if (std::strcmp(fileName, cStateFile) == 0)
    {
        MergeLegacyValues(fileName, root);
        MergeLegacyList(root, cRecentFile, "", "files", "recent");
        MergeLegacyList(root, cPinnedFile, "", "files", "pinned");
        MergeLegacyWarnings(root);
    }
}
