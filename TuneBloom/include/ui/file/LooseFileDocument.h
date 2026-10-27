#pragma once

#include <ui/file/Document.h>

#include <bfsar/files/InnerFile.h>

class LooseFileDocument : public Document
{
public:
    const sead::SafeString& loadedPath() const override;
    const char* extension() const override;
    UIType defaultTab() const override;

    bool isValidName(const sead::SafeString& name) const override;
    bool isUniqueName(const sead::SafeString& name, const Item* ignoreItem) const override;
    void renumber(Item::List& list) override;
    void captureItemBaselines() override;

    ParseContext parseContext() const override;
    u64 estimateMemory() const override;
    bool isLoaded() const override;
    bool canUnload() const override;
    void unload() override;
    bool reload() override;

    bool prepareSave() override;
    bool prepareSaveAs() override;
    bool chooseSaveAsPath(sead::BufferedSafeString* outPath) const override;
    void finishSave() override;

    s32 getBackupNamesSetting() const override;
    bool writeBackup(const BackupWrite& write, backup::Snapshot* snapshot, Bfsar::BackupResult* outResult) override;
    void copyPreviousSidecars(const std::string& sourcePath, const std::string& destDir, backup::Snapshot* snapshot) override;
    void restoreSidecars(const backup::Snapshot& snapshot) override;
    void applyRestoredSnapshot(const backup::Snapshot& snapshot, bool untitled) override;

protected:
    virtual InnerFileTarget fileTarget() const = 0;
    virtual InnerFileKind innerFileKind() const = 0;
    virtual bool writeTo(const sead::SafeString& path) const = 0;

    static sead::FixedSafeString<64> MakeUntitledName(const InnerFileTarget& target, InnerFileKind kind);

    SaveResult reportSave(SaveResult result, const char** outError);
    SaveResult saveUntitled(const char** outError);

    sead::FixedSafeString<512> mLoadedPath;
    sead::FixedSafeString<512> mSaveError;
};
