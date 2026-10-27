#include <aal/AudioResource.h>

#include <aal/Amta.h>
#include <bfsar/audio/AudioProcessing.h>
#include <aal/Bars.h>
#include <aal/BarsSarc.h>
#include <aal/Bfstm.h>
#include <bfsar/files/BfstpFile.h>
#include <aal/Bwav.h>
#include <bfsar/archive/Cmpbin.h>
#include <bfsar/audio/DecodedPcm.h>
#include <bfsar/io/FileIo.h>
#include <bfsar/io/PackFile.h>
#include <ui/Messages.h>

#include <stream/seadFileDeviceStream.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <unordered_map>

namespace fs = std::filesystem;

static constexpr char cInvalidNameCharacters[] = "/\\:*?\"<>|";
static constexpr char cFirstNameCharacter      = '!';
static constexpr char cLastNameCharacter       = '~';
static constexpr s32 cLoadedNameLengthMax      = 255;
static constexpr char cStagedFileSuffix[]      = ".savetmp";

static AudioAssetFile MakeAssetFile(std::vector<u8> bytes)
{
    return std::make_shared<const std::vector<u8>>(std::move(bytes));
}

static void UpdateMetaLoudness(AudioAsset* asset, std::vector<u8>* meta)
{
    AudioAssetPcm pcm;

    if (!aal::HasAmtaLoudness(*meta) || !asset->decodePcm16(&pcm))
        return;

    const bool isAmta5 = aal::GetAmtaVersion(*meta) >= aal::cAmtaVersion5;
    const bool hasIntegerMeanSquare = asset->getInfo().kind != AudioAssetKind::Bwav;
    const aal::AmtaLoudness loudness = isAmta5 ? aal::MeasureAmta5Loudness(pcm.channels, pcm.sampleRate, pcm.isLoop, pcm.loopStartFrame, false)
                                               : aal::MeasureAmta4Loudness(pcm.channels, pcm.sampleRate, hasIntegerMeanSquare);

    aal::WriteAmtaLoudness(meta, loudness);
}

static void UpdateMetaFromInfo(AudioAsset* asset)
{
    const AudioAssetInfo& info = asset->getInfo();
    std::vector<u8> meta = asset->getMeta();

    if (meta.empty())
        return;

    aal::AmtaWaveFields fields;
    fields.sampleRate = info.sampleRate;
    fields.storedSampleCount = info.kind == AudioAssetKind::Bwav ? info.sampleCount : info.storedSampleCount;
    fields.sampleCount = info.sampleCount;
    fields.channelCount = info.channelCount;
    fields.isLoop = info.isLoop;
    fields.loopStartFrame = info.loopStartFrame;

    if (!aal::UpdateAmtaWaveFields(&meta, fields))
        return;

    UpdateMetaLoudness(asset, &meta);
    asset->setMeta(std::move(meta));
}

static bool WriteWaveFileBytes(const WaveFile& wave, std::vector<u8>* out)
{
    const sead::FormatFixedSafeString<fileio::cPathLengthMax> tempPath("%s/tunebloom_resource_wave_%p.bin", fileio::GetTempFolder(), static_cast<const void*>(&wave));
    const InnerFileTarget target = wave.getTarget();

    {
        sead::FileHandle handle;

        if (!fileio::OpenForWrite(&handle, tempPath))
            return false;

        {
            sead::FileDeviceWriteStream stream(&handle, sead::Stream::Modes::eBinary);
            wave.setup(target.endian);
            wave.write(&handle, &stream, target.endian, true);
        }

        handle.close();
    }

    const bool isRead = fileio::ReadWholeFile(tempPath.cstr(), out);
    fileio::RemoveFile(tempPath.cstr());

    return isRead && !out->empty();
}

static bool ReadWavPcm(const WaveFile::RiffWaveInfo& info, std::vector<std::vector<s16>>* out, u32* outLoopStartFrame, const char** outError)
{
    std::vector<std::vector<float>> channels;

    if (!decodeRiffChannels(info, &channels))
    {
        *outError = messages::import::cWaveReadFailed;
        return false;
    }

    const u32 sampleCount = info.isLoop ? std::min(info.loopEndFrame, info.sampleCount) : info.sampleCount;
    *outLoopStartFrame = info.isLoop ? info.loopStartFrame : 0;

    if (sampleCount == 0 || (info.isLoop && *outLoopStartFrame >= sampleCount))
    {
        *outError = messages::wavefile::cInvalidLoopRange;
        return false;
    }

    out->assign(std::min<size_t>(channels.size(), snd::cWaveChannelMax), std::vector<s16>(sampleCount, 0));

    for (u32 i = 0; i < out->size(); i++)
    {
        for (u32 j = 0; j < sampleCount; j++)
            (*out)[i][j] = AudioProcessing::toPcm16(channels[i][j]);
    }

    return true;
}

static void SetStreamAudio(AudioAsset* asset, const aal::Bwav& full)
{
    aal::Bwav current;
    ReadAudioAssetBwav(asset->getFile(), &current);

    const u32 prefetchSamples = current.channels.empty() ? aal::cBwavPrefetchSampleCount : current.channels.front().sampleCount;
    const u16 prefetchFlag = current.channels.empty() ? 1 : current.prefetchFlag;

    asset->setStreamFile(MakeAssetFile(aal::WriteBwav(full)), StreamFileState::Loaded, true);
    asset->setFile(MakeAssetFile(aal::WriteBwav(aal::MakeBwavPrefetch(full, prefetchSamples, prefetchFlag))));
}

static void SetStreamLoop(AudioAsset* asset, const aal::Bwav& full)
{
    aal::Bwav prefetch;
    ReadAudioAssetBwav(asset->getFile(), &prefetch);
    aal::CopyBwavLoop(full, &prefetch);

    asset->setStreamFile(MakeAssetFile(aal::WriteBwav(full)), StreamFileState::Loaded, true);
    asset->setFile(MakeAssetFile(aal::WriteBwav(prefetch)));
}

static bool DoesStreamMatchPrefetch(const aal::Bwav& stream, const aal::Bwav& prefetch)
{
    if (stream.channels.size() != prefetch.channels.size() || stream.sampleCrc != prefetch.sampleCrc)
        return false;

    for (u32 i = 0; i < stream.channels.size(); i++)
    {
        if (stream.channels[i].sampleRate != prefetch.channels[i].sampleRate || stream.channels[i].sampleCount != prefetch.channels[i].fullSampleCount)
            return false;
    }

    return true;
}

static bool SetBfstmStreamAudio(AudioAsset* asset, std::vector<u8> stream, const char** outError)
{
    const std::vector<u8>& current = *asset->getFile();
    std::vector<u8> prefetch;

    if (!BfstpFile::Rebuild(&prefetch, stream.data(), stream.size(), current.data(), current.size()))
    {
        *outError = messages::resource::cPrefetchBuildFailed;
        return false;
    }

    asset->setStreamFile(MakeAssetFile(std::move(stream)), StreamFileState::Loaded, true);
    asset->setFile(MakeAssetFile(std::move(prefetch)));

    return true;
}

static StreamFileState CheckBfstmStreamFile(const AudioAsset& asset, const std::vector<u8>& bytes)
{
    aal::Bfstm stream;
    const char* error = nullptr;

    if (!aal::ReadBfstm(bytes.data(), bytes.size(), &stream, &error))
        return StreamFileState::Damaged;

    const std::vector<u8>& current = *asset.getFile();
    std::vector<u8> prefetch;

    return BfstpFile::Rebuild(&prefetch, bytes.data(), bytes.size(), current.data(), current.size()) && prefetch == current ? StreamFileState::Loaded : StreamFileState::Mismatched;
}

static StreamFileState CheckBwavStreamFile(const AudioAsset& asset, const std::vector<u8>& bytes)
{
    aal::Bwav stream;
    aal::Bwav prefetch;
    const char* error = nullptr;

    if (!aal::ReadBwav(bytes.data(), bytes.size(), &stream, &error) || !aal::IsBwavComplete(stream))
        return StreamFileState::Damaged;

    return ReadAudioAssetBwav(asset.getFile(), &prefetch) && DoesStreamMatchPrefetch(stream, prefetch) ? StreamFileState::Loaded : StreamFileState::Mismatched;
}

static StreamFileState CheckStreamFile(const AudioAsset& asset, const std::vector<u8>& bytes)
{
    if (!asset.getFile())
        return StreamFileState::Mismatched;

    return asset.getInfo().kind == AudioAssetKind::StreamPrefetch ? CheckBfstmStreamFile(asset, bytes) : CheckBwavStreamFile(asset, bytes);
}

static const char* GetStreamFileExtension(const AudioAsset& asset)
{
    return GetAudioAssetKindExtension(asset.getInfo().kind == AudioAssetKind::StreamPrefetch ? AudioAssetKind::Stream : AudioAssetKind::Bwav);
}

static fs::path MakeStreamRelativePath(const AudioAsset& asset)
{
    return fs::path(cResourceStreamFolder) / (std::string(asset.getName().cstr()) + "." + GetStreamFileExtension(asset));
}

static std::string MakeStreamFilePath(const sead::SafeString& archivePath, const AudioAsset& asset)
{
    return (fs::path(packfile::GetFolderOnDisk(archivePath.cstr())) / MakeStreamRelativePath(asset)).string();
}

static bool IsSameFolder(const sead::SafeString& left, const sead::SafeString& right)
{
    std::error_code error;
    const fs::path leftFolder = fs::absolute(left.cstr(), error).lexically_normal().parent_path();
    const fs::path rightFolder = fs::absolute(right.cstr(), error).lexically_normal().parent_path();

    return leftFolder == rightFolder;
}

static bool StripExtension(const std::string& name, const char* extension, std::string* outStem)
{
    if (!extension)
        return false;

    const size_t length = std::strlen(extension);

    if (name.size() <= length + 1 || name[name.size() - length - 1] != '.' || name.compare(name.size() - length, length, extension) != 0)
        return false;

    *outStem = name.substr(0, name.size() - length - 1);
    return true;
}

static bool ReadSarcAudioName(const sarc::Entry& entry, std::string* outName)
{
    const AudioAssetKind kind = DetectAudioAssetKind(entry.data);
    const bool isAsset = kind == AudioAssetKind::Bwav || kind == AudioAssetKind::Fwav || kind == AudioAssetKind::StreamPrefetch;

    return entry.hasName && isAsset && StripExtension(entry.name, GetAudioAssetKindExtension(kind), outName);
}

static bool AttachSarcMeta(const AudioResource& resource, sarc::Entry* entry, std::vector<AudioAsset*>* assets, std::unordered_map<std::string, AudioAsset*>* assetsByName)
{
    std::string name;
    std::string metaName;

    if (!entry->hasName || !StripExtension(entry->name, aal::cAmtaFileExtension, &name) || !resource.validName(sead::SafeString(name.c_str())))
        return false;

    if (!aal::ReadAmtaName(entry->data, &metaName) || metaName != name)
        return false;

    const auto found = assetsByName->find(name);

    if (found != assetsByName->end() && !found->second->getMeta().empty())
        return false;

    AudioAsset* asset = found != assetsByName->end() ? found->second : new AudioAsset();

    if (found == assetsByName->end())
    {
        asset->getName() = name.c_str();
        assets->push_back(asset);
        assetsByName->emplace(name, asset);
    }

    asset->setMeta(std::move(entry->data));
    return true;
}

static void AddAssetsByName(Item::List* list, std::vector<AudioAsset*> assets)
{
    std::sort(assets.begin(), assets.end(), [](const AudioAsset* left, const AudioAsset* right)
    {
        return std::strcmp(left->getName().cstr(), right->getName().cstr()) < 0;
    });

    for (AudioAsset* asset : assets)
        list->pushBack(asset);
}

static bool MakeEntryMeta(const AudioAsset& asset, std::vector<u8>* out, const char** outError)
{
    std::string metaName;
    *out = asset.getMeta();

    if (aal::ReadAmtaName(*out, &metaName) && metaName != asset.getName().cstr() && !aal::RenameAmta(out, asset.getName().cstr()))
    {
        *outError = messages::resource::cRenameFailed;
        return false;
    }

    return true;
}

static void RemoveStagedFiles(const std::vector<std::string>& paths)
{
    for (const std::string& path : paths)
        fileio::RemoveFile(path.c_str());
}

AudioResource::AudioResource()
    : mIsOpen(false)
    , mContainer(Container::Bars)
    , mIsCompressed(false)
    , mIsBigEndian(false)
    , mVersion(0)
    , mTrailing()
    , mFilePath()
    , mAssets()
{
    mAssets.setResource(this);
}

AudioResource::~AudioResource()
{
    close();
}

bool AudioResource::IsResourceFile(const void* data, u64 size)
{
    return aal::IsBars(data, size) || aal::IsBwav(data, size) || aal::IsBarsSarc(data, size);
}

const char* AudioResource::GetResourceDataError(const void* data, u64 size)
{
    if (!cmpbin::IsZstdFrame(data, size))
        return IsResourceFile(data, size) ? nullptr : messages::resource::cNotResource;

    std::vector<u8> plain;

    if (!cmpbin::DecompressZstdFrame(data, size, plain))
        return messages::resource::cDecompressFailed;

    return IsResourceFile(plain.data(), plain.size()) ? nullptr : messages::resource::cZstdNotResource;
}

bool AudioResource::open(std::vector<u8> bytes, const sead::SafeString& path, const char** outError)
{
    close();

    mIsCompressed = cmpbin::IsZstdFrame(bytes.data(), bytes.size());

    if (mIsCompressed)
    {
        std::vector<u8> plain;

        if (!cmpbin::DecompressZstdFrame(bytes.data(), bytes.size(), plain))
        {
            *outError = messages::resource::cDecompressFailed;
            return false;
        }

        bytes = std::move(plain);
    }

    bool isOpened = false;

    if (aal::IsBars(bytes.data(), bytes.size()))
        isOpened = openBars(std::move(bytes), outError);
    else if (aal::IsBwav(bytes.data(), bytes.size()))
        isOpened = openBwav(std::move(bytes), path, outError);
    else if (aal::IsBarsSarc(bytes.data(), bytes.size()))
        isOpened = openSarc(std::move(bytes), outError);
    else
        *outError = messages::resource::cNotResource;

    if (!isOpened)
    {
        close();
        return false;
    }

    mFilePath = path;
    mIsOpen = true;

    renumberAssets();

    for (Item* item : mAssets)
        probeStreamFile(static_cast<AudioAsset*>(item));

    return true;
}

bool AudioResource::openBars(std::vector<u8> bytes, const char** outError)
{
    aal::BarsFile bars;

    if (!aal::ReadBars(bytes.data(), bytes.size(), &bars, outError))
        return false;

    mContainer = Container::Bars;
    mIsBigEndian = bars.isBigEndian;
    mVersion = bars.version;
    mTrailing = std::move(bars.trailing);

    std::vector<AudioAssetFile> files;
    files.reserve(bars.assets.size());

    for (std::vector<u8>& asset : bars.assets)
        files.push_back(MakeAssetFile(std::move(asset)));

    std::vector<AudioAsset*> assets;
    assets.reserve(bars.entries.size());

    for (aal::BarsEntry& entry : bars.entries)
    {
        std::string name;

        if (!aal::ReadAmtaName(entry.meta, &name) || name.size() > cLoadedNameLengthMax)
        {
            for (AudioAsset* asset : assets)
                delete asset;

            *outError = messages::resource::cAmtaDamaged;
            return false;
        }

        AudioAsset* asset = new AudioAsset();
        asset->getName() = name.c_str();
        asset->setMeta(std::move(entry.meta));
        asset->setInResetTable(entry.isInResetTable);

        if (entry.assetIndex != aal::cBarsNoAsset)
            asset->setFile(files[entry.assetIndex]);

        assets.push_back(asset);
    }

    AddAssetsByName(&mAssets, std::move(assets));
    return true;
}

bool AudioResource::openSarc(std::vector<u8> bytes, const char** outError)
{
    sarc::File file;

    if (!sarc::Read(bytes.data(), bytes.size(), &file))
    {
        *outError = messages::resource::cSarcDamaged;
        return false;
    }

    std::vector<AudioAsset*> assets;
    std::unordered_map<std::string, AudioAsset*> assetsByName;
    std::vector<sarc::Entry*> remaining;

    for (sarc::Entry& entry : file.entries)
    {
        std::string name;

        if (!ReadSarcAudioName(entry, &name) || !validName(sead::SafeString(name.c_str())) || assetsByName.count(name) != 0)
        {
            remaining.push_back(&entry);
            continue;
        }

        AudioAsset* asset = new AudioAsset();
        asset->getName() = name.c_str();
        asset->setFile(MakeAssetFile(std::move(entry.data)));

        assets.push_back(asset);
        assetsByName.emplace(name, asset);
    }

    std::vector<sarc::Entry> otherFiles;

    for (sarc::Entry* entry : remaining)
    {
        if (!AttachSarcMeta(*this, entry, &assets, &assetsByName))
            otherFiles.push_back(std::move(*entry));
    }

    mContainer = Container::Sarc;
    mIsBigEndian = file.isBigEndian;
    mVersion = file.version;

    file.entries = std::move(otherFiles);
    mSarcFiles = std::move(file);

    AddAssetsByName(&mAssets, std::move(assets));
    return true;
}

bool AudioResource::openBwav(std::vector<u8> bytes, const sead::SafeString& path, const char** outError)
{
    aal::Bwav bwav;

    if (!aal::ReadBwav(bytes.data(), bytes.size(), &bwav, outError))
        return false;

    std::string name = fs::path(path.cstr()).filename().string();

    for (const char* extension : { cZstdExtension, GetResourceFormatInfo(ResourceFormat::Bwav).extension })
    {
        if (HasFileExtension(name.c_str(), name.size(), extension))
            name.resize(name.size() - std::strlen(extension) - 1);
    }

    name.resize(std::min<size_t>(name.size(), cLoadedNameLengthMax));

    mContainer = Container::Bwav;
    mIsBigEndian = bwav.isBigEndian;
    mVersion = bwav.version;

    AudioAsset* asset = new AudioAsset();
    asset->getName() = name.c_str();
    asset->setFile(MakeAssetFile(std::move(bytes)));
    mAssets.pushBack(asset);

    return true;
}

void AudioResource::renumberAssets()
{
    u32 id = 0;

    for (Item* item : mAssets)
        item->setId(id++);
}

void AudioResource::close()
{
    mAssets.clear();
    mTrailing.clear();
    mSarcFiles = sarc::File();
    mFilePath.clear();
    mIsOpen = false;
    mIsCompressed = false;
    mIsBigEndian = false;
    mVersion = 0;
}

bool AudioResource::isOpen() const
{
    return mIsOpen;
}

const sead::SafeString& AudioResource::getFilePath() const
{
    return mFilePath;
}

AudioResource::Container AudioResource::getContainer() const
{
    return mContainer;
}

ResourceFormat AudioResource::getFormat() const
{
    return mContainer == Container::Bwav ? ResourceFormat::Bwav : ResourceFormat::Bars;
}

bool AudioResource::isCompressed() const
{
    return mIsCompressed;
}

bool AudioResource::isBigEndian() const
{
    return mIsBigEndian;
}

u16 AudioResource::getVersion() const
{
    return mVersion;
}

u32 AudioResource::getOtherFileCount() const
{
    return static_cast<u32>(mSarcFiles.entries.size());
}

Item::List& AudioResource::getAssetList()
{
    return mAssets;
}

const Item::List& AudioResource::getAssetList() const
{
    return mAssets;
}

bool AudioResource::validName(const sead::SafeString& name) const
{
    const s32 length = name.calcLength();

    if (length == 0 || length > cNameLengthMax)
        return false;

    for (s32 i = 0; i < length; i++)
    {
        const char character = name[i];

        if (character < cFirstNameCharacter || character > cLastNameCharacter || std::strchr(cInvalidNameCharacters, character))
            return false;
    }

    return true;
}

bool AudioResource::validateName(const sead::SafeString& name, const Item* ignoreItem) const
{
    for (const Item* asset : mAssets)
    {
        if (asset != ignoreItem && name == asset->getName())
            return false;
    }

    return true;
}

bool AudioResource::validate(sead::BufferedSafeString* outError, const Item** outItem) const
{
    *outItem = nullptr;

    if (mContainer == Container::Bwav && (mAssets.isEmpty() || static_cast<const AudioAsset*>(mAssets.front())->getInfo().kind != AudioAssetKind::Bwav))
    {
        outError->copy(messages::resource::cBwavNeedsAudio);
        return false;
    }

    std::unordered_map<u32, const Item*> hashes;

    for (const Item* item : mAssets)
    {
        const AudioAsset* asset = static_cast<const AudioAsset*>(item);
        *outItem = asset;

        if (asset->validate(*outError))
            return false;

        if (mContainer == Container::Bwav)
            continue;

        if (!validName(asset->getName()))
        {
            outError->copy(messages::item::cNameInvalidDetail);
            return false;
        }

        if (!validateName(asset->getName(), asset))
        {
            outError->copy(messages::item::cNameDuplicated);
            return false;
        }

        std::string metaName;
        std::vector<u8> meta = asset->getMeta();

        if (mContainer == Container::Sarc && meta.empty())
            continue;

        if (meta.empty() || !aal::ReadAmtaName(meta, &metaName))
        {
            outError->copy(messages::resource::cAmtaDamaged);
            return false;
        }

        if (metaName != asset->getName().cstr() && !aal::RenameAmta(&meta, asset->getName().cstr()))
        {
            outError->copy(messages::resource::cRenameFailed);
            return false;
        }

        if (mContainer == Container::Bars && !hashes.emplace(aal::HashBarsName(asset->getName().cstr()), asset).second)
        {
            outError->copy(messages::resource::cNameHashCollision);
            return false;
        }
    }

    *outItem = nullptr;
    return true;
}

void AudioResource::captureItemBaselines()
{
    for (Item* asset : mAssets)
        asset->captureBaseline();
}

void AudioResource::syncMetaNames()
{
    for (Item* item : mAssets)
    {
        AudioAsset* asset = static_cast<AudioAsset*>(item);
        std::string name;

        if (asset->getMeta().empty() || (aal::ReadAmtaName(asset->getMeta(), &name) && name == asset->getName().cstr()))
            continue;

        std::vector<u8> meta = asset->getMeta();

        if (aal::RenameAmta(&meta, asset->getName().cstr()))
            asset->setMeta(std::move(meta));
    }
}

bool AudioResource::buildFile(std::vector<u8>* out, const char** outError) const
{
    if (mContainer == Container::Bwav)
    {
        const AudioAsset* asset = mAssets.isEmpty() ? nullptr : static_cast<const AudioAsset*>(mAssets.front());

        if (!asset || !asset->getFile())
        {
            *outError = messages::resource::cBwavNeedsAudio;
            return false;
        }

        *out = *asset->getFile();
        return true;
    }

    return mContainer == Container::Sarc ? buildSarc(out, outError) : buildBars(out, outError);
}

bool AudioResource::buildSarc(std::vector<u8>* out, const char** outError) const
{
    sarc::File file = mSarcFiles;

    for (const Item* item : mAssets)
    {
        const AudioAsset* asset = static_cast<const AudioAsset*>(item);
        const std::string name = asset->getName().cstr();
        std::vector<u8> meta;

        if (!MakeEntryMeta(*asset, &meta, outError))
            return false;

        if (const std::vector<u8>* data = asset->getFile().get())
        {
            const char* extension = GetAudioAssetKindExtension(asset->getInfo().kind);

            if (!extension)
            {
                *outError = messages::resource::cAudioDamaged;
                return false;
            }

            sarc::Entry entry;
            entry.name = name + "." + extension;
            entry.data = *data;
            file.entries.push_back(std::move(entry));
        }

        if (meta.empty())
            continue;

        sarc::Entry entry;
        entry.name = name + "." + aal::cAmtaFileExtension;
        entry.data = std::move(meta);
        file.entries.push_back(std::move(entry));
    }

    *out = sarc::Write(file);
    return true;
}

bool AudioResource::buildBars(std::vector<u8>* out, const char** outError) const
{
    aal::BarsFile bars;
    bars.isBigEndian = mIsBigEndian;
    bars.version = mVersion;
    bars.trailing = mTrailing;

    std::unordered_map<const std::vector<u8>*, s32> fileIndices;

    for (const Item* item : mAssets)
    {
        const AudioAsset* asset = static_cast<const AudioAsset*>(item);

        aal::BarsEntry entry;
        entry.isInResetTable = asset->isInResetTable();

        if (!MakeEntryMeta(*asset, &entry.meta, outError))
            return false;

        if (const std::vector<u8>* file = asset->getFile().get())
        {
            const auto inserted = fileIndices.emplace(file, static_cast<s32>(bars.assets.size()));

            if (inserted.second)
                bars.assets.push_back(*file);

            entry.assetIndex = inserted.first->second;
        }

        bars.entries.push_back(std::move(entry));
    }

    return aal::WriteBars(bars, out, outError);
}

bool AudioResource::stageStreamFiles(const sead::SafeString& previousPath, std::vector<StreamWrite>* out, const char** outError) const
{
    if (!holdsStreams())
        return true;

    const bool isNewLocation = !IsSameFolder(previousPath, mFilePath);
    std::vector<std::string> stagedPaths;

    for (Item* item : mAssets)
    {
        AudioAsset* asset = static_cast<AudioAsset*>(item);
        const bool copiesFile = copiesStreamFile(*asset, isNewLocation);

        if (!copiesFile && !writesStreamFile(*asset, isNewLocation))
            continue;

        StreamWrite write;
        write.asset = asset;
        write.path = getStreamFilePath(*asset);
        write.tempPath = write.path + cStagedFileSuffix;

        std::error_code error;
        fs::create_directories(fs::path(write.path).parent_path(), error);

        const bool isStaged = copiesFile
            ? fileio::CopyFileOver(MakeStreamFilePath(previousPath, *asset).c_str(), write.tempPath.c_str())
            : fileio::WriteWholeFile(write.tempPath.c_str(), asset->getStreamFile()->data(), asset->getStreamFile()->size()) == fileio::WriteResult::Ok;

        if (!isStaged)
        {
            RemoveStagedFiles(stagedPaths);
            fileio::RemoveFile(write.tempPath.c_str());

            *outError = messages::resource::cStreamWriteFailed;
            return false;
        }

        stagedPaths.push_back(write.tempPath);
        out->push_back(std::move(write));
    }

    return true;
}

bool AudioResource::commitStreamFiles(const std::vector<StreamWrite>& writes) const
{
    bool isCommitted = true;

    for (const StreamWrite& write : writes)
    {
        if (!fileio::RenameOver(write.tempPath.c_str(), write.path.c_str()))
        {
            fileio::RemoveFile(write.tempPath.c_str());
            isCommitted = false;
            continue;
        }

        if (write.asset->getStreamFile())
            write.asset->markStreamFileWritten();
    }

    return isCommitted;
}

bool AudioResource::buildSavedBytes(std::vector<u8>* out, const char** outError) const
{
    if (!buildFile(out, outError))
        return false;

    if (!mIsCompressed)
        return true;

    std::vector<u8> packed;

    if (!cmpbin::CompressZstdFrame(out->data(), out->size(), packed))
    {
        *outError = messages::resource::cCompressFailed;
        return false;
    }

    *out = std::move(packed);
    return true;
}

bool AudioResource::writeArchive(const char** outError)
{
    std::vector<u8> bytes;

    if (!buildSavedBytes(&bytes, outError))
        return false;

    if (!packfile::WriteFile(mFilePath.cstr(), bytes.data(), bytes.size()))
    {
        *outError = packfile::IsPackPath(mFilePath.cstr()) ? messages::resource::cPackWriteFailed : messages::resource::cWriteFailed;
        return false;
    }

    syncMetaNames();
    return true;
}

SaveResult AudioResource::writeAll(const sead::SafeString& previousPath, const char** outError)
{
    std::vector<StreamWrite> writes;

    if (!stageStreamFiles(previousPath, &writes, outError))
        return SaveResult::Failed;

    if (!writeArchive(outError))
    {
        for (const StreamWrite& write : writes)
            fileio::RemoveFile(write.tempPath.c_str());

        return SaveResult::Failed;
    }

    if (!commitStreamFiles(writes))
    {
        *outError = messages::resource::cStreamsNotSaved;
        return SaveResult::StreamsFailed;
    }

    return SaveResult::Saved;
}

SaveResult AudioResource::save(const char** outError)
{
    return writeAll(mFilePath, outError);
}

SaveResult AudioResource::saveAs(const sead::SafeString& path, const char** outError)
{
    const sead::FixedSafeString<512> previousPath(mFilePath);
    const bool previousCompressed = mIsCompressed;

    mFilePath = path;
    mIsCompressed = IsZstdPath(path.cstr());

    const SaveResult result = writeAll(previousPath, outError);

    if (result == SaveResult::Failed)
    {
        mFilePath = previousPath;
        mIsCompressed = previousCompressed;
    }

    return result;
}

bool AudioResource::holdsStreams() const
{
    return mContainer != Container::Bwav;
}

bool AudioResource::canEditAssetList() const
{
    return mIsOpen && mContainer != Container::Bwav && mSarcFiles.entries.empty();
}

bool AudioResource::writesStreamFile(const AudioAsset& asset, bool isNewLocation) const
{
    return holdsStreams() && asset.getStreamFile() && (asset.needsStreamFileWrite() || isNewLocation);
}

bool AudioResource::copiesStreamFile(const AudioAsset& asset, bool isNewLocation) const
{
    const StreamFileState state = asset.getStreamFileState();
    const bool isOnDisk = state == StreamFileState::Found || state == StreamFileState::Damaged || state == StreamFileState::Mismatched;

    return holdsStreams() && isNewLocation && !asset.getStreamFile() && isOnDisk;
}

bool AudioResource::canEditStreamAudio(const AudioAsset& asset, const char** outReason) const
{
    if (!asset.isStream())
        return true;

    if (!holdsStreams())
    {
        *outReason = messages::resource::cStreamNeedsArchive;
        return false;
    }

    if (asset.getStreamFileState() != StreamFileState::Loaded && asset.getInfo().kind == AudioAssetKind::StreamPrefetch)
    {
        *outReason = asset.getStreamFileState() == StreamFileState::Mismatched ? messages::resource::cStreamMismatched : messages::resource::cStreamNotLoaded;
        return false;
    }

    return true;
}

bool AudioResource::canRenameAsset(const AudioAsset& asset) const
{
    return canEditAssetList() && (!asset.isStream() || asset.getStreamFileState() == StreamFileState::Loaded);
}

bool AudioResource::canRemoveAssets() const
{
    return canEditAssetList();
}

bool AudioResource::canDuplicateAsset(const AudioAsset& asset, const char** outReason) const
{
    const StreamFileState state = asset.getStreamFileState();
    const bool hasStream = state == StreamFileState::Loaded || state == StreamFileState::Found || state == StreamFileState::NotChecked;

    if (!canRemoveAssets() || (asset.isStream() && !hasStream))
    {
        *outReason = state == StreamFileState::Mismatched ? messages::resource::cStreamMismatched : messages::resource::cStreamNotLoaded;
        return false;
    }

    return true;
}

bool AudioResource::canAddAssets() const
{
    return canEditAssetList() && findTemplateAsset(nullptr) != nullptr;
}

const AudioAsset* AudioResource::findTemplateAsset(const Item* preferred) const
{
    const auto canReplace = [this](const AudioAsset& asset)
    {
        const char* reason = nullptr;

        return !asset.isStream() && canReplaceAudio(asset, &reason);
    };

    if (preferred && preferred->getItemType() == Item::ItemType::AudioAsset && canReplace(*static_cast<const AudioAsset*>(preferred)))
        return static_cast<const AudioAsset*>(preferred);

    for (const Item* item : mAssets)
    {
        if (canReplace(*static_cast<const AudioAsset*>(item)))
            return static_cast<const AudioAsset*>(item);
    }

    return nullptr;
}

AudioAsset* AudioResource::createAsset(const AudioAsset& source) const
{
    AudioAsset* asset = new AudioAsset();
    asset->getName() = source.getName();
    asset->setMeta(source.getMeta());
    asset->setInResetTable(source.isInResetTable());
    asset->setFile(source.getFile());

    if (source.getStreamFile())
        asset->setStreamFile(source.getStreamFile(), StreamFileState::Loaded, true);

    return asset;
}

AudioAsset* AudioResource::duplicateAsset(AudioAsset* source, const char** outError)
{
    if (!canDuplicateAsset(*source, outError))
        return nullptr;

    loadStreamFile(source);

    if (!canRenameAsset(*source))
    {
        *outError = source->getStreamFileState() == StreamFileState::Mismatched ? messages::resource::cStreamMismatched : messages::resource::cStreamNotLoaded;
        return nullptr;
    }

    AudioAsset* asset = createAsset(*source);
    MakeCopyName(source->getName(), [this](const sead::SafeString& candidate) { return validateName(candidate, nullptr); }, &asset->getName());

    std::vector<u8> meta = asset->getMeta();

    if (!meta.empty() && !aal::RenameAmta(&meta, asset->getName().cstr()))
    {
        delete asset;
        *outError = messages::resource::cRenameFailed;
        return nullptr;
    }

    asset->setMeta(std::move(meta));
    mAssets.insertAfter(source, asset);
    renumberAssets();

    return asset;
}

bool AudioResource::canReplaceAudio(const AudioAsset& asset, const char** outReason) const
{
    const AudioAssetKind kind = asset.getInfo().kind;

    if (kind != AudioAssetKind::Bwav && kind != AudioAssetKind::Fwav && kind != AudioAssetKind::StreamPrefetch)
    {
        *outReason = messages::resource::cReplaceUnsupported;
        return false;
    }

    if (asset.getInfo().channelCount == 0)
    {
        *outReason = messages::resource::cAudioDamaged;
        return false;
    }

    if (!canEditStreamAudio(asset, outReason))
        return false;

    if (kind != AudioAssetKind::StreamPrefetch)
        return true;

    aal::Bfstm stream;
    const AudioAssetFile& file = asset.getStreamFile();

    if (!aal::ReadBfstmInfo(file->data(), file->size(), &stream))
    {
        *outReason = messages::resource::cAudioDamaged;
        return false;
    }

    return aal::CanWriteBfstm(stream, outReason);
}

bool AudioResource::canSetLoop(const AudioAsset& asset, const char** outReason) const
{
    if (asset.getInfo().kind != AudioAssetKind::Bwav)
    {
        *outReason = messages::resource::cLoopNeedsBwav;
        return false;
    }

    if (!canEditStreamAudio(asset, outReason))
        return false;

    if (asset.isStream() && asset.getStreamFileState() != StreamFileState::Loaded)
    {
        *outReason = asset.getStreamFileState() == StreamFileState::Mismatched ? messages::resource::cStreamMismatched : messages::resource::cStreamNotLoaded;
        return false;
    }

    return true;
}

bool AudioResource::replaceAudio(AudioAsset* asset, const WaveFile::RiffWaveInfo& info, WaveFile::Encoding encoding, const char** outError)
{
    if (!canReplaceAudio(*asset, outError))
        return false;

    if (asset->getInfo().kind == AudioAssetKind::Fwav)
    {
        const WaveFile* current = asset->getPreviewWave();

        if (!current)
        {
            *outError = messages::resource::cAudioDamaged;
            return false;
        }

        WaveFile wave;
        wave.setStandaloneTarget(current->getTarget());

        if (!wave.readWavFile(info, encoding))
        {
            *outError = messages::import::cWaveImportFailed;
            return false;
        }

        wave.updateLoop();

        std::vector<u8> bytes;

        if (!WriteWaveFileBytes(wave, &bytes))
        {
            *outError = messages::resource::cEncodeFailed;
            return false;
        }

        asset->setFile(MakeAssetFile(std::move(bytes)));
        UpdateMetaFromInfo(asset);

        return true;
    }

    std::vector<std::vector<s16>> pcm;
    u32 loopStartFrame = 0;

    if (!ReadWavPcm(info, &pcm, &loopStartFrame, outError))
        return false;

    if (asset->getInfo().kind == AudioAssetKind::StreamPrefetch)
    {
        aal::Bfstm shape;
        aal::Bfstm encoded;
        std::vector<u8> bytes;
        const AudioAssetFile& stream = asset->getStreamFile();

        if (!aal::ReadBfstm(stream->data(), stream->size(), &shape, outError) ||
            !aal::EncodeBfstm(shape, pcm, info.sampleRate, info.isLoop, loopStartFrame, &encoded, outError))
            return false;

        if (!aal::WriteBfstm(encoded, &bytes))
        {
            *outError = messages::resource::cEncodeFailed;
            return false;
        }

        if (!SetBfstmStreamAudio(asset, std::move(bytes), outError))
            return false;

        UpdateMetaFromInfo(asset);
        return true;
    }

    aal::Bwav current;
    ReadAudioAssetBwav(asset->getFile(), &current);

    aal::Bwav full = aal::EncodeBwav(pcm, info.sampleRate, info.isLoop, loopStartFrame);
    full.isBigEndian = current.isBigEndian;

    if (asset->isStream())
        SetStreamAudio(asset, full);
    else
        asset->setFile(MakeAssetFile(aal::WriteBwav(full)));

    UpdateMetaFromInfo(asset);

    return true;
}

bool AudioResource::replaceFile(AudioAsset* asset, std::vector<u8> file, const char** outError)
{
    const AudioAssetKind kind = DetectAudioAssetKind(file);

    if (!canReplaceAudio(*asset, outError))
        return false;

    if (kind != asset->getCompleteFileKind())
    {
        *outError = kind == AudioAssetKind::Bwav || kind == AudioAssetKind::Fwav || kind == AudioAssetKind::Stream ? messages::resource::cImportKindMismatch : messages::resource::cImportUnsupported;
        return false;
    }

    if (kind == AudioAssetKind::Stream)
    {
        aal::Bfstm stream;

        if (!aal::ReadBfstm(file.data(), file.size(), &stream, outError) || !SetBfstmStreamAudio(asset, std::move(file), outError))
            return false;

        UpdateMetaFromInfo(asset);
        return true;
    }

    if (kind == AudioAssetKind::Fwav)
    {
        if (!IsReadableFwav(file))
        {
            *outError = messages::resource::cAudioDamaged;
            return false;
        }

        asset->setFile(MakeAssetFile(std::move(file)));
        UpdateMetaFromInfo(asset);

        return true;
    }

    aal::Bwav bwav;

    if (!aal::ReadBwav(file.data(), file.size(), &bwav, outError))
        return false;

    if (!aal::IsBwavComplete(bwav))
    {
        *outError = messages::resource::cImportNeedsCompleteBwav;
        return false;
    }

    if (asset->isStream())
        SetStreamAudio(asset, bwav);
    else
        asset->setFile(MakeAssetFile(std::move(file)));

    UpdateMetaFromInfo(asset);

    return true;
}

bool AudioResource::setLoop(AudioAsset* asset, bool isLoop, u32 loopStartFrame, const char** outError)
{
    if (!canSetLoop(*asset, outError))
        return false;

    aal::Bwav bwav;

    if (!ReadAudioAssetBwav(asset->getCompleteFile(), &bwav))
    {
        *outError = messages::resource::cAudioDamaged;
        return false;
    }

    if (!aal::CanSetBwavLoop(bwav, isLoop, loopStartFrame))
    {
        *outError = messages::wavefile::cInvalidLoopRange;
        return false;
    }

    aal::SetBwavLoop(&bwav, isLoop, loopStartFrame);

    if (asset->isStream())
        SetStreamLoop(asset, bwav);
    else
        asset->setFile(MakeAssetFile(aal::WriteBwav(bwav)));

    UpdateMetaFromInfo(asset);

    return true;
}

std::string AudioResource::getStreamFilePath(const AudioAsset& asset) const
{
    return MakeStreamFilePath(mFilePath, asset);
}

std::vector<std::string> AudioResource::getStreamWritePaths(const sead::SafeString& destinationPath) const
{
    const bool isNewLocation = !IsSameFolder(destinationPath, mFilePath);
    std::vector<std::string> paths;

    for (const Item* item : mAssets)
    {
        const AudioAsset* asset = static_cast<const AudioAsset*>(item);

        if (writesStreamFile(*asset, isNewLocation) || copiesStreamFile(*asset, isNewLocation))
            paths.push_back(MakeStreamRelativePath(*asset).generic_string());
    }

    return paths;
}

bool AudioResource::writeBackup(const std::string& directory, const std::string& fileName, std::vector<std::string>* outStreamPaths) const
{
    std::vector<u8> bytes;
    const char* error = nullptr;

    if (!buildSavedBytes(&bytes, &error) || fileio::WriteWholeFile((fs::path(directory) / fileName).string().c_str(), bytes.data(), bytes.size()) != fileio::WriteResult::Ok)
        return false;

    for (const Item* item : mAssets)
    {
        const AudioAsset* asset = static_cast<const AudioAsset*>(item);

        if (!writesStreamFile(*asset, false))
            continue;

        const std::string relativePath = MakeStreamRelativePath(*asset).generic_string();
        const fs::path path = fs::path(directory) / relativePath;
        std::error_code ec;

        fs::create_directories(path.parent_path(), ec);

        if (fileio::WriteWholeFile(path.string().c_str(), asset->getStreamFile()->data(), asset->getStreamFile()->size()) != fileio::WriteResult::Ok)
            return false;

        outStreamPaths->push_back(relativePath);
    }

    return true;
}

bool AudioResource::restoreStreamFile(const std::string& relativePath, std::vector<u8> bytes)
{
    for (Item* item : mAssets)
    {
        AudioAsset* asset = static_cast<AudioAsset*>(item);

        if (!asset->isStream() || MakeStreamRelativePath(*asset).generic_string() != relativePath)
            continue;

        if (CheckStreamFile(*asset, bytes) != StreamFileState::Loaded)
            return false;

        asset->setStreamFile(MakeAssetFile(std::move(bytes)), StreamFileState::Loaded, true);
        return true;
    }

    return false;
}

void AudioResource::probeStreamFile(AudioAsset* asset) const
{
    if (!holdsStreams() || !asset->isStream() || asset->getStreamFileState() != StreamFileState::NotChecked || mFilePath.isEmpty())
        return;

    std::error_code error;
    const bool exists = fs::is_regular_file(getStreamFilePath(*asset), error);

    asset->setStreamFile(nullptr, exists ? StreamFileState::Found : StreamFileState::Missing, false);
}

void AudioResource::loadStreamFile(AudioAsset* asset)
{
    probeStreamFile(asset);

    if (asset->getStreamFileState() != StreamFileState::Found)
        return;

    std::vector<u8> bytes;
    const StreamFileState state = fileio::ReadWholeFile(getStreamFilePath(*asset).c_str(), &bytes) ? CheckStreamFile(*asset, bytes) : StreamFileState::Damaged;

    asset->setStreamFile(state == StreamFileState::Loaded ? MakeAssetFile(std::move(bytes)) : nullptr, state, false);
}
