#pragma once

#include <bfsar/archive/Bea.h>
#include <bfsar/archive/Bfsar.h>
#include <core/SaveResult.h>
#include <ui/app/Workspace.h>

#include <prim/seadSafeString.h>

#include <functional>
#include <string>
#include <vector>

class AudioAsset;
class AudioResource;
struct DocumentState;

struct PlayableFile
{
    StreamSource stream;
    WaveFile* wave = nullptr;
    AudioAsset* asset = nullptr;

    bool isValid() const
    {
        return stream.isValid() || wave || asset;
    }
};

enum class BrowserFileKind;

namespace backup
{

struct Snapshot;

}

class Document
{
public:
    enum class Kind
    {
        SoundArchive,
        AudioResource,
        StreamFile,
        WaveFile
    };

    struct File
    {
        Kind kind = Kind::SoundArchive;
        u8* data = nullptr;
        u32 size = 0;
        bool wasCmpbin = false;
        bool prefersZstd = false;
        bea::Source container;
    };

    struct ParseContext
    {
        sead::Endian::Types endian = sead::Endian::eBig;
        bool usesV3SoundInfo = false;

        bool operator==(const ParseContext& other) const
        {
            return endian == other.endian && usesV3SoundInfo == other.usesV3SoundInfo;
        }
    };

    struct BackupWrite
    {
        std::string dir;
        std::string previousDir;
        std::vector<Bfsar::BackupStream> previousStreams;
    };

    static const char* Load(const sead::SafeString& path, File* outFile);
    static void Free(File* file);
    static bool Check(const File& file, const char** outError);
    static bool PeekEndian(const File& file, sead::Endian::Types* outEndian);
    static bool IsCompleteFile(const u8* header, u64 fileSize);
    static Document* Open(File* file, const sead::SafeString& path, const sead::SafeString* metadataSource = nullptr);
    static Document* Create(ArchiveFormat format, ArchivePlatform platform);

    static ParseContext CurrentParseContext();
    static void ApplyParseContext(const ParseContext& context);

    Document();
    virtual ~Document();

    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    DocumentState& state();
    const DocumentState& state() const;
    std::string archiveName() const;

    virtual Kind kind() const = 0;
    virtual const sead::SafeString& filePath() const = 0;
    virtual const sead::SafeString& loadedPath() const = 0;
    virtual std::string displayName() const = 0;
    virtual const char* extension() const = 0;
    virtual BrowserFileKind fileKind() const = 0;
    virtual UIType defaultTab() const = 0;
    virtual void drawInfo(UIType tab) = 0;
    virtual StreamSource activeStreamSource() = 0;

    virtual Item* defaultSelection()
    {
        return nullptr;
    }

    virtual bool hasProjectList() const
    {
        return false;
    }

    virtual bool drawFileProperties()
    {
        return false;
    }

    virtual bool showsItemInInfo() const
    {
        return false;
    }

    virtual PlayableFile playableFile()
    {
        return {};
    }

    virtual void formatFileFacts(sead::BufferedSafeString*) const
    {
    }

    virtual bool isValidName(const sead::SafeString& name) const = 0;
    virtual bool isUniqueName(const sead::SafeString& name, const Item* ignoreItem) const = 0;
    virtual void renumber(Item::List& list) = 0;
    virtual void forEachItemList(const std::function<void(Item::List&)>& visit) = 0;
    virtual void captureItemBaselines() = 0;
    virtual bool isInfoModified() const = 0;
    virtual bool ownsItem(const Item& item) const = 0;

    virtual ParseContext parseContext() const = 0;
    virtual u64 estimateMemory() const = 0;
    virtual bool isLoaded() const = 0;
    virtual bool canUnload() const = 0;
    virtual void unload() = 0;
    virtual bool reload() = 0;

    virtual bool prepareSave() = 0;
    virtual bool prepareSaveAs() = 0;
    virtual bool chooseSaveAsPath(sead::BufferedSafeString* outPath) const = 0;
    virtual SaveResult save(const char** outError) = 0;
    virtual SaveResult saveAs(const sead::SafeString& path, const char** outError) = 0;
    virtual void finishSave() = 0;

    virtual s32 getBackupNamesSetting() const = 0;
    virtual bool writeBackup(const BackupWrite& write, backup::Snapshot* snapshot, Bfsar::BackupResult* outResult) = 0;
    virtual void copyPreviousSidecars(const std::string& sourcePath, const std::string& destDir, backup::Snapshot* snapshot) = 0;
    virtual void restoreSidecars(const backup::Snapshot& snapshot) = 0;
    virtual void applyRestoredSnapshot(const backup::Snapshot& snapshot, bool untitled) = 0;

protected:
    void closeWindows();

    static void AddInnerExtension(sead::BufferedSafeString* path, const char* innerExtension, const char* outerExtension);

private:
    DocumentState* mState;
};

Document* ActiveDocument();

bool IsSoundArchiveOpen();
Bfsar& ActiveSoundArchive();
Bfsar* FindActiveSoundArchive();

AudioResource& ActiveAudioResource();
AudioResource* FindActiveAudioResource();

