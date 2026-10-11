#include <ui/export/ExportInternal.h>

#include <ui/Messages.h>

#include <bfsar/model/Sound.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <string>

void SanitizeFilenameInPlace(sead::BufferedSafeString *s)
{
    for (s32 i = 0; i < s->calcLength(); i++)
    {
        char &c = s->getBuffer()[i];
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' || (unsigned char)c < 0x20)
            c = '_';
    }
}

static void ResolveSoundExportName(const Sound* sound, sead::BufferedSafeString* out)
{
    if (sound->getName().isEmpty())
        out->format(messages::exporting::cFallbackSoundFormat, sound->getId());
    else
        out->format("%s", sound->getName().cstr());
}

void BuildDefaultExportPath(sead::BufferedSafeString* outPath, const Sound* sound, const char* ext)
{
    sead::FixedSafeString<256> name;
    ResolveSoundExportName(sound, &name);
    SanitizeFilenameInPlace(&name);

    std::string cwd = std::filesystem::current_path().string();
    outPath->format("%s/%s.%s", cwd.c_str(), name.cstr(), ext);
}

static std::vector<std::string> sExportUsedNames;

void BeginExportNameBatch()
{
    sExportUsedNames.clear();
}

static bool ClaimExportName(const std::string& key)
{
    if (std::find(sExportUsedNames.begin(), sExportUsedNames.end(), key) != sExportUsedNames.end())
        return false;

    sExportUsedNames.push_back(key);

    return true;
}

static std::string MakeExportNameKey(const sead::SafeString &name, const char *ext)
{
    std::string key = name.cstr();
    key += '.';
    key += ext;

    for (char &c : key)
        c = (char)::tolower((unsigned char)c);

    return key;
}

void BuildUniqueExportPath(sead::BufferedSafeString *outPath, const char *dir, const sead::SafeString &rawName, const char *ext)
{
    sead::FixedSafeString<256> name;
    name.format("%s", rawName.cstr());
    SanitizeFilenameInPlace(&name);

    sead::FixedSafeString<256> uniqueName = name;
    for (u32 copyNum = 2; !ClaimExportName(MakeExportNameKey(uniqueName, ext)); copyNum++)
        uniqueName.format("%s_%u", name.cstr(), copyNum);

    outPath->format("%s/%s.%s", dir, uniqueName.cstr(), ext);
}

void BuildExportPathFromDir(sead::BufferedSafeString *outPath, const char *dir, const Sound *sound, const char *ext)
{
    sead::FixedSafeString<256> name;
    ResolveSoundExportName(sound, &name);

    BuildUniqueExportPath(outPath, dir, name, ext);
}

void BuildTrackExportPath(sead::BufferedSafeString* outPath, const char* path, u32 trackNo)
{
    const char* dot = strrchr(path, '.');

    if (dot)
        outPath->format("%.*s_track%u%s", static_cast<s32>(dot - path), path, trackNo, dot);
    else
        outPath->format("%s_track%u.wav", path, trackNo);
}

void ResolveExportBaseName(const Item *item, const char *fallbackLabel, sead::BufferedSafeString *out)
{
    if (item->isNameValid())
    {
        const char *name = item->getName().cstr();
        if (strncmp(name, "GUESS_", 6) == 0)
            name += 6;

        if (*name != '\0')
        {
            out->format("%s", name);
            return;
        }
    }

    out->format("%s %u", fallbackLabel, item->getId());
}

void BuildItemDefaultExportPath(const Item* item, const char* fallbackLabel, const char* ext, sead::BufferedSafeString* out)
{
    sead::FixedSafeString<256> baseName;
    ResolveExportBaseName(item, fallbackLabel, &baseName);
    SanitizeFilenameInPlace(&baseName);

    std::string cwd = std::filesystem::current_path().string();
    out->format("%s/%s.%s", cwd.c_str(), baseName.cstr(), ext);
}

void ReplacePathExtension(sead::BufferedSafeString* path, const char* ext)
{
    const s32 dot = path->rfindIndex(".");

    if (dot >= 0)
        path->trim(dot);

    path->append(ext);
}
