#pragma once

#include <aal/Amta.h>
#include <aal/AudioAsset.h>
#include <aal/BarsParam.h>
#include <aal/Bwav.h>
#include <aal/OldAudioData.h>
#include <core/Sarc.h>
#include <core/SaveResult.h>
#include <bfsar/files/WaveFile.h>

#include <prim/seadSafeString.h>

#include <string>
#include <vector>

struct AudioAssetEncoding
{
    WaveFile::Encoding fwavEncoding = WaveFile::Encoding::DspAdpcm;
    aal::BwavCodec bwavCodec = aal::BwavCodec::DspAdpcm;
    u32 opusChannelBitrate = aal::cBwavOpusChannelBitrate;
};

enum class StreamFolderName
{
    Plain,
    StemSuffix,
    StemPrefix
};

struct StreamFolder
{
    std::string parent;
    StreamFolderName name = StreamFolderName::Plain;
};

class AudioResource
{
public:
    enum class Container
    {
        Bars,
        Bwav,
        Sarc
    };

    static constexpr s32 cNameLengthMax = 128;

    AudioResource();
    ~AudioResource();

    static bool IsResourceFile(const void* data, u64 size);
    static const char* GetResourceDataError(const void* data, u64 size);

    bool open(std::vector<u8> bytes, const sead::SafeString& path, const char** outError);
    void createBwav(const sead::SafeString& fileName);
    void createBars(u16 version, AudioAssetKind newAssetKind);
    void createSarc(bool hasMeta);
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
    u32 getOtherFileCount() const;

    Item::List& getAssetList();
    const Item::List& getAssetList() const;
    bool holdsStreams() const;

    bool validName(const sead::SafeString& name) const;
    bool validateName(const sead::SafeString& name, const Item* ignoreItem) const;

    bool canRenameAsset(const AudioAsset& asset) const;
    bool canRemoveAssets() const;
    bool canAddAssets() const;
    bool canCreateStreamAsset() const;
    bool canDuplicateAsset(const AudioAsset& asset, const char** outReason) const;
    AudioAsset* createAsset(const AudioAsset& source) const;
    AudioAsset* createNewAsset(const WaveFile::RiffWaveInfo& info, const AudioAssetEncoding& encoding, bool isStream, const char** outError);
    AudioAssetKind getNewAssetKind() const;
    bool canChooseNewAssetKind() const;
    void setNewAssetKind(AudioAssetKind kind);
    AudioAsset* duplicateAsset(AudioAsset* source, const char** outError);

    bool canReplaceAudio(const AudioAsset& asset, const char** outReason) const;
    bool canReimportAudio(const AudioAsset& asset, const char** outReason) const;
    bool canSetLoop(const AudioAsset& asset, const char** outReason) const;
    OldAudioData findOldAudioData(const AudioAsset& asset) const;
    bool replaceAudio(AudioAsset* asset, const WaveFile::RiffWaveInfo& info, const aal::AmtaSourceAudio& source, const AudioAssetEncoding& encoding, const OldAudioClear& cleared, const char** outError);
    bool replaceFile(AudioAsset* asset, std::vector<u8> file, const char** outError);
    bool setLoop(AudioAsset* asset, bool isLoop, u32 loopStartFrame, const char** outError);
    bool canEditLabels(const AudioAsset& asset, const char** outReason) const;
    bool canEditMarkers(const AudioAsset& asset, const char** outReason) const;
    bool dropsLastSample();
    f32 getQuietTailLevel();
    bool setLabels(AudioAsset* asset, const aal::AmtaLabels& labels, const char** outError);

    void renumberAssets();
    void loadStreamFile(AudioAsset* asset);
    std::string getStreamFilePath(const AudioAsset& asset) const;

    void copyPreviousStreamFiles(const sead::SafeString& destinationPath, const std::string& directory, std::vector<std::string>* outStreamPaths) const;
    bool writeBackup(const std::string& directory, const std::string& fileName, std::vector<std::string>* outStreamPaths) const;
    bool restoreStreamFile(const std::string& relativePath, std::vector<u8> bytes);

private:
    struct StreamWrite
    {
        AudioAsset* asset = nullptr;
        std::string path;
        std::string tempPath;
    };

    struct SarcParam
    {
        bool isPresent = false;
        aal::BarsParam param;
        sarc::Entry entry;
        std::vector<u64> assetUids;
    };

    bool openBars(std::vector<u8> bytes, const char** outError);
    bool openSarc(std::vector<u8> bytes, const char** outError);
    void openSarcParam(std::vector<sarc::Entry>* otherFiles);
    void buildSarcParam(sarc::File* file) const;
    void findStreamFolders();
    bool findBaseStreamFolder(const std::vector<const AudioAsset*>& streams);
    void fillLoudnessChoices();
    bool canEditAssetList() const;
    bool canCreateNewAsset() const;
    bool isNewMetaBigEndian() const;
    bool hasNewAssetMeta() const;
    u32 getNewFwavVersion() const;
    u32 getNewMetaInfoMask() const;
    bool writesStreamFile(const AudioAsset& asset, bool isNewLocation) const;
    bool copiesStreamFile(const AudioAsset& asset, bool isNewLocation) const;
    bool usesStreamFile(const AudioAsset& asset) const;
    bool canEditStreamAudio(const AudioAsset& asset, const char** outReason) const;
    bool openBwav(std::vector<u8> bytes, const sead::SafeString& path, const char** outError);
    void probeStreamFile(AudioAsset* asset) const;
    void syncMetaNames();
    bool buildFile(std::vector<u8>* out, const char** outError) const;
    bool buildBars(std::vector<u8>* out, const char** outError) const;
    bool buildSarc(std::vector<u8>* out, const char** outError) const;
    bool buildSavedBytes(std::vector<u8>* out, const char** outError) const;
    bool stageStreamFiles(const sead::SafeString& previousPath, const StreamFolder& previousFolder, std::vector<StreamWrite>* out, const char** outError) const;
    bool commitStreamFiles(const std::vector<StreamWrite>& writes) const;
    std::string getStreamWritePath(const AudioAsset& asset) const;
    std::string getStreamBackupPath(const sead::SafeString& archivePath, const AudioAsset& asset) const;
    bool writeArchive(const char** outError);
    SaveResult writeAll(const sead::SafeString& previousPath, const StreamFolder& previousFolder, const char** outError);

    bool mIsOpen;
    Container mContainer;
    bool mIsCompressed;
    bool mIsBigEndian;
    StreamFolder mStreamFolder;
    std::string mBaseTitleFolder;
    StreamFolder mBaseStreamFolder;
    u16 mVersion;
    AudioAssetKind mNewAssetKind;
    bool mNewAssetHasMeta;
    s8 mLastSampleDrop;
    s8 mQuietTailLevel;
    std::vector<u8> mTrailing;
    sarc::File mSarcFiles;
    SarcParam mSarcParam;
    sead::FixedSafeString<512> mFilePath;
    Item::List mAssets;
};
