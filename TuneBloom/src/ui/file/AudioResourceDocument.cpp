#include <ui/file/AudioResourceDocument.h>

#include <ui/backup/BackupInternal.h>
#include <ui/file/FileDialogs.h>
#include <ui/file/LooseFileInfo.h>
#include <ui/Messages.h>
#include <ui/app/PopupMgr.h>
#include <ui/resource/ResourceViews.h>

#include <bfsar/io/FileIo.h>
#include <bfsar/io/PackFile.h>
#include <browser/FileProbe.h>

#include <filesystem>

namespace fs = std::filesystem;

Document* AudioResourceDocument::Open(File* file, const sead::SafeString& path)
{
    std::vector<u8> bytes(file->data, file->data + file->size);
    Free(file);

    AudioResourceDocument* document = new AudioResourceDocument();
    const char* error = nullptr;

    if (document->resource().open(std::move(bytes), path, &error))
        return document;

    PopupMgr::instance()->addPopup({ error, nullptr });

    delete document;
    return nullptr;
}

AudioResourceDocument::~AudioResourceDocument()
{
    closeWindows();
}

AudioResource& AudioResourceDocument::resource()
{
    return mResource;
}

Document::Kind AudioResourceDocument::kind() const
{
    return Kind::AudioResource;
}

const sead::SafeString& AudioResourceDocument::filePath() const
{
    return mResource.getFilePath();
}

const sead::SafeString& AudioResourceDocument::loadedPath() const
{
    return mResource.getFilePath();
}

std::string AudioResourceDocument::displayName() const
{
    return fs::path(mResource.getFilePath().cstr()).filename().string();
}

const char* AudioResourceDocument::extension() const
{
    return GetResourceFormatInfo(mResource.getFormat()).extension;
}

BrowserFileKind AudioResourceDocument::fileKind() const
{
    return mResource.getFormat() == ResourceFormat::Bwav ? BrowserFileKind::Wave : BrowserFileKind::AudioResource;
}

UIType AudioResourceDocument::defaultTab() const
{
    return UIType::AudioAssets;
}

void AudioResourceDocument::drawInfo(UIType)
{
    DrawAudioAssetsUI();
}

bool AudioResourceDocument::drawFileProperties()
{
    DrawAudioResourceInfoUI();
    return true;
}

StreamSource AudioResourceDocument::activeStreamSource()
{
    return {};
}

PlayableFile AudioResourceDocument::playableFile()
{
    PlayableFile playable;

    if (mResource.getContainer() != AudioResource::Container::Bwav)
        return playable;

    for (Item* asset : mResource.getAssetList())
    {
        playable.asset = static_cast<AudioAsset*>(asset);
        break;
    }

    return playable;
}

void AudioResourceDocument::formatFileFacts(sead::BufferedSafeString* out) const
{
    sead::FixedSafeString<16> version;
    FormatResourceVersion(&version, mResource.getVersion());

    out->format("%s %s", GetResourceFormatInfo(mResource.getFormat()).displayName, version.cstr());

    for (const Item* asset : mResource.getAssetList())
    {
        const AudioAssetInfo& info = static_cast<const AudioAsset*>(asset)->getInfo();

        AppendRateFact(out, info.sampleRate);
        AppendChannelFact(out, info.channelCount);
        break;
    }
}

bool AudioResourceDocument::isValidName(const sead::SafeString& name) const
{
    return mResource.validName(name);
}

bool AudioResourceDocument::isUniqueName(const sead::SafeString& name, const Item* ignoreItem) const
{
    return mResource.validateName(name, ignoreItem);
}

void AudioResourceDocument::renumber(Item::List& list)
{
    if (&list == &mResource.getAssetList())
        mResource.renumberAssets();
    else
        Bfsar::updateList(list);
}

void AudioResourceDocument::forEachItemList(const std::function<void(Item::List&)>& visit)
{
    visit(mResource.getAssetList());
}

void AudioResourceDocument::captureItemBaselines()
{
    mResource.captureItemBaselines();
}

bool AudioResourceDocument::isInfoModified() const
{
    return false;
}

bool AudioResourceDocument::ownsItem(const Item& item) const
{
    const ItemList* list = static_cast<const ItemList*>(item.list());

    return list && list->getResource() == &mResource;
}

Document::ParseContext AudioResourceDocument::parseContext() const
{
    return CurrentParseContext();
}

u64 AudioResourceDocument::estimateMemory() const
{
    return 0;
}

bool AudioResourceDocument::isLoaded() const
{
    return true;
}

bool AudioResourceDocument::canUnload() const
{
    return false;
}

void AudioResourceDocument::unload()
{
}

bool AudioResourceDocument::reload()
{
    return true;
}

bool AudioResourceDocument::prepareSave()
{
    sead::FixedSafeString<256> error;
    const Item* item = nullptr;

    if (mResource.validate(&error, &item))
        return true;

    PopupMgr::instance()->addPopup({ error, const_cast<Item*>(item) });
    return false;
}

bool AudioResourceDocument::prepareSaveAs()
{
    return prepareSave();
}

bool AudioResourceDocument::chooseSaveAsPath(sead::BufferedSafeString* outPath) const
{
    const ResourceFormatInfo& format = GetResourceFormatInfo(mResource.getFormat());

    const sead::FormatFixedSafeString<32> pattern("*.%s", format.extension);
    const sead::FormatFixedSafeString<64> filterName("%s (%s)", format.displayName, pattern.cstr());
    const sead::FormatFixedSafeString<32> compressedPattern("*.%s", cZstdExtension);
    const sead::FormatFixedSafeString<96> compressedName(messages::common::cCompressedFilterFormat, format.displayName, format.extension, cZstdExtension);

    FileFilter filters[] = {
        { filterName.cstr(), pattern.cstr() },
        { compressedName.cstr(), compressedPattern.cstr() }
    };

    const fs::path documentPath(mResource.getFilePath().cstr());
    const std::string defaultPath = (fs::path(packfile::GetDiskPath(documentPath.string())).parent_path() / documentPath.filename()).string();

    if (!SaveFileDialog(outPath, nullptr, IM_ARRAYSIZE(filters), filters, format.extension, defaultPath.c_str()))
        return false;

    AddInnerExtension(outPath, format.extension, cZstdExtension);

    return true;
}

SaveResult AudioResourceDocument::save(const char** outError)
{
    return mResource.save(outError);
}

SaveResult AudioResourceDocument::saveAs(const sead::SafeString& path, const char** outError)
{
    return mResource.saveAs(path, outError);
}

void AudioResourceDocument::finishSave()
{
}

s32 AudioResourceDocument::getBackupNamesSetting() const
{
    return -1;
}

bool AudioResourceDocument::writeBackup(const BackupWrite& write, backup::Snapshot* snapshot, Bfsar::BackupResult* outResult)
{
    sead::FixedSafeString<256> error;
    const Item* invalidItem = nullptr;

    if (!mResource.validate(&error, &invalidItem))
    {
        *outResult = Bfsar::BackupResult::Invalid;
        return false;
    }

    std::vector<std::string> streamPaths;

    if (!mResource.writeBackup(write.dir, snapshot->archiveName, &streamPaths))
    {
        *outResult = Bfsar::BackupResult::WriteFailed;
        return false;
    }

    for (const std::string& path : streamPaths)
        snapshot->streams.push_back({ path, 0 });

    *outResult = Bfsar::BackupResult::Ok;
    return true;
}

void AudioResourceDocument::copyPreviousSidecars(const std::string& sourcePath, const std::string& destDir, backup::Snapshot* snapshot)
{
    const fs::path sourceDir = fs::path(sourcePath).parent_path();

    for (const std::string& relativePath : mResource.getStreamWritePaths(sead::FixedSafeString<512>(sourcePath.c_str())))
    {
        const fs::path dest = fs::path(destDir) / relativePath;
        std::error_code error;

        if (!fs::is_regular_file(sourceDir / relativePath, error))
            continue;

        fs::create_directories(dest.parent_path(), error);
        fs::copy_file(sourceDir / relativePath, dest, error);

        if (!error)
            snapshot->streams.push_back({ relativePath, 0 });
    }
}

void AudioResourceDocument::restoreSidecars(const backup::Snapshot& snapshot)
{
    const fs::path dir(snapshot.dir);

    for (const Bfsar::BackupStream& stream : snapshot.streams)
    {
        std::vector<u8> bytes;

        if (fileio::ReadWholeFile((dir / stream.path).string().c_str(), &bytes) && mResource.restoreStreamFile(stream.path, std::move(bytes)))
            continue;

        const sead::FormatFixedSafeString<1024> message(messages::backup::cRestoreStreamFormat, stream.path.c_str());
        PopupMgr::instance()->addPopup({ message, nullptr });
    }
}

void AudioResourceDocument::applyRestoredSnapshot(const backup::Snapshot&, bool)
{
}
