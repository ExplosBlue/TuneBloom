#include <ui/Messages.h>
#include <ui/PopupMgr.h>

#include <ui/Shortcuts.h>
#include <ui/UI.h>

SEAD_SINGLETON_DISPOSER_IMPL(PopupMgr);

static const ImVec4 cErrorColor(1.0f, 0.35f, 0.35f, 1.0f);
static const ImVec4 cWarningColor(1.0f, 0.78f, 0.35f, 1.0f);

struct ListEntry
{
    Item* item;
    const std::vector<PopupMgr::ItemMessage>* messages;
    bool hasError;
};

static f32 CalcListWidth(const std::vector<ListEntry>& entries)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 iconWidth = ImGui::CalcTextSize(ICON_LC_ALERT_OCTAGON " ").x;

    f32 width = ImGui::GetFontSize() * 6.0f;

    for (const ListEntry& entry : entries)
    {
        const f32 nameWidth = iconWidth + ImGui::CalcTextSize(entry.item->getFormattedName().cstr()).x;

        if (nameWidth > width)
            width = nameWidth;
    }

    width += style.FramePadding.x * 2.0f + style.ScrollbarSize + style.WindowPadding.x;

    const f32 maxWidth = ImGui::GetContentRegionAvail().x * 0.45f;

    return width > maxWidth ? maxWidth : width;
}

static void DrawSeverityCount(const char* icon, size_t count, const ImVec4& color)
{
    if (count == 0)
        return;

    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(sead::FormatFixedSafeString<64>(messages::popup::cSeverityCountFormat, icon, count, count == 1 ? messages::popup::cItemSingular : messages::popup::cItemPlural).cstr());
    ImGui::PopStyleColor();
}

static void DrawMessage(const PopupMgr::ItemMessage& message)
{
    if (!message.detail.empty())
    {
        ImGui::TextDisabled(messages::popup::cDetailMarker);

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone))
        {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22.0f);
            ImGui::TextUnformatted(message.detail.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }
    else
    {
        ImGui::Dummy(ImVec2(ImGui::CalcTextSize(messages::popup::cDetailMarker).x, 0.0f));
    }

    ImGui::SameLine();

    ImGui::TextWrapped("%s", message.text.c_str());
}

static void DrawMessageGroup(const std::vector<PopupMgr::ItemMessage>& messages, PopupMgr::Severity severity)
{
    bool drewHeader = false;

    for (const PopupMgr::ItemMessage& message : messages)
    {
        if (message.severity != severity)
            continue;

        if (!drewHeader)
        {
            const bool isError = severity == PopupMgr::Severity::Error;

            ImGui::PushStyleColor(ImGuiCol_Text, isError ? cErrorColor : cWarningColor);
            ImGui::SeparatorText(sead::FormatFixedSafeString<32>("%s %s", isError ? ICON_LC_ALERT_OCTAGON : ICON_LC_ALERT_TRIANGLE, isError ? messages::popup::cGroupErrors : messages::popup::cGroupWarnings).cstr());
            ImGui::PopStyleColor();

            drewHeader = true;
        }
        else
        {
            ImGui::Spacing();
        }

        DrawMessage(message);
    }

    if (drewHeader)
    {
        ImGui::Spacing();
    }
}

static bool HasError(const std::vector<PopupMgr::ItemMessage>& messages)
{
    for (const PopupMgr::ItemMessage& message : messages)
    {
        if (message.severity == PopupMgr::Severity::Error)
            return true;
    }

    return false;
}

PopupMgr::PopupMgr()
    : mPopups()
    , mPopupOpen(false)
    , mCorruptInfo()
    , mCurrentProcessItem(nullptr)
    , mErrorContext(ErrorContext::Opening)
    , mProcessedErrors()
{
}

void PopupMgr::addPopup(const PopupInfo& info)
{
    mPopups.pushBack(info);
}

void PopupMgr::forgetItem(const Item* item)
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

void PopupMgr::update()
{
    updateErrors();

    if (mPopups.size() == 0)
    {
        return;
    }

    const PopupInfo& info = mPopups.front();

    static const char* sPopupName = "###PopupMgr";

    if (!mPopupOpen)
    {
        ImGui::OpenPopup(sPopupName);
        mPopupOpen = true;
    }

    {
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal(sead::FormatFixedSafeString<32>(ICON_LC_ALERT_TRIANGLE " Warning%s", sPopupName).cstr(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImVec2 buttonSize((ImGui::GetWindowContentRegionMax().x - ImGui::GetStyle().WindowPadding.x * 2.0f) / 2.0f, 0.0f);

            Item* item = info.item;
            if (item)
            {
                const char* name = info.super ? info.super->getFormattedName().cstr() : item->getFormattedName().cstr();
                ImGui::Text("Item '%s' is invalid:", name);
                ImGui::Separator();
            }
            else
            {
                buttonSize = ImVec2(ImGui::GetWindowContentRegionMax().x - ImGui::GetStyle().WindowPadding.x, 0);
            }

            ImGui::Text("%s", info.text.cstr());
            ImGui::Separator();

            if (ImGui::Button("OK", buttonSize))
            {
                PopupInfo dummy;
                mPopups.popFront(&dummy);

                mPopupOpen = false;

                ImGui::CloseCurrentPopup();
            }

            if (item)
            {
                ImGui::SameLine();

                if (ImGui::Button(ICON_LC_EXTERNAL_LINK " Go To", buttonSize))
                {
                    PopupInfo dummy;
                    mPopups.popFront(&dummy);

                    mPopupOpen = false;

                    if (info.super)
                    {
                        if (item->getItemType() == Item::ItemType::BankFileInstrument)
                        {
                            SelectItem(item);
                            OpenFileWindow(info.super);
                        }
                        else
                        {
                            SelectItem(info.super);
                            sSubSelectedItem = item;
                            sSelectedItemIsSubWindow = true;
                        }
                    }
                    else
                    {
                        SelectItem(item);
                    }

                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::EndPopup();
        }
    }
}

void PopupMgr::pushCurrentItemError(const sead::SafeString& error, const char* detail)
{
    pushCurrentItemMessage(error, detail, Severity::Error);
}

void PopupMgr::pushCurrentItemWarning(const sead::SafeString& warning, const char* detail)
{
    pushCurrentItemMessage(warning, detail, Severity::Warning);
}

void PopupMgr::pushCurrentItemMessage(const sead::SafeString& text, const char* detail, Severity severity)
{
    if (!mCurrentProcessItem)
    {
        return;
    }

    const char* str = text.cstr();
    if (text.isEmpty())
    {
        str = "Unknown";
    }

    mProcessedErrors[mCurrentProcessItem].push_back({str, detail ? detail : "", severity});
}

void PopupMgr::updateErrors()
{
    if (mProcessedErrors.empty())
    {
        sBfsar.clearGenWaveArchiveList();
        return;
    }

    static const char* sPopupName = "###PopupMgrErrors";

    if (!mPopupOpen)
    {
        ImGui::OpenPopup(sPopupName);
        mPopupOpen = true;
    }

    std::vector<ListEntry> entries;
    entries.reserve(mProcessedErrors.size());

    for (const auto& entry : mProcessedErrors)
    {
        if (HasError(entry.second))
            entries.push_back({entry.first, &entry.second, true});
    }

    const size_t errorCount = entries.size();

    for (const auto& entry : mProcessedErrors)
    {
        if (!HasError(entry.second))
            entries.push_back({entry.first, &entry.second, false});
    }

    const size_t warningCount = entries.size() - errorCount;

    const bool isSaving = mErrorContext == ErrorContext::Saving;
    const bool hasErrors = errorCount != 0;

    const f32 fontSize = ImGui::GetFontSize();

    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImVec2 windowSize(fontSize * 38.0f, fontSize * 24.0f);

    const f32 maxWidth = viewport->WorkSize.x * 0.85f;
    const f32 maxHeight = viewport->WorkSize.y * 0.85f;

    if (windowSize.x > maxWidth)
        windowSize.x = maxWidth;

    if (windowSize.y > maxHeight)
        windowSize.y = maxHeight;

    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Appearing);

    const char* title = messages::popup::cTitleWarningsOpening;

    if (isSaving)
        title = messages::popup::cTitleSkippedSaving;
    else if (hasErrors)
        title = messages::popup::cTitleErrorsOpening;

    if (ImGui::BeginPopupModal(sead::FormatFixedSafeString<96>(ICON_LC_ALERT_TRIANGLE " %s%s", title, sPopupName).cstr()))
    {
        static size_t sSelected = 0;

        if (ImGui::IsWindowAppearing() || sSelected >= entries.size())
        {
            sSelected = 0;
        }

        bool scroll = false;

        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow))
        {
            if (shortcuts::Pressed(shortcuts::Action::SelectPrevious) && sSelected != 0)
            {
                sSelected--;
                scroll = true;
            }

            if (shortcuts::Pressed(shortcuts::Action::SelectNext) && sSelected + 1 < entries.size())
            {
                sSelected++;
                scroll = true;
            }
        }

        if (isSaving)
        {
            ImGui::TextWrapped("%s", messages::popup::cHeadingSkipped);
        }
        else if (hasErrors)
        {
            ImGui::TextWrapped("%s", messages::popup::cHeadingErrors);

            ImGui::PushStyleColor(ImGuiCol_Text, cErrorColor);
            ImGui::TextUnformatted(messages::popup::cBackupReminder);
            ImGui::PopStyleColor();
        }
        else
        {
            ImGui::TextWrapped("%s", messages::popup::cHeadingWarnings);
        }

        ImGui::Spacing();

        DrawSeverityCount(ICON_LC_ALERT_OCTAGON, errorCount, cErrorColor);

        if (errorCount != 0 && warningCount != 0)
        {
            ImGui::SameLine();
            ImGui::TextDisabled(messages::popup::cCountSeparator);
            ImGui::SameLine();
        }

        DrawSeverityCount(ICON_LC_ALERT_TRIANGLE, warningCount, cWarningColor);

        ImGui::Spacing();

        const ImGuiStyle& style = ImGui::GetStyle();
        const f32 footerHeight = ImGui::GetFrameHeight() + style.ItemSpacing.y * 2.0f + style.WindowPadding.y;
        const f32 paneHeight = -footerHeight;

        {
            ImGui::BeginChild("LeftPane", ImVec2(CalcListWidth(entries), paneHeight), ImGuiChildFlags_Border | ImGuiChildFlags_ResizeX);

            for (size_t i = 0; i < entries.size(); i++)
            {
                const ListEntry& entry = entries[i];
                const bool isSelected = sSelected == i;

                if (scroll && isSelected)
                {
                    ImGui::SetScrollHereY();
                }

                if (!isSelected)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, entry.hasError ? cErrorColor : cWarningColor);
                }

                sead::FormatFixedSafeString<384> label("%s %s##%u",
                    entry.hasError ? ICON_LC_ALERT_OCTAGON : ICON_LC_ALERT_TRIANGLE,
                    entry.item->getFormattedName().cstr(),
                    (u32)i);

                if (ImGui::Selectable(label.cstr(), isSelected))
                {
                    sSelected = i;
                }

                if (!isSelected)
                {
                    ImGui::PopStyleColor();
                }
            }

            ImGui::EndChild();
        }

        ImGui::SameLine();

        {
            ImGui::BeginChild("ItemView", ImVec2(0.0f, paneHeight), ImGuiChildFlags_Border);

            const std::vector<ItemMessage>& itemMessages = *entries[sSelected].messages;

            DrawMessageGroup(itemMessages, Severity::Error);
            DrawMessageGroup(itemMessages, Severity::Warning);

            ImGui::EndChild();
        }

        ImGui::Separator();

        if (ImGui::Button(messages::popup::cConfirm, ImVec2(-FLT_MIN, 0.0f)))
        {
            mCurrentProcessItem = nullptr;
            mErrorContext = ErrorContext::Opening;
            mProcessedErrors.clear();
            mPopupOpen = false;
            sSelected = 0;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void PopupMgr::closeFile()
{
    mCurrentProcessItem = nullptr;
    mErrorContext = ErrorContext::Opening;
    mProcessedErrors.clear();
}
