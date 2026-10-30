#include <aal/BarsSarc.h>

#include <aal/AudioAsset.h>
#include <aal/BarsList.h>
#include <bfsar/model/ArchiveFormat.h>
#include <core/FileName.h>

#include <set>
#include <string>

namespace aal
{

static size_t GetStemLength(const std::string& name)
{
    if (filename::HasExactExtension(name, cAmtaFileExtension))
        return name.size() - std::char_traits<char>::length(cAmtaFileExtension) - 1;

    for (const AudioAssetKind kind : { AudioAssetKind::Bwav, AudioAssetKind::Fwav, AudioAssetKind::StreamPrefetch })
    {
        const char* extension = GetAudioAssetKindExtension(kind);

        if (filename::HasExactExtension(name, extension))
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

    return sarc::ReadIndex(data, size, &index) && (index.entries.empty() || HasEntryName(index, IsBarsSarcName));
}

bool IsSoundPackIndex(const sarc::Index& index)
{
    return HasEntryName(index, IsPackResourceName);
}

static bool IsSoundArchiveName(const std::string& name)
{
    for (ArchiveFormat format : { ArchiveFormat::BFSAR, ArchiveFormat::BCSAR })
    {
        if (filename::HasExactExtension(name, GetInnerFileExtension(format, InnerFileKind::SoundArchive)))
            return true;
    }

    return false;
}

bool IsPackResourceName(const std::string& name)
{
    return filename::HasExactExtension(name, cBarsFileExtension) || filename::HasExactExtension(name, cBarsListFileExtension) || IsSoundArchiveName(name);
}

}
