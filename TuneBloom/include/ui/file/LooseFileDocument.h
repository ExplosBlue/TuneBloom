#pragma once

#include <ui/file/Document.h>

#include <ui/file/LooseFileInfo.h>

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

    bool chooseSaveAsPath(sead::BufferedSafeString* outPath) const override;
    void finishSave() override;

    bool writeBackup(const BackupWrite& write, backup::Snapshot* snapshot, Bfsar::BackupResult* outResult) override;

protected:
    virtual LooseFileFormat fileFormat() const = 0;
    virtual bool openFile(const File& file, const sead::SafeString& path, sead::BufferedSafeString* outError) = 0;
    virtual bool writeTo(const sead::SafeString& path) const = 0;

    static Document* OpenAs(LooseFileDocument* document, File* file, const sead::SafeString& path);
    static sead::FixedSafeString<64> MakeUntitledName(const char* extension);

    SaveResult reportSave(SaveResult result, const char** outError);

    sead::FixedSafeString<512> mLoadedPath;
    sead::FixedSafeString<512> mSaveError;
};

template <typename LooseFile>
class LooseFileDocumentOf : public LooseFileDocument
{
public:
    LooseFile& file()
    {
        return mFile;
    }

    const sead::SafeString& filePath() const override
    {
        return mFile.getFilePath();
    }

    std::string displayName() const override
    {
        return mFile.getFileName().cstr();
    }

    bool isInfoModified() const override
    {
        return mFile.isModified();
    }

    SaveResult save(const char** outError) override
    {
        return reportSave(mFile.save(&mSaveError), outError);
    }

    SaveResult saveAs(const sead::SafeString& path, const char** outError) override
    {
        return reportSave(mFile.saveAs(path, &mSaveError), outError);
    }

protected:
    bool openFile(const File& file, const sead::SafeString& path, sead::BufferedSafeString* outError) override
    {
        return mFile.open(file.data, file.size, path, outError);
    }

    bool writeTo(const sead::SafeString& path) const override
    {
        return mFile.writeTo(path);
    }

    LooseFile mFile;
};
