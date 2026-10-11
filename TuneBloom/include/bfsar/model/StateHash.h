#pragma once

#include <bfsar/model/Item.h>

class StateHash
{
public:
    void mixBytes(const void* data, size_t size)
    {
        const u8* bytes = static_cast<const u8*>(data);

        for (size_t i = 0; i < size; i++)
        {
            mValue ^= bytes[i];
            mValue *= 0x100000001B3ULL;
        }
    }

    template <typename T>
    void mixValue(const T& value)
    {
        mixBytes(&value, sizeof(value));
    }

    void mixText(const sead::SafeString& text)
    {
        mixBytes(text.cstr(), text.calcLength() + 1);
    }

    void mixItem(const Item* item)
    {
        mixValue(item ? item->getUid() : static_cast<u64>(0));
    }

    void mixRef(const ItemReference& reference)
    {
        mixItem(reference.getItem());
    }

    void mixName(const Item& item)
    {
        mixValue(item.isEnableName());
        mixText(item.getName());
    }

    u64 value() const
    {
        return mValue;
    }

private:
    u64 mValue = 0xCBF29CE484222325ULL;
};
