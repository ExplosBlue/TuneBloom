#pragma once

#include <ui/file/Document.h>

#include <aal/AudioResource.h>

class AudioResourceDocument : public Document
{
public:
    static Document* Open(File* file, const sead::SafeString& path);
    static Document* CreateBwav();
    static Document* CreateBars(u16 version, AudioAssetKind newAssetKind);
    static Document* CreateSarc(bool hasMeta);

    ~AudioResourceDocument() override;

    AudioResource& resource();

    Kind kind() const override;
    const sead::SafeString& filePath() const override;
    const sead::SafeString& loadedPath() const override;
    std::string displayName() const override;
    const char* extension() const override;
    BrowserFileKind fileKind() const override;
    UIType defaultTab() const override;
    void drawInfo(UIType tab) override;
    bool drawFileProperties() override;
    StreamSource activeStreamSource() override;
    PlayableFile playableFile() override;
    void formatFileFacts(sead::BufferedSafeString* out) const override;

    bool isValidName(const sead::SafeString& name) const override;
    const char* getNameRuleDetail() const override;
    bool isUniqueName(const sead::SafeString& name, const Item* ignoreItem) const override;
    void renumber(Item::List& list) override;
    void forEachItemList(const std::function<void(Item::List&)>& visit) override;
    void captureItemBaselines() override;
    bool isInfoModified() const override;
    bool ownsItem(const Item& item) const override;

    ParseContext parseContext() const override;

    bool prepareSave() override;
    bool prepareSaveAs() override;
    bool chooseSaveAsPath(sead::BufferedSafeString* outPath) const override;
    SaveResult save(const char** outError) override;
    SaveResult saveAs(const sead::SafeString& path, const char** outError) override;

    bool writeBackup(const BackupWrite& write, backup::Snapshot* snapshot, Bfsar::BackupResult* outResult) override;
    void copyPreviousSidecars(const std::string& sourcePath, const std::string& destDir, backup::Snapshot* snapshot) override;
    void restoreSidecars(const backup::Snapshot& snapshot) override;

private:
    AudioResource mResource;
};
