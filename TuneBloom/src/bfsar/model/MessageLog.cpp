#include <bfsar/model/MessageLog.h>

#include <messages/Messages.h>

SEAD_SINGLETON_DISPOSER_IMPL(MessageLog);

MessageLog::MessageLog()
    : mPopups()
    , mCorruptInfo()
    , mCurrentProcessItem(nullptr)
    , mErrorContext(ErrorContext::Opening)
    , mProcessedErrors()
{
}

void MessageLog::addPopup(const PopupInfo& info)
{
    mPopups.pushBack(info);
}

bool MessageLog::hasPopup() const
{
    return mPopups.size() != 0;
}

const MessageLog::PopupInfo& MessageLog::frontPopup() const
{
    return mPopups.front();
}

void MessageLog::popPopup()
{
    PopupInfo popped;
    mPopups.popFront(&popped);
}

void MessageLog::forgetAllItems()
{
    for (s32 i = 0; i < mPopups.size(); i++)
    {
        mPopups(i).item = nullptr;
        mPopups(i).super = nullptr;
    }
}

void MessageLog::forgetItem(const Item* item)
{
    for (s32 i = 0; i < mPopups.size(); i++)
    {
        PopupInfo& info = mPopups(i);

        if (info.item == item)
            info.item = nullptr;

        if (info.super == item)
            info.super = nullptr;
    }
}

void MessageLog::closeFile()
{
    mCurrentProcessItem = nullptr;
    discardProcessedErrors();
}

void MessageLog::pushCurrentItemError(const sead::SafeString& error, const char* detail)
{
    pushCurrentItemMessage(error, detail, Severity::Error);
}

void MessageLog::pushCurrentItemWarning(const sead::SafeString& warning, const char* detail)
{
    pushCurrentItemMessage(warning, detail, Severity::Warning);
}

void MessageLog::pushCurrentItemMessage(const sead::SafeString& text, const char* detail, Severity severity)
{
    if (!mCurrentProcessItem)
        return;

    const char* str = text.isEmpty() ? messages::common::cUnknown : text.cstr();

    mProcessedErrors[mCurrentProcessItem].push_back({str, detail ? detail : "", severity});
}

void MessageLog::moveProcessedErrors(ItemErrors* target)
{
    if (mProcessedErrors.empty())
        return;

    target->context = mErrorContext;

    for (auto& entry : mProcessedErrors)
    {
        std::vector<ItemMessage>& itemMessages = target->items[entry.first];
        itemMessages.insert(itemMessages.end(), entry.second.begin(), entry.second.end());
    }

    discardProcessedErrors();
}

void MessageLog::discardProcessedErrors()
{
    mProcessedErrors.clear();
    mErrorContext = ErrorContext::Opening;
}
