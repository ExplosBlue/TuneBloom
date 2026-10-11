#pragma once

#include <bfsar/model/Item.h>

class WaveArchive : public Item
{
public:
    WaveArchive()
        : Item()
        , mIsLoadIndividual(false)
    {
        mItemType = ItemType::WaveArchive;
    }

    const Item* validate(sead::BufferedSafeString& error) const override;

    bool isStateTracked() const override
    {
        return true;
    }

    u64 computeStateSignature() const override;

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
