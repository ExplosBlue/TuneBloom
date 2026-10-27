#include <ui/file/LooseWaveDocument.h>

#include <ui/app/PopupMgr.h>
#include <ui/file/LooseFileInfo.h>
#include <ui/player/PlayerViews.h>
#include <ui/wave/LooseWaveInfo.h>

#include <browser/FileProbe.h>

Document* LooseWaveDocument::Open(File* file, const sead::SafeString& path)
{
    LooseWaveDocument* document = new LooseWaveDocument();
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

Document* LooseWaveDocument::Create(const InnerFileTarget& target)
{
    LooseWaveDocument* document = new LooseWaveDocument();
    document->mFile.create(target, MakeUntitledName(target, InnerFileKind::Wave));

    return document;
}

LooseWaveDocument::~LooseWaveDocument()
{
    if (mFile.getWave() && sSoundPlayer.getPlayingWaveFile() == mFile.getWave())
        sSoundPlayer.stopAllPlayers();

    closeWindows();
}

LooseWaveFile& LooseWaveDocument::file()
{
    return mFile;
}

Document::Kind LooseWaveDocument::kind() const
{
    return Kind::WaveFile;
}

const sead::SafeString& LooseWaveDocument::filePath() const
{
    return mFile.getFilePath();
}

std::string LooseWaveDocument::displayName() const
{
    return mFile.getFileName().cstr();
}

BrowserFileKind LooseWaveDocument::fileKind() const
{
    return BrowserFileKind::Wave;
}

void LooseWaveDocument::drawInfo(UIType)
{
    mFile.getWave()->drawEditorUI(true);
}

bool LooseWaveDocument::drawFileProperties()
{
    DrawLooseWaveInfoUI(mFile);
    return true;
}

bool LooseWaveDocument::showsItemInInfo() const
{
    return true;
}

StreamSource LooseWaveDocument::activeStreamSource()
{
    return {};
}

PlayableFile LooseWaveDocument::playableFile()
{
    PlayableFile playable;
    playable.wave = mFile.getWave();

    return playable;
}

void LooseWaveDocument::formatFileFacts(sead::BufferedSafeString* out) const
{
    const WaveFile* wave = mFile.getWave();

    FormatLooseFileFacts(out, mFile.getTarget(), InnerFileKind::Wave);

    if (!wave)
        return;

    AppendWaveFacts(out, *wave);
    AppendChannelFact(out, static_cast<u32>(wave->getChannels().size()));
}

Item* LooseWaveDocument::defaultSelection()
{
    return mFile.getWave();
}

void LooseWaveDocument::forEachItemList(const std::function<void(Item::List&)>& visit)
{
    visit(mFile.getWaveList());
}

bool LooseWaveDocument::isInfoModified() const
{
    return mFile.isModified();
}

bool LooseWaveDocument::ownsItem(const Item& item) const
{
    return item.list() == &const_cast<LooseWaveFile&>(mFile).getWaveList();
}

SaveResult LooseWaveDocument::save(const char** outError)
{
    if (mFile.getFilePath().isEmpty())
        return saveUntitled(outError);

    return reportSave(mFile.save(&mSaveError), outError);
}

SaveResult LooseWaveDocument::saveAs(const sead::SafeString& path, const char** outError)
{
    return reportSave(mFile.saveAs(path, &mSaveError), outError);
}

InnerFileTarget LooseWaveDocument::fileTarget() const
{
    return mFile.getTarget();
}

InnerFileKind LooseWaveDocument::innerFileKind() const
{
    return InnerFileKind::Wave;
}

bool LooseWaveDocument::writeTo(const sead::SafeString& path) const
{
    return mFile.writeTo(path);
}
