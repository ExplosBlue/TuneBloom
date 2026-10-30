#pragma once

#include <ui/file/LooseFileDocument.h>

class WaveFile;

class WavDocument : public LooseFileDocument
{
public:
    static Document* Open(File* file, const sead::SafeString& path);

    ~WavDocument() override;

    Kind kind() const override;
    const sead::SafeString& filePath() const override;
    std::string displayName() const override;
    BrowserFileKind fileKind() const override;
    void drawInfo(UIType tab) override;
    bool isPlayerOnly() const override;
    StreamSource activeStreamSource() override;
    PlayableFile playableFile() override;
    bool getFileFacts(FileFacts* out) const override;

    void forEachItemList(const std::function<void(Item::List&)>& visit) override;
    bool isInfoModified() const override;
    bool ownsItem(const Item& item) const override;

    bool prepareSave() override;
    bool prepareSaveAs() override;
    SaveResult save(const char** outError) override;
    SaveResult saveAs(const sead::SafeString& path, const char** outError) override;

protected:
    LooseFileFormat fileFormat() const override;
    bool openFile(const File& file, const sead::SafeString& path, sead::BufferedSafeString* outError) override;
    bool writeTo(const sead::SafeString& path) const override;

private:
    sead::FixedSafeString<512> mFilePath;
    sead::FixedSafeString<256> mFileName;
    Item::List mWaveList;
    WaveFile* mWave = nullptr;
    u16 mBitsPerSample = 0;
    u16 mChannelNum = 0;
    bool mIsFloat = false;
};
