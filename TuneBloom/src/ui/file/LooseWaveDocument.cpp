#include <ui/file/LooseWaveDocument.h>

#include <ui/file/LooseFileInfo.h>
#include <messages/Messages.h>
#include <ui/player/PlayerViews.h>
#include <ui/wave/LooseWaveInfo.h>

#include <scan/FileProbe.h>

Document* LooseWaveDocument::Open(File* file, const sead::SafeString& path)
{
    return OpenAs(new LooseWaveDocument(), file, path);
}

Document* LooseWaveDocument::Create(const InnerFileTarget& target)
{
    LooseWaveDocument* document = new LooseWaveDocument();
    document->mFile.create(target, MakeUntitledName(GetInnerFileExtension(target.format, InnerFileKind::Wave)));

    return document;
}

LooseWaveDocument::~LooseWaveDocument()
{
    if (mFile.getWave() && sSoundPlayer.getPlayingWaveFile() == mFile.getWave())
        sSoundPlayer.stopAllPlayers();

    closeWindows();
}

Document::Kind LooseWaveDocument::kind() const
{
    return Kind::WaveFile;
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

bool LooseWaveDocument::getFileFacts(FileFacts* out) const
{
    out->format = fileFormat();
    out->kindName = messages::common::cWaveFile;

    if (const WaveFile* wave = mFile.getWave())
        SetWaveFacts(out, *wave);

    return true;
}

Item* LooseWaveDocument::defaultSelection()
{
    return mFile.getWave();
}

void LooseWaveDocument::forEachItemList(const std::function<void(Item::List&)>& visit)
{
    visit(mFile.getWaveList());
}

bool LooseWaveDocument::ownsItem(const Item& item) const
{
    return item.list() == &const_cast<LooseWaveFile&>(mFile).getWaveList();
}

LooseFileFormat LooseWaveDocument::fileFormat() const
{
    return GetLooseFileFormat(mFile.getTarget(), InnerFileKind::Wave);
}
