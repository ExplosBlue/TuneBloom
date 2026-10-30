#include <ui/file/LooseStreamDocument.h>

#include <ui/file/LooseFileInfo.h>
#include <messages/Messages.h>
#include <ui/player/PlayerViews.h>
#include <ui/sound/SoundViews.h>
#include <ui/stream/StreamTracks.h>
#include <ui/stream/LooseStreamInfo.h>

#include <scan/FileProbe.h>

Document* LooseStreamDocument::Open(File* file, const sead::SafeString& path)
{
    return OpenAs(new LooseStreamDocument(), file, path);
}

Document* LooseStreamDocument::Create(const InnerFileTarget& target, Sound::StreamSoundInfo::StreamType streamType)
{
    const bool isOpus = streamType == Sound::StreamSoundInfo::StreamType::Opus;
    const char* fileExtension = isOpus ? cOpusExtension : GetInnerFileExtension(target.format, InnerFileKind::Stream);

    LooseStreamDocument* document = new LooseStreamDocument();
    document->mFile.create(target, streamType, MakeUntitledName(fileExtension));

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

Document::Kind LooseStreamDocument::kind() const
{
    return Kind::StreamFile;
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

bool LooseStreamDocument::getFileFacts(FileFacts* out) const
{
    const Sound::StreamSoundInfo& stream = mFile.getStream();
    const WaveFile* mainWave = stream.getMainWave();

    out->format = fileFormat();
    out->kindName = messages::common::cStream;

    if (mainWave)
        SetWaveFacts(out, *mainWave);

    out->trackNum = static_cast<u32>(stream.getTrackList().size());
    out->channelNum = static_cast<u32>(stream.getAllocateChannelCount());

    if (stream.getStreamType() == Sound::StreamSoundInfo::StreamType::Opus)
    {
        out->encoding = messages::stream::cTypeOpus;
        out->bitrate = stream.getOriginalOpusChannelBitrate() * out->channelNum;
    }
    else if (mainWave)
    {
        out->bitrate = CalcWaveBitrate(*mainWave, out->channelNum);
    }

    return true;
}

void LooseStreamDocument::forEachItemList(const std::function<void(Item::List&)>& visit)
{
    visit(mFile.getStream().getTrackList());
    visit(mFile.getStreamWaveList());
}

bool LooseStreamDocument::ownsItem(const Item& item) const
{
    LooseStreamFile& file = const_cast<LooseStreamFile&>(mFile);

    return item.list() == &file.getStream().getTrackList() || item.list() == &file.getStreamWaveList();
}

LooseFileFormat LooseStreamDocument::fileFormat() const
{
    return GetLooseStreamFormat(mFile);
}
