#pragma once

#include <ui/file/LooseFileDocument.h>

#include <aal/BarsListFile.h>

class BarsListDocument : public LooseFileDocument
{
public:
    static Document* Open(File* file, const sead::SafeString& path);

    ~BarsListDocument() override;

    BarsListFile& file();

    Kind kind() const override;
    const sead::SafeString& filePath() const override;
    std::string displayName() const override;
    BrowserFileKind fileKind() const override;
    UIType defaultTab() const override;
    void drawInfo(UIType tab) override;
    bool drawFileProperties() override;
    StreamSource activeStreamSource() override;

    bool isValidName(const sead::SafeString& name) const override;
    const char* getNameRuleDetail() const override;
    bool isUniqueName(const sead::SafeString& name, const Item* ignoreItem) const override;
    void renumber(Item::List& list) override;
    void forEachItemList(const std::function<void(Item::List&)>& visit) override;
    bool isInfoModified() const override;
    bool ownsItem(const Item& item) const override;

    SaveResult save(const char** outError) override;
    SaveResult saveAs(const sead::SafeString& path, const char** outError) override;

protected:
    LooseFileFormat fileFormat() const override;
    bool openFile(const File& file, const sead::SafeString& path, sead::BufferedSafeString* outError) override;
    bool writeTo(const sead::SafeString& path) const override;

private:
    BarsListFile mFile;
};

BarsListFile* FindActiveBarsList();
