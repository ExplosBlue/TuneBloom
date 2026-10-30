#pragma once

#include <ui/file/LooseFileDocument.h>

#include <bfsar/files/LooseStreamFile.h>

class LooseStreamDocument : public LooseFileDocumentOf<LooseStreamFile>
{
public:
    static Document* Open(File* file, const sead::SafeString& path);
    static Document* Create(const InnerFileTarget& target, Sound::StreamSoundInfo::StreamType streamType);

    ~LooseStreamDocument() override;

    Kind kind() const override;
    BrowserFileKind fileKind() const override;
    void drawInfo(UIType tab) override;
    bool drawFileProperties() override;
    StreamSource activeStreamSource() override;
    PlayableFile playableFile() override;
    bool getFileFacts(FileFacts* out) const override;

    void forEachItemList(const std::function<void(Item::List&)>& visit) override;
    bool ownsItem(const Item& item) const override;

protected:
    LooseFileFormat fileFormat() const override;
};

void ForgetLooseStream(const Sound::StreamSoundInfo& stream);
