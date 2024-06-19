#pragma once

#include <bfsar/Item.h>

class Group : public Item
{
public:
    class ItemInfo : public Item
    {
    public:
        ItemInfo(Group* owner)
            : Item()
            , mItemRefType(ItemType::Invalid)
            , mItemRef(owner)
            , mLoadFlag(0xFFFFFFFF)
        {
            mItemType = ItemType::GroupItemInfo;
        }

        sead::FixedSafeString<256> getFormattedName() const override
        {
            sead::FixedSafeString<256> name;
            if (mItemRef.isAttached())
            {
                name = mItemRef.getItem()->getFormattedName();
            }
            else
            {
                name = "(null)";
            }

            return name;
        }

        ItemType getItemRefType() const
        {
            return mItemRefType;
        }

        const ItemReference& getItemRef() const
        {
            return mItemRef;
        }

        ItemReference& getItemRef()
        {
            return mItemRef;
        }

    private:
        ItemType mItemRefType;
        ItemReference mItemRef;
        u32 mLoadFlag;

        friend class Bfsar;
    };

    enum class OutputType
    {
        Embed,
        Link,
        // External
    };

public:
    Group()
        : Item()
        , mOutputType(OutputType::Embed)
        , mItemInfoList()
    {
        mItemType = ItemType::Group;
    }

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

private:
    OutputType mOutputType;
    ItemInfo::List mItemInfoList;

    friend class Bfsar;
};
