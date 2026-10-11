#pragma once

#include <bfsar/model/Item.h>
#include <bfsar/files/InnerFile.h>

#include <ui/sequence/SeqTextInfo.h>

#include <vector>
#include <string>
#include <unordered_map>

class TextEditor;
struct SeqCompileState;
struct SeqToolbarState;

class SequenceFile : public Item, public InnerFile
{
public:
    struct LabelStartInfo
    {
        u32 offset;

        u32 parsedOffset; // If offset points to a alloctrack command this is offset by +3 bytes
        u32 allocTrackFlags;
    };

    static const u32 cInvaldOffset = 0xFFFFFFFF;

public:
    SequenceFile()
        : Item()
        , InnerFile()
        , mIsValid(false)

        , mIsDirty(false)
        , mLoadError(false)
        , mTextEditor(nullptr)
        , mSeqTextInfo()
        , mSeqText(nullptr)

        , mSeqBytes(nullptr)
        , mSeqBytesSize(0)
        , mLabelsStartInfo()
        , mLabels()
        , mOffsetToLine()

        , mStartLabel()
    {
        mItemType = ItemType::SequenceFile;

        for (u32 i = 0; i < 4; i++)
        {
            mBankRefs[i] = new ItemReference(this);
        }
    }

    ~SequenceFile() override;

    void onOpenFileWindow() override;
    void onCloseFileWindow() override;

    const Item* validate(sead::BufferedSafeString& error) const override;

    bool isStateTracked() const override
    {
        return true;
    }

    u64 computeStateSignature() const override;

    void drawUI() override;
    void drawFileUI();

private:
    bool doRead(const void* fileAddr) override;
    u32 doWrite(sead::FileHandle* handle, sead::WriteStream* stream, bool isLast) const override;

    void setSeqBytes_(const u8* bytes, u32 size);
    void freeSeqBytes_();
    void discardSeqBytes_();

    void compile_(bool setCursorPos = true);
    std::vector<std::string> getSourceLines_() const;
    bool parseLines_(SeqCompileState& state);
    bool defineLabel_(SeqCompileState& state, const std::string& label, u32 line);
    bool encodeCommands_(SeqCompileState& state);
    bool resolveLabels_(SeqCompileState& state);
    void storeCompiled_(SeqCompileState& state);
    void setCompileError_(const SeqCompileState& state, u32 line, const std::string& errorMsg);

    std::vector<std::string> getEditorLines_() const;
    void showEditorError_(u32 line, const std::string& errorMsg, bool setCursorPos);
    void clearEditorMarkers_();
    void deleteEditor_();

public:
    bool isValid() const
    {
        return mIsValid;
    }

    bool isDirty() const
    {
        return mIsDirty;
    }

    const u8* getSeqBytes() const
    {
        return mSeqBytes;
    }

    u32 getSeqBytesSize() const
    {
        return mSeqBytesSize;
    }

    u32 getSeqBytesRevision() const
    {
        return mSeqBytesRevision;
    }

    u32 getLabelOffset(const sead::SafeString& label, bool parsed = true) const;
    u32 getLabelAllocTracks(const sead::SafeString& label) const;

    std::string getLabelFromParsedOffset(u32 offset, u32 allocTrackFlags) const;

    const std::vector<std::string>& getLabels() const
    {
        return mLabels;
    }

    ItemReference* getBankRef_(u32 index)
    {
        SEAD_ASSERT(index < 4);
        return mBankRefs[index];
    }

    sead::FixedSafeString<128>& getStartLabel_()
    {
        return mStartLabel;
    }

    void setLoadError_()
    {
        mLoadError = true;
    }

    bool recompileIfInvalid_();

    void setCursorToLabel_(const sead::SafeString& label);

    SeqTextInfo& getSeqTextInfo_()
    {
        return mSeqTextInfo;
    }

    const std::string& getMd5Hash();
    void setSerializedHash(const std::string& hash);

    void refreshSerializedHash();

private:
    void invalidatePlayer_() const;

    void play_(s32 headerLine);
    void drawToolbar_();
    void calcToolbarState_(SeqToolbarState& bar) const;
    void drawTransportTools_(const SeqToolbarState& bar);
    void drawViewTools_(const SeqToolbarState& bar);
    void updatePlayMarkers_(const SeqToolbarState& bar);
    void drawBankSelector_(const char* name, f32 width);
    void syncRenamedLabels_();
    void updateAnnotations_();

private:
    bool mIsValid;

    bool mIsDirty;
    bool mLoadError;
    TextEditor* mTextEditor;
    bool mHasPlayMarkers = false;
    bool mAnnotationsDirty = true;
    bool mMd5Dirty = false;
    ArchiveFormat mMd5Format = ArchiveFormat::BFSAR;
    u32 mMd5Version = 0;
    sead::Endian::Types mMd5Endian = sead::Endian::eBig;
    f64 mAutoCompileTime = 0.0;
    SeqTextInfo mSeqTextInfo;
    sead::HeapSafeString* mSeqText;

    u8* mSeqBytes;
    u32 mSeqBytesSize;
    u32 mSeqBytesRevision = 0;
    std::unordered_map<std::string, LabelStartInfo> mLabelsStartInfo;
    std::vector<std::string> mLabels;
    std::vector<std::string> mCompiledLabels;
    std::unordered_map<u32, u32> mOffsetToLine;

    ItemReference* mBankRefs[4];
    sead::FixedSafeString<128> mStartLabel;
    std::string mMd5Hash;

    friend class Bfsar;
};