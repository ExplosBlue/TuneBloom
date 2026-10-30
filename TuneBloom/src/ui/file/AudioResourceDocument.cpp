#include <ui/file/AudioResourceDocument.h>

#include <ui/backup/BackupInternal.h>
#include <ui/file/LooseFileInfo.h>
#include <messages/Messages.h>
#include <bfsar/model/MessageLog.h>
#include <ui/resource/ResourceViews.h>
#include <ui/wave/WaveFormatUI.h>

#include <aal/AudioAsset.h>
#include <aal/Bwav.h>
#include <bfsar/io/FileIo.h>
#include <scan/FileProbe.h>

#include <filesystem>

namespace fs = std::filesystem;

static void AddSnapshotStreams(backup::Snapshot* snapshot, const std::vector<std::string>& paths)
{
    for (const std::string& path : paths)
        snapshot->streams.push_back({ path, 0 });
}

Document* AudioResourceDocument::Open(File* file, const sead::SafeString& path)
{
    std::vector<u8> bytes(file->data, file->data + file->size);
    Free(file);

    AudioResourceDocument* document = new AudioResourceDocument();
    const char* error = nullptr;

    if (document->resource().open(std::move(bytes), path, &error))
        return document;

    MessageLog::instance()->addPopup({ error, nullptr });

    delete document;
    return nullptr;
}

Document* AudioResourceDocument::CreateBwav()
{
    const sead::FormatFixedSafeString<64> fileName(messages::archive::cUntitledFileNameFormat, GetResourceFormatInfo(ResourceFormat::Bwav).extension);

    AudioResourceDocument* document = new AudioResourceDocument();
    document->mResource.createBwav(fileName);

    return document;
}

Document* AudioResourceDocument::CreateBars(u16 version, AudioAssetKind newAssetKind)
{
    AudioResourceDocument* document = new AudioResourceDocument();
    document->mResource.createBars(version, newAssetKind);

    return document;
}

Document* AudioResourceDocument::CreateSarc(bool hasMeta)
{
    AudioResourceDocument* document = new AudioResourceDocument();
    document->mResource.createSarc(hasMeta);

    return document;
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
    if (mResource.getFilePath().isEmpty())
        return archiveName();

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

u32 AudioResourceDocument::getOpusBitrate(const AudioAsset& asset) const
{
    const AudioAssetFile& file = asset.getStreamFile() ? asset.getStreamFile() : asset.getFile();

    if (file != mOpusBitrateFile)
    {
        mOpusBitrateFile = file;
        mOpusBitrate = GetAudioAssetOpusBitrate(asset);
    }

    return mOpusBitrate;
}

void AudioResourceDocument::setAssetFacts(FileFacts* facts, const AudioAsset& asset) const
{
    const AudioAssetInfo& info = asset.getInfo();

    facts->sampleRate = info.sampleRate;
    facts->sampleCount = info.sampleCount;
    facts->channelNum = info.channelCount;
    facts->isLoop = info.isLoop;
    facts->loopStartFrame = info.loopStartFrame;
    facts->loopEndFrame = info.loopEndFrame;

    aal::BwavCodec codec;

    if (!GetAudioAssetBwavCodec(asset, &codec))
        return;

    facts->encoding = cBwavCodecNames[static_cast<s32>(codec)];

    if (codec == aal::BwavCodec::Opus)
        facts->bitrate = getOpusBitrate(asset) * info.channelCount;
    else if (codec == aal::BwavCodec::Pcm16)
        facts->bitrate = CalcPcmBitrate(info.sampleRate, info.channelCount, 16);
    else
        facts->bitrate = CalcAdpcmBitrate(info.sampleRate, info.channelCount);
}

bool AudioResourceDocument::getFileFacts(FileFacts* out) const
{
    out->format = GetAudioResourceFormat(mResource);
    out->kindName = messages::common::cWaveFile;

    for (const Item* asset : mResource.getAssetList())
    {
        setAssetFacts(out, *static_cast<const AudioAsset*>(asset));
        break;
    }

    return true;
}

bool AudioResourceDocument::isValidName(const sead::SafeString& name) const
{
    return mResource.validName(name);
}

const char* AudioResourceDocument::getNameRuleDetail() const
{
    return messages::resource::cNameInvalidDetail;
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

bool AudioResourceDocument::prepareSave()
{
    sead::FixedSafeString<256> error;
    const Item* item = nullptr;

    if (mResource.validate(&error, &item))
        return true;

    MessageLog::instance()->addPopup({ error, const_cast<Item*>(item) });
    return false;
}

bool AudioResourceDocument::prepareSaveAs()
{
    return prepareSave();
}

bool AudioResourceDocument::chooseSaveAsPath(sead::BufferedSafeString* outPath) const
{
    const ResourceFormatInfo& format = GetResourceFormatInfo(mResource.getFormat());

    return showSaveAsDialog(outPath, format.displayName, format.extension, cZstdExtension);
}

SaveResult AudioResourceDocument::save(const char** outError)
{
    return mResource.save(outError);
}

SaveResult AudioResourceDocument::saveAs(const sead::SafeString& path, const char** outError)
{
    return mResource.saveAs(path, outError);
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

    AddSnapshotStreams(snapshot, streamPaths);

    *outResult = Bfsar::BackupResult::Ok;
    return true;
}

void AudioResourceDocument::copyPreviousSidecars(const std::string& sourcePath, const std::string& destDir, backup::Snapshot* snapshot)
{
    std::vector<std::string> streamPaths;
    mResource.copyPreviousStreamFiles(sead::FixedSafeString<512>(sourcePath.c_str()), destDir, &streamPaths);
    AddSnapshotStreams(snapshot, streamPaths);
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
        MessageLog::instance()->addPopup({ message, nullptr });
    }
}
