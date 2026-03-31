#pragma once

#include <bfsar/Item.h>

class SoundSet : public Item
{
public:
    enum class SoundSetType
    {
        Wave,
        Seq
    };

public:
    SoundSet()
        : Item()
        , mSoundSetType(SoundSetType::Wave)
        , mIsEmpty(false)
        , mStartId(0)
        , mEndId(0)
        , mWaveArchiveType(WaveArchiveType::AutomaticShared)
        , mWaveArchiveRef(this)
    {
        mItemType = ItemType::SoundSet;

        mWaveArchiveRef.setOnDetachCallback<SoundSet>(&SoundSet::onDetachWaveArchive_); // Temp ? Idk a good solution yet
    }

    const Item* validate(sead::BufferedSafeString& error) const override;

    SoundSetType getSoundSetType() const
    {
        return mSoundSetType;
    }

    void setSoundSetType(SoundSetType type)
    {
        mSoundSetType = type;
    }

    bool getIsEmpty() const
    {
        return mIsEmpty;
    }

    void setIsEmpty(bool isEmpty)
    {
        mIsEmpty = isEmpty;
    }

    u32 getStartId() const
    {
        if (mIsEmpty)
        {
            return cInvalidId;
        }

        return mStartId;
    }

    void setStartId(u32 startId)
    {
        mStartId = startId;
    }

    u32 getEndId() const
    {
        if (mIsEmpty)
        {
            return cInvalidId;
        }

        return mEndId;
    }

    void setEndId(u32 endId)
    {
        mEndId = endId;
    }

    WaveArchiveType getWaveArchiveType() const
    {
        return mWaveArchiveType;
    }

    void setWaveArchiveType(WaveArchiveType warcType)
    {
        mWaveArchiveType = warcType;
    }

    const ItemReference& getWaveArchiveRef() const
    {
        return mWaveArchiveRef;
    }

    ItemReference& getWaveArchiveRef()
    {
        return mWaveArchiveRef;
    }

private:
    void onDetachWaveArchive_(Item* item)
    {
        mWaveArchiveType = WaveArchiveType::AutomaticShared;
    }

private:
    SoundSetType mSoundSetType;
    bool mIsEmpty;
    u32 mStartId;
    u32 mEndId;
    WaveArchiveType mWaveArchiveType;
    ItemReference mWaveArchiveRef;

    friend class Bfsar;
};
