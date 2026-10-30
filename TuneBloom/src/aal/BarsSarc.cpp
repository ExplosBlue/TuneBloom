#include <aal/BarsSarc.h>

#include <aal/AudioAsset.h>
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

static bool HasBarsSarcName(const sarc::Index& index)
{
    for (const sarc::IndexEntry& entry : index.entries)
    {
        if (entry.hasName && IsBarsSarcName(entry.name))
            return true;
    }

    return false;
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

bool IsBarsSarc(const void* data, u64 size)
{
    sarc::Index index;

    return sarc::ReadIndex(data, size, &index) && (index.entries.empty() || HasBarsSarcName(index));
}

}
