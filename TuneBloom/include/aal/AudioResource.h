#pragma once

#include <aal/AudioAsset.h>
#include <core/SaveResult.h>
#include <bfsar/files/WaveFile.h>

#include <prim/seadSafeString.h>

#include <string>
#include <vector>

class AudioResource
{
public:
    enum class Container
    {
        Bars,
        Bwav
    };

    static constexpr s32 cNameLengthMax = 128;

    AudioResource();
    ~AudioResource();

    static bool IsResourceFile(const void* data, u64 size);
    static const char* GetResourceDataError(const void* data, u64 size);

    bool open(std::vector<u8> bytes, const sead::SafeString& path, const char** outError);
    void close();
    bool isOpen() const;

    SaveResult save(const char** outError);
    SaveResult saveAs(const sead::SafeString& path, const char** outError);
    bool validate(sead::BufferedSafeString* outError, const Item** outItem) const;
    void captureItemBaselines();

    const sead::SafeString& getFilePath() const;
    Container getContainer() const;
    ResourceFormat getFormat() const;
    bool isCompressed() const;
    bool isBigEndian() const;
    u16 getVersion() const;

    Item::List& getAssetList();
    const Item::List& getAssetList() const;

    bool validName(const sead::SafeString& name) const;
    bool validateName(const sead::SafeString& name, const Item* ignoreItem) const;

    bool canRenameAsset(const AudioAsset& asset) const;
    bool canRemoveAssets() const;
    bool canAddAssets() const;
    bool canDuplicateAsset(const AudioAsset& asset, const char** outReason) const;
    const AudioAsset* findTemplateAsset(const Item* preferred) const;
    AudioAsset* createAsset(const AudioAsset& source) const;
    AudioAsset* duplicateAsset(AudioAsset* source, const char** outError);

    bool canReplaceAudio(const AudioAsset& asset, const char** outReason) const;
    bool canSetLoop(const AudioAsset& asset, const char** outReason) const;
    bool replaceAudio(AudioAsset* asset, const WaveFile::RiffWaveInfo& info, WaveFile::Encoding encoding, const char** outError);
    bool replaceFile(AudioAsset* asset, std::vector<u8> file, const char** outError);
    bool setLoop(AudioAsset* asset, bool isLoop, u32 loopStartFrame, const char** outError);

    void renumberAssets();
    void loadStreamFile(AudioAsset* asset);
    std::string getStreamFilePath(const AudioAsset& asset) const;

    std::vector<std::string> getStreamWritePaths(const sead::SafeString& destinationPath) const;
    bool writeBackup(const std::string& directory, const std::string& fileName, std::vector<std::string>* outStreamPaths) const;
    bool restoreStreamFile(const std::string& relativePath, std::vector<u8> bytes);

private:
    struct StreamWrite
    {
        AudioAsset* asset = nullptr;
        std::string path;
        std::string tempPath;
    };

    bool openBars(std::vector<u8> bytes, const char** outError);
    bool writesStreamFile(const AudioAsset& asset, bool isNewLocation) const;
    bool copiesStreamFile(const AudioAsset& asset, bool isNewLocation) const;
    bool canEditStreamAudio(const AudioAsset& asset, const char** outReason) const;
    bool openBwav(std::vector<u8> bytes, const sead::SafeString& path, const char** outError);
    void probeStreamFile(AudioAsset* asset) const;
    void syncMetaNames();
    bool buildFile(std::vector<u8>* out, const char** outError) const;
    bool buildSavedBytes(std::vector<u8>* out, const char** outError) const;
    bool stageStreamFiles(const sead::SafeString& previousPath, std::vector<StreamWrite>* out, const char** outError) const;
    bool commitStreamFiles(const std::vector<StreamWrite>& writes) const;
    bool writeArchive(const char** outError);
    SaveResult writeAll(const sead::SafeString& previousPath, const char** outError);

    bool mIsOpen;
    Container mContainer;
    bool mIsCompressed;
    bool mIsBigEndian;
    u16 mVersion;
    std::vector<u8> mTrailing;
    sead::FixedSafeString<512> mFilePath;
    Item::List mAssets;
};
