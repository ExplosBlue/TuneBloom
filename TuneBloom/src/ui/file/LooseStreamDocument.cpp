#include <ui/file/LooseStreamDocument.h>

#include <ui/app/PopupMgr.h>
#include <ui/file/LooseFileInfo.h>
#include <ui/player/PlayerViews.h>
#include <ui/sound/SoundViews.h>
#include <ui/stream/StreamTracks.h>
#include <ui/stream/LooseStreamInfo.h>

#include <browser/FileProbe.h>

Document* LooseStreamDocument::Open(File* file, const sead::SafeString& path)
{
    LooseStreamDocument* document = new LooseStreamDocument();
    sead::FixedSafeString<512> error;

    const bool opened = document->mFile.open(file->data, file->size, path, &error);
    Free(file);

    if (opened)
    {
        document->mLoadedPath = path;
        return document;
    }

    PopupMgr::instance()->addPopup({ error, nullptr });

    delete document;
    return nullptr;
}

Document* LooseStreamDocument::Create(const InnerFileTarget& target)
{
    LooseStreamDocument* document = new LooseStreamDocument();
    document->mFile.create(target, MakeUntitledName(target, InnerFileKind::Stream));

    return document;
}

LooseStreamDocument::~LooseStreamDocument()
{
    sSoundPlayer.forgetStream(&mFile.getStream());

    const Item* highlighted = GetHighlightedStreamTrack();

    if (highlighted && highlighted->list() == &mFile.getStream().getTrackList())
        HighlightStreamTrack(nullptr);

    closeWindows();
}

LooseStreamFile& LooseStreamDocument::file()
{
    return mFile;
}

Document::Kind LooseStreamDocument::kind() const
{
    return Kind::StreamFile;
}

const sead::SafeString& LooseStreamDocument::filePath() const
{
    return mFile.getFilePath();
}

std::string LooseStreamDocument::displayName() const
{
    return mFile.getFileName().cstr();
}

BrowserFileKind LooseStreamDocument::fileKind() const
{
    return BrowserFileKind::Stream;
}

void LooseStreamDocument::drawInfo(UIType)
{
    DrawLooseStreamContentUI(mFile);
}

bool LooseStreamDocument::drawFileProperties()
{
    DrawLooseStreamInfoUI(mFile);
    return true;
}

StreamSource LooseStreamDocument::activeStreamSource()
{
    return mFile.getStreamSource();
}

PlayableFile LooseStreamDocument::playableFile()
{
    PlayableFile playable;
    playable.stream = mFile.getStreamSource();

    return playable;
}

void LooseStreamDocument::formatFileFacts(sead::BufferedSafeString* out) const
{
    const Sound::StreamSoundInfo& stream = mFile.getStream();

    FormatLooseFileFacts(out, mFile.getTarget(), InnerFileKind::Stream);

    if (const WaveFile* mainWave = stream.getMainWave())
        AppendWaveFacts(out, *mainWave);

    AppendTrackFact(out, static_cast<u32>(stream.getTrackList().size()));
    AppendChannelFact(out, static_cast<u32>(stream.getAllocateChannelCount()));
}

void LooseStreamDocument::forEachItemList(const std::function<void(Item::List&)>& visit)
{
    visit(mFile.getStream().getTrackList());
    visit(mFile.getStreamWaveList());
}

bool LooseStreamDocument::isInfoModified() const
{
    return mFile.isModified();
}

bool LooseStreamDocument::ownsItem(const Item& item) const
{
    LooseStreamFile& file = const_cast<LooseStreamFile&>(mFile);

    return item.list() == &file.getStream().getTrackList() || item.list() == &file.getStreamWaveList();
}

SaveResult LooseStreamDocument::save(const char** outError)
{
    if (mFile.getFilePath().isEmpty())
        return saveUntitled(outError);

    return reportSave(mFile.save(&mSaveError), outError);
}

SaveResult LooseStreamDocument::saveAs(const sead::SafeString& path, const char** outError)
{
    return reportSave(mFile.saveAs(path, &mSaveError), outError);
}

InnerFileTarget LooseStreamDocument::fileTarget() const
{
    return mFile.getTarget();
}

InnerFileKind LooseStreamDocument::innerFileKind() const
{
    return InnerFileKind::Stream;
}

bool LooseStreamDocument::writeTo(const sead::SafeString& path) const
{
    return mFile.writeTo(path);
}
