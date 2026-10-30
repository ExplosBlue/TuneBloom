#pragma once

#include <ui/file/LooseFileDocument.h>

#include <bfsar/files/LooseWaveFile.h>

class LooseWaveDocument : public LooseFileDocumentOf<LooseWaveFile>
{
public:
    static Document* Open(File* file, const sead::SafeString& path);
    static Document* Create(const InnerFileTarget& target);

    ~LooseWaveDocument() override;

    Kind kind() const override;
    BrowserFileKind fileKind() const override;
    void drawInfo(UIType tab) override;
    bool drawFileProperties() override;
    bool showsItemInInfo() const override;
    StreamSource activeStreamSource() override;
    PlayableFile playableFile() override;
    bool getFileFacts(FileFacts* out) const override;
    Item* defaultSelection() override;

    void forEachItemList(const std::function<void(Item::List&)>& visit) override;
    bool ownsItem(const Item& item) const override;

protected:
    LooseFileFormat fileFormat() const override;
};
