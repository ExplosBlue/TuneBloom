#pragma once

#include <bfsar/Item.h>

class WaveArchive : public Item
{
public:
    WaveArchive()
        : Item()
        , mIsLoadIndividual(false)
    {
        mItemType = ItemType::WaveArchive;
    }

    bool validate(sead::BufferedSafeString& error) const override;

    bool getIsLoadIndividual() const
    {
        return mIsLoadIndividual;
    }

    void setIsLoadIndividual(bool isLoadIndividual)
    {
        mIsLoadIndividual = isLoadIndividual;
    }

private:
    bool mIsLoadIndividual;

    friend class Bfsar;
};
