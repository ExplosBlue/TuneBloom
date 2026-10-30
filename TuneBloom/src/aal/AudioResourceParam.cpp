#include <aal/AudioResource.h>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

using aal::BarsParamRecord;
using aal::BarsParamText;

static bool MakeSarcFileName(const AudioAsset& asset, std::string* out)
{
    const char* extension = GetAudioAssetKindExtension(asset.getInfo().kind);

    if (!asset.getFile() || !extension)
        return false;

    *out = std::string(asset.getName().cstr()) + "." + extension;
    return true;
}

static std::string StripLastExtension(const std::string& name)
{
    const size_t dot = name.rfind('.');

    return dot == std::string::npos ? name : name.substr(0, dot);
}

static std::string MakeParamStreamFileName(const AudioAsset& asset)
{
    return std::string(asset.getName().cstr()) + "." + GetAudioAssetKindExtension(AudioAssetKind::Stream);
}

static void RenameParamRecord(BarsParamRecord* record, const AudioAsset& asset, const std::string& fileName)
{
    const std::string oldStreamName = StripLastExtension(record->text(BarsParamText::File)) + "." + GetAudioAssetKindExtension(AudioAssetKind::Stream);

    record->text(BarsParamText::File) = fileName;

    if (record->text(BarsParamText::StreamFile) == oldStreamName)
        record->text(BarsParamText::StreamFile) = MakeParamStreamFileName(asset);
}

static bool HasSameKind(const BarsParamRecord& record, const std::string& fileName)
{
    return record.text(BarsParamText::File).substr(record.text(BarsParamText::File).rfind('.') + 1) == fileName.substr(fileName.rfind('.') + 1);
}

static std::string MakeSoundName(const std::string& preferred, const std::vector<BarsParamRecord>& records)
{
    const auto isUsed = [&records](const std::string& name)
    {
        return std::any_of(records.begin(), records.end(), [&name](const BarsParamRecord& record) { return record.text(BarsParamText::Sound) == name; });
    };

    std::string name = preferred;

    for (u32 copyNum = 1; isUsed(name); copyNum++)
        name = preferred + "_" + std::to_string(copyNum);

    return name;
}

void AudioResource::openSarcParam(std::vector<sarc::Entry>* otherFiles)
{
    const auto found = std::find_if(otherFiles->begin(), otherFiles->end(), [](const sarc::Entry& entry)
    {
        return entry.hasName && entry.name == aal::cBarsParamFileName;
    });

    SarcParam sarcParam;

    if (found == otherFiles->end() || !aal::ReadBarsParam(found->data, &sarcParam.param))
        return;

    std::unordered_map<std::string, u64> uidsByFileName;

    for (const Item* item : mAssets)
    {
        std::string fileName;

        if (MakeSarcFileName(*static_cast<const AudioAsset*>(item), &fileName))
            uidsByFileName.emplace(fileName, item->getUid());
    }

    for (const BarsParamRecord& record : sarcParam.param.records)
    {
        const auto asset = uidsByFileName.find(record.text(BarsParamText::File));

        if (asset == uidsByFileName.end())
            return;

        sarcParam.assetUids.push_back(asset->second);
        uidsByFileName.erase(asset);
    }

    sarcParam.entry = std::move(*found);
    sarcParam.isPresent = true;
    otherFiles->erase(found);

    mSarcParam = std::move(sarcParam);
}

void AudioResource::buildSarcParam(sarc::File* file) const
{
    if (!mSarcParam.isPresent)
        return;

    const std::vector<BarsParamRecord>& sourceRecords = mSarcParam.param.records;
    std::unordered_map<u64, const AudioAsset*> assetsByUid;

    for (const Item* item : mAssets)
        assetsByUid.emplace(item->getUid(), static_cast<const AudioAsset*>(item));

    aal::BarsParam param;
    param.isBigEndian = mSarcParam.param.isBigEndian;

    std::unordered_set<u64> linkedUids;
    std::unordered_map<const std::vector<u8>*, const BarsParamRecord*> recordsByFile;
    u32 nextId = 0;

    for (u32 i = 0; i < sourceRecords.size(); i++)
    {
        nextId = std::max(nextId, sourceRecords[i].getId() + 1);
        const auto asset = assetsByUid.find(mSarcParam.assetUids[i]);
        std::string fileName;

        if (asset == assetsByUid.end() || !MakeSarcFileName(*asset->second, &fileName))
            continue;

        BarsParamRecord record = sourceRecords[i];
        RenameParamRecord(&record, *asset->second, fileName);

        param.records.push_back(std::move(record));
        linkedUids.insert(asset->first);
        recordsByFile.emplace(asset->second->getFile().get(), &sourceRecords[i]);
    }

    for (const Item* item : mAssets)
    {
        const AudioAsset& asset = *static_cast<const AudioAsset*>(item);
        std::string fileName;

        if (linkedUids.count(item->getUid()) != 0 || !MakeSarcFileName(asset, &fileName) || sourceRecords.empty())
            continue;

        const auto source = recordsByFile.find(asset.getFile().get());
        const auto sameKind = std::find_if(sourceRecords.begin(), sourceRecords.end(), [&fileName](const BarsParamRecord& record) { return HasSameKind(record, fileName); });
        const BarsParamRecord& base = source != recordsByFile.end() ? *source->second : sameKind != sourceRecords.end() ? *sameKind : sourceRecords.front();

        const std::string assetName = asset.getName().cstr();
        const std::string preferredSound = source != recordsByFile.end() ? base.text(BarsParamText::Sound) : assetName.substr(0, assetName.find('.'));

        BarsParamRecord record = base;
        record.text(BarsParamText::Sound) = MakeSoundName(preferredSound, param.records);
        record.text(BarsParamText::File) = fileName;
        record.text(BarsParamText::StreamFile) = base.text(BarsParamText::StreamFile).empty() ? std::string() : MakeParamStreamFileName(asset);
        record.values[0] = nextId++;

        param.records.push_back(std::move(record));
    }

    sarc::Entry entry = mSarcParam.entry;

    if (param.records != sourceRecords)
        entry.data = aal::WriteBarsParam(param);

    file->entries.push_back(std::move(entry));
}
