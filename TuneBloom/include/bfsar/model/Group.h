#pragma once

#include <bfsar/model/Item.h>

#include <string>
#include <vector>

class Sound;
class SoundSet;
class Bank;

class Group : public Item
{
public:
    class ItemInfo : public Item
    {
    public:
        enum LoadFlag
        {
            // Sound
            LoadSeq = 1 << 0,
            LoadWsd = 1 << 1,

            // Bank
            LoadBank = 1 << 2,

            // Waveform Archive
            LoadWarc = 1 << 3,

            LoadAll = 0xFFFFFFFF
        };

        enum class SequenceItems
        {
            All = 0,
            SequenceAndBank,
            SequenceAndWarc,
            BankAndWarc,
            Sequence,
            Bank,
            Warc,

            Num
        };

        enum class WaveSoundSetItems
        {
            All = 0,
            Wsd,
            Warc,

            Num
        };

        enum class BankItems
        {
            All = 0,
            Bank,
            Warc,

            Num
        };

        enum class WaveArchiveItems
        {
            All = 0,

            Num
        };

        static const char* sItemIdTypes[4];

    public:
        ItemInfo(Group* owner)
            : Item()
            , mIsDisabled(false)
            , mItemRefType(ItemType::Invalid)
            , mItemRef(owner)
            //, mLoadFlag(LoadFlag::LoadAll)
            , mLoadItem(0) // All
            , mOriginalItemId(0)
        {
            mItemType = ItemType::GroupItemInfo;
        }

        sead::FixedSafeString<256> getFormattedName() const override;

        void drawUI();

        bool getIsDisabled() const
        {
            return mIsDisabled;
        }

        void setIsDisabled(bool isDisabled)
        {
            mIsDisabled = isDisabled;
        }

        ItemType getItemRefType() const
        {
            return mItemRefType;
        }

        void setItemRefType_(ItemType type)
        {
            mItemRefType = type;
        }

        const ItemReference& getItemRef() const
        {
            return mItemRef;
        }

        ItemReference& getItemRef()
        {
            return mItemRef;
        }

        void setLoadItem_(u32 loadItem)
        {
            mLoadItem = loadItem;
        }

        u32 getLoadItem_() const
        {
            return mLoadItem;
        }

        u32 getLoadFlag() const;

        u32 getOriginalItemId() const { return mOriginalItemId; }
        const char** getLoadItems(u32* outCount) const;

        static const char** GetLoadItems(const Item* item, ItemType itemType, u32* outCount);

        void setLoadItems(u32 loadFlags);

        const Item* validate(sead::BufferedSafeString& error) const override;

    private:
        u32 getLoadFlagForSequence_() const;
        u32 getLoadFlagForWaveSoundSet_() const;
        u32 getLoadFlagForBank_() const;
        u32 getLoadFlagForWaveArchive_() const;

        void setLoadItemsForSequence_(u32 loadFlags);
        void setLoadItemsForWaveSoundSet_(u32 loadFlags);
        void setLoadItemsForBank_(u32 loadFlags);
        void setLoadItemsForWaveArchive_(u32 loadFlags);

        bool validateSequence_(const Sound& sound, sead::BufferedSafeString& error) const;
        bool validateWaveSoundSet_(const SoundSet& soundSet, sead::BufferedSafeString& error, u32 loadFlags) const;
        bool validateBank_(const Bank& bank, sead::BufferedSafeString& error, u32 loadFlags) const;

    private:
        bool mIsDisabled;
        ItemType mItemRefType;
        ItemReference mItemRef;
        u32 mLoadItem;
        u32 mOriginalItemId;

        friend class Bfsar;
    };

    enum class OutputType
    {
        Embed,
        Link,
        External
    };

    enum class ExternalFileState
    {
        None,
        Loaded,
        Missing
    };

public:
    Group()
        : Item()
        , mOutputType(OutputType::Embed)
        , mItemInfoList()
        , mExternalFileState(ExternalFileState::None)
        , mHasFileSlot(false)
        , mGroupInfoFileId(cNoFileId)
    {
        mItemType = ItemType::Group;
    }

    const Item* validate(sead::BufferedSafeString& error) const override;

    bool isStateTracked() const override
    {
        return true;
    }

    u64 computeStateSignature() const override;

    OutputType getOutputType() const
    {
        return mOutputType;
    }

    void setOutputType(OutputType outputType)
    {
        mOutputType = outputType;
    }

    ItemInfo::List& getItemInfoList()
    {
        return mItemInfoList;
    }

    const ItemInfo::List& getItemInfoList() const
    {
        return mItemInfoList;
    }

    const std::string& getExternalFilePath() const
    {
        return mExternalFilePath;
    }

    ExternalFileState getExternalFileState() const
    {
        return mExternalFileState;
    }

    bool hasFileSlot() const
    {
        return mHasFileSlot;
    }

    bool hasGroupInfoFileId() const
    {
        return mGroupInfoFileId != cNoFileId;
    }

    const std::vector<u32>& getOriginalFileOrder() const
    {
        return mOriginalFileOrder;
    }

    const std::vector<u32>& getOriginalExtraFiles() const
    {
        return mOriginalExtraFiles;
    }

private:
    static constexpr u32 cNoFileId = 0xFFFFFFFF;

    OutputType mOutputType;
    ItemInfo::List mItemInfoList;

    // External group file
    std::string mExternalFilePath;
    ExternalFileState mExternalFileState;
    bool mHasFileSlot;
    u32 mGroupInfoFileId;
    std::vector<u32> mOriginalFileOrder;
    std::vector<u32> mOriginalExtraFiles;

    friend class Bfsar;
};
