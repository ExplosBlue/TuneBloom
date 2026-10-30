#include <aal/AudioResource.h>

#include <aal/Amta.h>
#include <aal/Bars.h>
#include <aal/BarsSarc.h>
#include <aal/Bfstm.h>
#include <aal/Bwav.h>
#include <bfsar/archive/Cmpbin.h>
#include <bfsar/files/BfstpFile.h>
#include <bfsar/io/FileIo.h>
#include <bfsar/io/PackFile.h>
#include <bfsar/io/TitleFolder.h>
#include <core/FileName.h>
#include <ui/Messages.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <unordered_map>

namespace fs = std::filesystem;

static constexpr char cFirstNameCharacter      = '!';
static constexpr char cLastNameCharacter       = '~';
static constexpr char cInnerNameSpace          = ' ';
static constexpr s32 cLoadedNameLengthMax      = 255;
static constexpr char cStagedFileSuffix[]      = ".savetmp";
static constexpr char cParentFolderPrefix[]    = "../";
static constexpr u32 cNewBwavSampleRate        = 48000;

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

static StreamFileState CheckStreamFileAlone(const AudioAsset& asset, const std::vector<u8>& bytes)
{
    aal::Bfstm bfstm;
    aal::Bwav bwav;
    const char* error = nullptr;

    if (asset.getStreamFileKind() == AudioAssetKind::Stream)
        return aal::ReadBfstm(bytes.data(), bytes.size(), &bfstm, &error) ? StreamFileState::Loaded : StreamFileState::Damaged;

    return aal::ReadBwav(bytes.data(), bytes.size(), &bwav, &error) && aal::IsBwavComplete(bwav) ? StreamFileState::Loaded : StreamFileState::Damaged;
}

static StreamFileState CheckStreamFile(const AudioAsset& asset, const std::vector<u8>& bytes)
{
    if (!asset.getFile())
        return CheckStreamFileAlone(asset, bytes);

    return asset.getInfo().kind == AudioAssetKind::StreamPrefetch ? CheckBfstmStreamFile(asset, bytes) : CheckBwavStreamFile(asset, bytes);
}

static std::string GetArchiveStem(const sead::SafeString& archivePath)
{
    fs::path name = fs::path(archivePath.cstr()).filename();

    if (IsZstdPath(archivePath.cstr()))
        name = name.stem();

    return name.stem().string();
}

static std::string GetStreamFolderName(const sead::SafeString& archivePath, StreamFolderName name)
{
    switch (name)
    {
        case StreamFolderName::StemSuffix:
            return GetArchiveStem(archivePath) + cResourceNamedStreamSuffix;

        case StreamFolderName::StemPrefix:
            return cResourceNamedStreamPrefix + GetArchiveStem(archivePath);

        default:
            break;
    }

    return cResourceStreamFolder;
}

static fs::path MakeStreamFolderPath(const fs::path& archiveFolder, const sead::SafeString& archivePath, const StreamFolder& folder)
{
    const fs::path parent = folder.parent.empty() ? archiveFolder : (archiveFolder / folder.parent).lexically_normal();

    return parent / GetStreamFolderName(archivePath, folder.name);
}

static fs::path GetArchiveFolder(const sead::SafeString& archivePath)
{
    return fs::path(packfile::GetFolderOnDisk(archivePath.cstr()));
}

static std::string MakeStreamFileName(const AudioAsset& asset)
{
    return std::string(asset.getName().cstr()) + "." + GetAudioAssetKindExtension(asset.getStreamFileKind());
}

static std::string MakeStreamFilePath(const fs::path& archiveFolder, const sead::SafeString& archivePath, const StreamFolder& folder, const AudioAsset& asset)
{
    return (MakeStreamFolderPath(archiveFolder, archivePath, folder) / MakeStreamFileName(asset)).string();
}

static std::string FindStreamFileOnDisk(const sead::SafeString& archivePath, const StreamFolder& folder, const std::string& baseFolder, const StreamFolder& baseForm, const AudioAsset& asset)
{
    std::error_code error;
    const std::string path = MakeStreamFilePath(GetArchiveFolder(archivePath), archivePath, folder, asset);

    if (baseFolder.empty() || fs::is_regular_file(path, error))
        return path;

    const std::string basePath = MakeStreamFilePath(fs::path(baseFolder), archivePath, baseForm, asset);

    return fs::is_regular_file(basePath, error) ? basePath : path;
}

static bool IsSameStreamFolder(const sead::SafeString& left, const StreamFolder& leftForm, const sead::SafeString& right, const StreamFolder& rightForm)
{
    std::error_code error;
    const fs::path leftFolder = fs::absolute(MakeStreamFolderPath(GetArchiveFolder(left), left, leftForm), error).lexically_normal();
    const fs::path rightFolder = fs::absolute(MakeStreamFolderPath(GetArchiveFolder(right), right, rightForm), error).lexically_normal();

    return leftFolder == rightFolder;
}

static StreamFolder GetStreamFolderAt(const sead::SafeString& archivePath, const StreamFolder& folder)
{
    std::error_code error;

    if (folder.parent.empty() || fs::is_directory(GetArchiveFolder(archivePath) / folder.parent, error))
        return folder;

    return { std::string(), folder.name };
}

static u32 CountStreamFiles(const fs::path& streamFolder, const std::vector<const AudioAsset*>& streams)
{
    std::error_code error;
    u32 count = 0;

    if (!fs::is_directory(streamFolder, error))
        return 0;

    for (const AudioAsset* asset : streams)
        count += fs::is_regular_file(streamFolder / MakeStreamFileName(*asset), error) ? 1 : 0;

    return count;
}

static std::vector<StreamFolder> ListNearStreamFolders()
{
    return { { std::string(), StreamFolderName::StemSuffix }, { std::string(), StreamFolderName::StemPrefix }, { std::string(), StreamFolderName::Plain } };
}

static std::vector<StreamFolder> ListSiblingStreamFolders(const fs::path& archiveFolder)
{
    std::vector<StreamFolder> folders;
    std::error_code error;

    for (const fs::directory_entry& entry : fs::directory_iterator(archiveFolder.parent_path(), error))
    {
        std::string name;

        if (entry.path().filename() == archiveFolder.filename() || !fs::is_directory(entry.path() / cResourceStreamFolder, error) || !fileio::GetNarrowPath(entry.path().filename(), &name))
            continue;

        folders.push_back({ cParentFolderPrefix + name, StreamFolderName::Plain });
    }

    std::sort(folders.begin(), folders.end(), [](const StreamFolder& left, const StreamFolder& right) { return left.parent < right.parent; });

    return folders;
}

static bool FindFullestStreamFolder(const fs::path& archiveFolder, const sead::SafeString& archivePath, const std::vector<StreamFolder>& candidates, const std::vector<const AudioAsset*>& streams, u32 countMin, StreamFolder* out)
{
    u32 bestCount = 0;

    for (const StreamFolder& candidate : candidates)
    {
        const u32 count = CountStreamFiles(MakeStreamFolderPath(archiveFolder, archivePath, candidate), streams);

        if (count <= bestCount || count < countMin)
            continue;

        bestCount = count;
        *out = candidate;
    }

    return bestCount > 0;
}

static bool FindHoldingStreamFolder(const fs::path& archiveFolder, const sead::SafeString& archivePath, const std::vector<const AudioAsset*>& streams, StreamFolder* out)
{
    const u32 siblingCountMin = static_cast<u32>(streams.size() / 2 + 1);

    return FindFullestStreamFolder(archiveFolder, archivePath, ListNearStreamFolders(), streams, 1, out) ||
           FindFullestStreamFolder(archiveFolder, archivePath, ListSiblingStreamFolders(archiveFolder), streams, siblingCountMin, out);
}

static std::vector<const AudioAsset*> CollectStreamAssets(const Item::List& assets)
{
    std::vector<const AudioAsset*> streams;

    for (const Item* item : assets)
    {
        const AudioAsset* asset = static_cast<const AudioAsset*>(item);

        if (asset->isStream())
            streams.push_back(asset);
    }

    return streams;
}

static StreamFolder FindExistingStreamFolder(const fs::path& archiveFolder, const sead::SafeString& archivePath)
{
    std::error_code error;

    for (const StreamFolder& candidate : ListNearStreamFolders())
    {
        if (fs::is_directory(MakeStreamFolderPath(archiveFolder, archivePath, candidate), error))
            return candidate;
    }

    return StreamFolder();
}

static bool ReadSarcAudioName(const sarc::Entry& entry, std::string* outName)
{
    const AudioAssetKind kind = DetectAudioAssetKind(entry.data);
    const bool isAsset = kind == AudioAssetKind::Bwav || kind == AudioAssetKind::Fwav || kind == AudioAssetKind::StreamPrefetch;

    return entry.hasName && isAsset && filename::StripExactExtension(entry.name, GetAudioAssetKindExtension(kind), outName);
}

static bool AttachSarcMeta(const AudioResource& resource, sarc::Entry* entry, std::vector<AudioAsset*>* assets, std::unordered_map<std::string, AudioAsset*>* assetsByName)
{
    std::string name;
    std::string metaName;

    if (!entry->hasName || !filename::StripExactExtension(entry->name, aal::cAmtaFileExtension, &name) || !resource.validName(sead::SafeString(name.c_str())))
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

static void MakeAssetCopyName(const AudioResource& resource, const sead::SafeString& name, bool keepsExtensions, sead::BufferedSafeString* out)
{
    const std::string text = name.cstr();
    const size_t dot = keepsExtensions ? text.find('.') : std::string::npos;
    const std::string stem = text.substr(0, dot);
    const std::string extensions = dot == std::string::npos ? std::string() : text.substr(dot);

    sead::FixedSafeString<AudioResource::cNameLengthMax + 1> fullName;

    MakeCopyName(sead::SafeString(stem.c_str()), [&resource, &extensions, &fullName](const sead::SafeString& candidate)
    {
        fullName.format("%s%s", candidate.cstr(), extensions.c_str());
        return resource.validateName(fullName, nullptr);
    }, out);

    out->append(extensions.c_str());
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
    , mStreamFolder()
    , mBaseTitleFolder()
    , mBaseStreamFolder()
    , mVersion(0)
    , mNewAssetKind(AudioAssetKind::Bwav)
    , mNewAssetHasMeta(true)
    , mLastSampleDrop(-1)
    , mQuietTailLevel(-1)
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
    findStreamFolders();

    for (Item* item : mAssets)
        probeStreamFile(static_cast<AudioAsset*>(item));

    return true;
}

void AudioResource::findStreamFolders()
{
    if (!holdsStreams())
        return;

    const std::vector<const AudioAsset*> streams = CollectStreamAssets(mAssets);
    std::error_code error;
    const fs::path archiveFolder = fs::absolute(GetArchiveFolder(mFilePath), error);
    const bool isInBaseTitle = findBaseStreamFolder(streams);

    if (!streams.empty() && FindHoldingStreamFolder(archiveFolder, mFilePath, streams, &mStreamFolder))
        return;

    mStreamFolder = isInBaseTitle ? GetStreamFolderAt(mFilePath, mBaseStreamFolder) : FindExistingStreamFolder(archiveFolder, mFilePath);
}

bool AudioResource::findBaseStreamFolder(const std::vector<const AudioAsset*>& streams)
{
    std::error_code error;

    mBaseTitleFolder = streams.empty() ? std::string() : titlefolder::FindBaseFolder(fs::absolute(GetArchiveFolder(mFilePath), error).string());
    mBaseStreamFolder = StreamFolder();

    return !mBaseTitleFolder.empty() && FindHoldingStreamFolder(fs::path(mBaseTitleFolder), mFilePath, streams, &mBaseStreamFolder);
}

bool AudioResource::openBars(std::vector<u8> bytes, const char** outError)
{
    aal::BarsFile bars;

    if (!aal::ReadBars(bytes.data(), bytes.size(), &bars, outError))
        return false;

    mContainer = Container::Bars;
    mIsBigEndian = bars.isBigEndian;
    mVersion = bars.version;
    mNewAssetKind = bars.version == aal::cBarsVersion11 ? AudioAssetKind::Fwav : AudioAssetKind::Bwav;
    mTrailing = std::move(bars.trailing);

    std::vector<AudioAssetFile> files;
    files.reserve(bars.assets.size());

    for (std::vector<u8>& asset : bars.assets)
        files.push_back(MakeAudioAssetFile(std::move(asset)));

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
        asset->setFile(MakeAudioAssetFile(std::move(entry.data)));

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
    mNewAssetKind = AudioAssetKind::Fwav;

    AddAssetsByName(&mAssets, std::move(assets));
    openSarcParam(&otherFiles);

    file.entries = std::move(otherFiles);
    mSarcFiles = std::move(file);

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
    asset->setFile(MakeAudioAssetFile(std::move(bytes)));
    mAssets.pushBack(asset);

    return true;
}

void AudioResource::renumberAssets()
{
    u32 id = 0;

    for (Item* item : mAssets)
        item->setId(id++);
}

static std::vector<u8> MakeEmptyBwav(bool isBigEndian)
{
    aal::BwavFormat format;
    format.sampleRate = cNewBwavSampleRate;
    format.isBigEndian = isBigEndian;

    aal::Bwav bwav;
    const char* error = nullptr;
    aal::EncodeBwav({ std::vector<s16>() }, format, &bwav, &error);

    return aal::WriteBwav(bwav);
}

void AudioResource::createBwav(const sead::SafeString& fileName)
{
    close();

    const char* error = nullptr;

    mIsOpen = openBwav(MakeEmptyBwav(false), fileName, &error);
    SEAD_ASSERT_MSG(mIsOpen, "%s", error);

    renumberAssets();
}

void AudioResource::createBars(u16 version, AudioAssetKind newAssetKind)
{
    close();

    mContainer = Container::Bars;
    mVersion = version;
    mNewAssetKind = version == aal::cBarsVersion11 ? newAssetKind : AudioAssetKind::Bwav;
    mIsOpen = true;
}

void AudioResource::createSarc(bool hasMeta)
{
    close();

    mContainer = Container::Sarc;
    mIsBigEndian = mSarcFiles.isBigEndian;
    mVersion = mSarcFiles.version;
    mNewAssetKind = AudioAssetKind::Fwav;
    mNewAssetHasMeta = hasMeta;
    mIsOpen = true;
}

void AudioResource::close()
{
    mAssets.clear();
    mTrailing.clear();
    mSarcFiles = sarc::File();
    mSarcParam = SarcParam();
    mFilePath.clear();
    mIsOpen = false;
    mIsCompressed = false;
    mIsBigEndian = false;
    mStreamFolder = StreamFolder();
    mBaseTitleFolder.clear();
    mBaseStreamFolder = StreamFolder();
    mVersion = 0;
    mNewAssetKind = AudioAssetKind::Bwav;
    mNewAssetHasMeta = true;
    mLastSampleDrop = -1;
    mQuietTailLevel = -1;
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

    if (length == 0 || length > cNameLengthMax || name[0] == cInnerNameSpace || name[length - 1] == cInnerNameSpace)
        return false;

    for (s32 i = 0; i < length; i++)
    {
        const char character = name[i];

        if (character == cInnerNameSpace)
            continue;

        if (character < cFirstNameCharacter || character > cLastNameCharacter || std::strchr(filename::cInvalidCharacters, character))
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
            outError->copy(messages::resource::cNameInvalidDetail);
            return false;
        }

        if (!validateName(asset->getName(), asset))
        {
            outError->copy(messages::item::cNameDuplicated);
            return false;
        }

        std::string metaName;
        const std::vector<u8>& meta = asset->getMeta();

        if (mContainer == Container::Sarc && meta.empty())
            continue;

        if (meta.empty() || !aal::ReadAmtaName(meta, &metaName))
        {
            outError->copy(messages::resource::cAmtaDamaged);
            return false;
        }

        std::vector<u8> renamedMeta;
        const char* renameError = nullptr;

        if (!MakeEntryMeta(*asset, &renamedMeta, &renameError))
        {
            outError->copy(renameError);
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

    buildSarcParam(&file);

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

bool AudioResource::stageStreamFiles(const sead::SafeString& previousPath, const StreamFolder& previousFolder, std::vector<StreamWrite>* out, const char** outError) const
{
    if (!holdsStreams())
        return true;

    const bool isNewLocation = !IsSameStreamFolder(previousPath, previousFolder, mFilePath, mStreamFolder);
    std::vector<std::string> stagedPaths;

    for (Item* item : mAssets)
    {
        AudioAsset* asset = static_cast<AudioAsset*>(item);
        const bool copiesFile = copiesStreamFile(*asset, isNewLocation);

        if (!copiesFile && !writesStreamFile(*asset, isNewLocation))
            continue;

        StreamWrite write;
        write.asset = asset;
        write.path = getStreamWritePath(*asset);
        write.tempPath = write.path + cStagedFileSuffix;

        std::error_code error;
        fs::create_directories(fs::path(write.path).parent_path(), error);

        const bool isStaged = copiesFile
            ? fileio::CopyFileOver(FindStreamFileOnDisk(previousPath, previousFolder, mBaseTitleFolder, mBaseStreamFolder, *asset).c_str(), write.tempPath.c_str())
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

SaveResult AudioResource::writeAll(const sead::SafeString& previousPath, const StreamFolder& previousFolder, const char** outError)
{
    std::vector<StreamWrite> writes;

    if (!stageStreamFiles(previousPath, previousFolder, &writes, outError))
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
    return writeAll(mFilePath, mStreamFolder, outError);
}

SaveResult AudioResource::saveAs(const sead::SafeString& path, const char** outError)
{
    const sead::FixedSafeString<512> previousPath(mFilePath);
    const StreamFolder previousFolder = mStreamFolder;
    const bool previousCompressed = mIsCompressed;

    mFilePath = path;
    mStreamFolder = GetStreamFolderAt(path, previousFolder);
    mIsCompressed = IsZstdPath(path.cstr());

    const SaveResult result = writeAll(previousPath, previousFolder, outError);

    if (result == SaveResult::Failed)
    {
        mFilePath = previousPath;
        mStreamFolder = previousFolder;
        mIsCompressed = previousCompressed;
        return result;
    }

    findBaseStreamFolder(CollectStreamAssets(mAssets));

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

bool AudioResource::usesStreamFile(const AudioAsset& asset) const
{
    return holdsStreams() && asset.isStream();
}

bool AudioResource::canEditStreamAudio(const AudioAsset& asset, const char** outReason) const
{
    if (!usesStreamFile(asset))
        return true;

    if (asset.getStreamFileState() != StreamFileState::Loaded && (asset.getInfo().kind == AudioAssetKind::StreamPrefetch || !asset.getFile()))
    {
        *outReason = GetStreamFileStateError(asset.getStreamFileState());
        return false;
    }

    return true;
}

static bool HasLoadedStream(const AudioAsset& asset)
{
    return !asset.isStream() || asset.getStreamFileState() == StreamFileState::Loaded;
}

bool AudioResource::canRenameAsset(const AudioAsset& asset) const
{
    return canEditAssetList() && HasLoadedStream(asset) && aal::CanRenameAmta(asset.getMeta());
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
        *outReason = GetStreamFileStateError(state);
        return false;
    }

    return true;
}

bool AudioResource::canAddAssets() const
{
    return canEditAssetList() && canCreateNewAsset();
}

AudioAsset* AudioResource::createAsset(const AudioAsset& source) const
{
    AudioAsset* asset = new AudioAsset();
    asset->getName() = source.getName();
    asset->setMeta(source.getMeta());
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

    if (!HasLoadedStream(*source))
    {
        *outError = GetStreamFileStateError(source->getStreamFileState());
        return nullptr;
    }

    AudioAsset* asset = createAsset(*source);
    MakeAssetCopyName(*this, source->getName(), mContainer == Container::Sarc, &asset->getName());

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

std::string AudioResource::getStreamFilePath(const AudioAsset& asset) const
{
    return FindStreamFileOnDisk(mFilePath, mStreamFolder, mBaseTitleFolder, mBaseStreamFolder, asset);
}

std::string AudioResource::getStreamWritePath(const AudioAsset& asset) const
{
    return MakeStreamFilePath(GetArchiveFolder(mFilePath), mFilePath, mStreamFolder, asset);
}

std::string AudioResource::getStreamBackupPath(const sead::SafeString& archivePath, const AudioAsset& asset) const
{
    return (fs::path(GetStreamFolderName(archivePath, mStreamFolder.name)) / MakeStreamFileName(asset)).generic_string();
}

void AudioResource::copyPreviousStreamFiles(const sead::SafeString& destinationPath, const std::string& directory, std::vector<std::string>* outStreamPaths) const
{
    const StreamFolder destinationFolder = GetStreamFolderAt(destinationPath, mStreamFolder);
    const bool isNewLocation = !IsSameStreamFolder(destinationPath, destinationFolder, mFilePath, mStreamFolder);

    for (const Item* item : mAssets)
    {
        const AudioAsset* asset = static_cast<const AudioAsset*>(item);

        if (!writesStreamFile(*asset, isNewLocation) && !copiesStreamFile(*asset, isNewLocation))
            continue;

        const fs::path source = MakeStreamFilePath(GetArchiveFolder(destinationPath), destinationPath, destinationFolder, *asset);
        const std::string relativePath = getStreamBackupPath(destinationPath, *asset);
        const fs::path dest = fs::path(directory) / relativePath;
        std::error_code error;

        if (!fs::is_regular_file(source, error))
            continue;

        fs::create_directories(dest.parent_path(), error);
        fs::copy_file(source, dest, error);

        if (!error)
            outStreamPaths->push_back(relativePath);
    }
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

        const std::string relativePath = getStreamBackupPath(mFilePath, *asset);
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

        if (!asset->isStream() || getStreamBackupPath(mFilePath, *asset) != relativePath)
            continue;

        if (CheckStreamFile(*asset, bytes) != StreamFileState::Loaded)
            return false;

        asset->setStreamFile(MakeAudioAssetFile(std::move(bytes)), StreamFileState::Loaded, true);
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

    asset->setStreamFile(state == StreamFileState::Loaded ? MakeAudioAssetFile(std::move(bytes)) : nullptr, state, false);
}
