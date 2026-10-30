#pragma once

#include <container/seadRingBuffer.h>
#include <heap/seadDisposer.h>
#include <prim/seadSafeString.h>

#include <map>
#include <string>
#include <vector>

#include <bfsar/model/Item.h>
#include <messages/MessageType.h>

class MessageLog
{
    SEAD_SINGLETON_DISPOSER(MessageLog);

public:
    enum class ErrorContext
    {
        Opening,
        Saving
    };

    enum class Severity
    {
        Error,
        Warning
    };

    struct ItemMessage
    {
        std::string text;
        std::string detail;
        Severity severity = Severity::Error;
    };

    struct ItemOrder
    {
        bool operator()(const Item* a, const Item* b) const
        {
            return a->getIdWithTypeAll() < b->getIdWithTypeAll();
        }
    };

    using ItemMessageMap = std::map<Item*, std::vector<ItemMessage>, ItemOrder>;

    struct ItemErrors
    {
        ErrorContext context = ErrorContext::Opening;
        ItemMessageMap items;
    };

    struct PopupInfo
    {
    private:
        struct Text : public sead::FixedSafeString<1024>
        {
            Text()
            {
            }

            Text(const char* str)
                : sead::FixedSafeString<1024>(str)
            {
            }

            Text(const sead::SafeString& str)
                : sead::FixedSafeString<1024>(str)
            {
            }
        };

    public:
        Text text;
        Item* item = nullptr;
        Item* super = nullptr;
    };

private:
    MessageLog();

public:
    void addPopup(const PopupInfo& info);
    bool hasPopup() const;
    const PopupInfo& frontPopup() const;
    void popPopup();

    void forgetItem(const Item* item);
    void forgetAllItems();
    void closeFile();

    void moveProcessedErrors(ItemErrors* target);
    void discardProcessedErrors();

    void setCorruptInfo(const sead::SafeString& info)
    {
        mCorruptInfo = info;
    }

    const sead::SafeString& getCorruptInfo() const
    {
        return mCorruptInfo;
    }

    void setCurrentProcessItem(Item* item)
    {
        mCurrentProcessItem = item;
    }

    void setErrorContext(ErrorContext context)
    {
        mErrorContext = context;
    }

    Item* getCurrentProcessItem_()
    {
        return mCurrentProcessItem;
    }

    void pushCurrentItemError(const sead::SafeString& error, const char* detail = nullptr);
    void pushCurrentItemWarning(const sead::SafeString& warning, const char* detail = nullptr);

    void pushCurrentItemError(const messages::Message& message)
    {
        pushCurrentItemError(message.text, message.detail);
    }

    void pushCurrentItemWarning(const messages::Message& message)
    {
        pushCurrentItemWarning(message.text, message.detail);
    }

private:
    void pushCurrentItemMessage(const sead::SafeString& text, const char* detail, Severity severity);

    sead::FixedRingBuffer<PopupInfo, 10> mPopups;
    sead::FixedSafeString<1024> mCorruptInfo;
    Item* mCurrentProcessItem;
    ErrorContext mErrorContext;
    ItemMessageMap mProcessedErrors;
};
