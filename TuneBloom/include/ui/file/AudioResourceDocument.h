#pragma once

#include <ui/file/Document.h>

#include <aal/AudioResource.h>

class AudioResourceDocument : public Document
{
public:
    static Document* Open(File* file, const sead::SafeString& path);

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

    bool isValidName(const sead::SafeString& name) const override;
    bool isUniqueName(const sead::SafeString& name, const Item* ignoreItem) const override;
    void renumber(Item::List& list) override;
    void forEachItemList(const std::function<void(Item::List&)>& visit) override;
    void captureItemBaselines() override;
    bool isInfoModified() const override;
    bool ownsItem(const Item& item) const override;

    ParseContext parseContext() const override;
    u64 estimateMemory() const override;
    bool isLoaded() const override;
    bool canUnload() const override;
    void unload() override;
    bool reload() override;

    bool prepareSave() override;
    bool prepareSaveAs() override;
    bool chooseSaveAsPath(sead::BufferedSafeString* outPath) const override;
    SaveResult save(const char** outError) override;
    SaveResult saveAs(const sead::SafeString& path, const char** outError) override;
    void finishSave() override;

    s32 getBackupNamesSetting() const override;
    bool writeBackup(const BackupWrite& write, backup::Snapshot* snapshot, Bfsar::BackupResult* outResult) override;
    void copyPreviousSidecars(const std::string& sourcePath, const std::string& destDir, backup::Snapshot* snapshot) override;
    void restoreSidecars(const backup::Snapshot& snapshot) override;
    void applyRestoredSnapshot(const backup::Snapshot& snapshot, bool untitled) override;

private:
    AudioResource mResource;
};
