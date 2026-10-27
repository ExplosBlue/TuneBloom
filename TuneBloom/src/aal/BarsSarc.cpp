#include <aal/BarsSarc.h>

#include <aal/AudioAsset.h>

#include <set>
#include <string>

namespace aal
{

static bool EndsWithExtension(const std::string& name, const char* extension)
{
    const size_t length = std::char_traits<char>::length(extension);

    return name.size() > length + 1 && name[name.size() - length - 1] == '.' && name.compare(name.size() - length, length, extension) == 0;
}

static size_t GetStemLength(const std::string& name)
{
    if (EndsWithExtension(name, cAmtaFileExtension))
        return name.size() - std::char_traits<char>::length(cAmtaFileExtension) - 1;

    for (const AudioAssetKind kind : { AudioAssetKind::Bwav, AudioAssetKind::Fwav, AudioAssetKind::StreamPrefetch })
    {
        const char* extension = GetAudioAssetKindExtension(kind);

        if (EndsWithExtension(name, extension))
            return name.size() - std::char_traits<char>::length(extension) - 1;
    }

    return 0;
}

static bool IsBarsSarcName(const std::string& name)
{
    return GetStemLength(name) > 0;
}

static u32 CountEntryNames(const sarc::Index& index, bool (*matches)(const std::string&))
{
    u32 count = 0;

    for (const sarc::IndexEntry& entry : index.entries)
    {
        if (entry.hasName && matches(entry.name))
            count++;
    }

    return count;
}

static bool HasEntryName(const sarc::Index& index, bool (*matches)(const std::string&))
{
    return CountEntryNames(index, matches) > 0;
}

u32 CountBarsSarcAssets(const sarc::Index& index)
{
    std::set<std::string> stems;

    for (const sarc::IndexEntry& entry : index.entries)
    {
        const size_t stemLength = entry.hasName ? GetStemLength(entry.name) : 0;

        if (stemLength > 0)
            stems.insert(entry.name.substr(0, stemLength));
    }

    return static_cast<u32>(stems.size());
}

u32 CountPackResources(const sarc::Index& index)
{
    return CountEntryNames(index, IsPackResourceName);
}

bool IsBarsSarc(const void* data, u64 size)
{
    sarc::Index index;

    return sarc::ReadIndex(data, size, &index) && HasEntryName(index, IsBarsSarcName);
}

bool IsSoundPack(const void* data, u64 size)
{
    sarc::Index index;

    return sarc::ReadIndex(data, size, &index) && IsSoundPackIndex(index);
}

bool IsSoundPackIndex(const sarc::Index& index)
{
    return HasEntryName(index, IsPackResourceName);
}

bool IsPackResourceName(const std::string& name)
{
    return EndsWithExtension(name, cBarsFileExtension);
}

}
