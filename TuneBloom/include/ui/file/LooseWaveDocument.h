#pragma once

#include <ui/file/LooseFileDocument.h>

#include <bfsar/files/LooseWaveFile.h>

class LooseWaveDocument : public LooseFileDocument
{
public:
    static Document* Open(File* file, const sead::SafeString& path);
    static Document* Create(const InnerFileTarget& target);

    ~LooseWaveDocument() override;

    LooseWaveFile& file();

    Kind kind() const override;
    const sead::SafeString& filePath() const override;
    std::string displayName() const override;
    BrowserFileKind fileKind() const override;
    void drawInfo(UIType tab) override;
    bool drawFileProperties() override;
    bool showsItemInInfo() const override;
    StreamSource activeStreamSource() override;
    PlayableFile playableFile() override;
    void formatFileFacts(sead::BufferedSafeString* out) const override;
    Item* defaultSelection() override;

    void forEachItemList(const std::function<void(Item::List&)>& visit) override;
    bool isInfoModified() const override;
    bool ownsItem(const Item& item) const override;

    SaveResult save(const char** outError) override;
    SaveResult saveAs(const sead::SafeString& path, const char** outError) override;

protected:
    InnerFileTarget fileTarget() const override;
    InnerFileKind innerFileKind() const override;
    bool writeTo(const sead::SafeString& path) const override;

private:
    LooseWaveFile mFile;
};
