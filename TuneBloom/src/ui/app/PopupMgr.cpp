#include <ui/app/PopupMgr.h>

#include <ui/settings/ConfigFile.h>
#include <ui/widgets/Dialogs.h>
#include <ui/file/File.h>
#include <ui/file/FileWindow.h>
#include <ui/file/GroupFileLocate.h>
#include <ui/Messages.h>
#include <core/TestHooks.h>
#include <ui/items/Selection.h>
#include <ui/app/Shortcuts.h>
#include <ui/sound/SoundViews.h>
#include <ui/settings/Theme.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

SEAD_SINGLETON_DISPOSER_IMPL(PopupMgr);

static const ImVec4 cErrorColor(1.0f, 0.35f, 0.35f, 1.0f);
static constexpr ImVec4 cWarningColor = cWarningTextColor;

static constexpr f32 cWidthEm          = 44.0f;
static constexpr f32 cHeightEm         = 26.0f;
static constexpr f32 cMinWidthEm       = 40.0f;
static constexpr f32 cMinHeightEm      = 14.0f;
static constexpr f32 cPanePadEm        = 0.6f;
static constexpr f32 cPaneGapEm        = 0.8f;
static constexpr f32 cHeadingGapEm     = 0.5f;
static constexpr f32 cRowGapEm         = 0.5f;
static constexpr f32 cMessageGapEm     = 0.4f;
static constexpr f32 cFooterGapEm      = 0.6f;
static constexpr f32 cFooterGroupGapEm = 1.5f;

static constexpr const char* cIgnoredSection     = "ignoredWarnings";
static constexpr const char* cIgnoredArchiveKey  = "archive";
static constexpr const char* cIgnoredWarningsKey = "warnings";

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

    width += ImGui::GetFontSize() * cPanePadEm * 2.0f + style.ScrollbarSize;

    const f32 maxWidth = ImGui::GetContentRegionAvail().x * 0.45f;

    return width > maxWidth ? maxWidth : width;
}

static void DrawSeverityCount(const char* icon, size_t count, const ImVec4& color)
{
    if (count == 0)
        return;

    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(sead::FormatFixedSafeString<64>(messages::popup::cSeverityCountFormat, icon, count, count == 1 ? messages::common::cItem : messages::common::cItems).cstr());
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
            ImGui::Dummy(ImVec2(0.0f, ImFloor(ImGui::GetFontSize() * cMessageGapEm)));
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

static bool HasWarning(const std::vector<PopupMgr::ItemMessage>& messages)
{
    for (const PopupMgr::ItemMessage& message : messages)
    {
        if (message.severity == PopupMgr::Severity::Warning)
            return true;
    }

    return false;
}

static std::string GetArchiveKey()
{
    std::string path = sBfsar.getFilePath().cstr();

#if defined(SEAD_PLATFORM_WINDOWS)
    std::replace(path.begin(), path.end(), '/', '\\');
#endif

    return path;
}

static std::string GetWarningKey(const Item* item, const PopupMgr::ItemMessage& message)
{
    std::string key = std::to_string(static_cast<s32>(item->getItemType()));
    key += '\t';
    key += item->getFormattedName().cstr();
    key += '\t';
    key += message.text;

    for (char& c : key)
    {
        if (c == '\n' || c == '\r')
            c = ' ';
    }

    return key;
}

PopupMgr::PopupMgr()
    : mPopups()
    , mPopupOpen(false)
    , mCorruptInfo()
    , mCurrentProcessItem(nullptr)
    , mErrorContext(ErrorContext::Opening)
    , mProcessedErrors()
    , mIgnoredWarnings()
{
    loadIgnoredWarnings();
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

static constexpr const char* cMessagePopupId = "###PopupMgr";
static constexpr const char* cErrorsPopupId  = "###PopupMgrErrors";

static bool IsResyncEnabled()
{
    return !testhooks::IsEnabled() || testhooks::IsBackupOn();
}

void PopupMgr::update()
{
    if (mPopupOpen && IsResyncEnabled() && !ImGui::IsPopupOpen(cMessagePopupId) && !ImGui::IsPopupOpen(cErrorsPopupId))
        mPopupOpen = false;

    updateErrors();

    if (mPopups.size() == 0)
    {
        return;
    }

    const PopupInfo& info = mPopups.front();

    if (!mPopupOpen)
    {
        ImGui::OpenPopup(cMessagePopupId);
        mPopupOpen = true;
    }

    {
        CenterNextDialog();

        if (BeginDialog(sead::FormatFixedSafeString<32>(ICON_LC_ALERT_TRIANGLE " %s%s", messages::common::cWarning, cMessagePopupId).cstr(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            Item* item = info.item;
            if (item)
            {
                const char* name = info.super ? info.super->getFormattedName().cstr() : item->getFormattedName().cstr();
                ImGui::Text(messages::popup::cItemInvalidFormat, name);
                ImGui::Separator();
            }

            ImGui::Text("%s", info.text.cstr());

            const sead::FormatFixedSafeString<32> goToLabel("%s %s", ICON_LC_EXTERNAL_LINK, messages::common::cGoTo);

            if (item)
                BeginDialogFooter({messages::dialog::cOk, goToLabel.cstr()});
            else
                BeginDialogFooter({messages::dialog::cOk});

            if (DialogButton(messages::dialog::cOk))
            {
                PopupInfo dummy;
                mPopups.popFront(&dummy);

                mPopupOpen = false;

                ImGui::CloseCurrentPopup();
            }

            if (item)
            {
                if (DialogButton(goToLabel.cstr()))
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
                        else if (item->getItemType() == Item::ItemType::StreamTrack)
                        {
                            SelectItem(info.super);
                            HighlightStreamTrack(item);
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
        str = messages::common::cUnknown;
    }

    mProcessedErrors[mCurrentProcessItem].push_back({str, detail ? detail : "", severity});
}

template <typename ErrorMap>
static std::vector<ListEntry> BuildErrorEntries(const ErrorMap& errors, size_t* outErrorCount, bool* outHasWarnings)
{
    std::vector<ListEntry> entries;
    entries.reserve(errors.size());

    for (const auto& entry : errors)
    {
        if (HasError(entry.second))
            entries.push_back({entry.first, &entry.second, true});

        if (HasWarning(entry.second))
            *outHasWarnings = true;
    }

    *outErrorCount = entries.size();

    for (const auto& entry : errors)
    {
        if (!HasError(entry.second))
            entries.push_back({entry.first, &entry.second, false});
    }

    return entries;
}

static void SetNextErrorsWindowSize(f32 fontSize)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImVec2 windowSize(fontSize * cWidthEm, fontSize * cHeightEm);

    const f32 maxWidth = viewport->WorkSize.x * 0.85f;
    const f32 maxHeight = viewport->WorkSize.y * 0.85f;

    if (windowSize.x > maxWidth)
        windowSize.x = maxWidth;

    if (windowSize.y > maxHeight)
        windowSize.y = maxHeight;

    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(sead::Mathf::min(fontSize * cMinWidthEm, maxWidth), sead::Mathf::min(fontSize * cMinHeightEm, maxHeight)), ImVec2(FLT_MAX, FLT_MAX));
}

static bool MoveErrorSelection(size_t* selected, size_t entryNum)
{
    bool moved = false;

    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow))
        return false;

    if (shortcuts::Pressed(shortcuts::Action::SelectPrevious) && *selected != 0)
    {
        (*selected)--;
        moved = true;
    }

    if (shortcuts::Pressed(shortcuts::Action::SelectNext) && *selected + 1 < entryNum)
    {
        (*selected)++;
        moved = true;
    }

    return moved;
}

static void DrawErrorsHeading(bool isSaving, bool hasErrors)
{
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
}

static void DrawErrorEntryList(const std::vector<ListEntry>& entries, size_t* selected, bool scroll, f32 paneHeight)
{
    ImGui::BeginChild("LeftPane", ImVec2(CalcListWidth(entries), paneHeight), ImGuiChildFlags_Border | ImGuiChildFlags_ResizeX);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, ImFloor(ImGui::GetFontSize() * cRowGapEm)));

    for (size_t i = 0; i < entries.size(); i++)
    {
        const ListEntry& entry = entries[i];
        const bool isSelected = *selected == i;

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
            *selected = i;
        }

        if (!isSelected)
        {
            ImGui::PopStyleColor();
        }
    }

    ImGui::PopStyleVar();
    ImGui::EndChild();
}

static void DrawIgnoreWarningsCheck(f32 fontSize, f32 viewLeft, bool* ignore)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 groupGap = ImFloor(fontSize * cFooterGroupGapEm);
    const f32 checkWidth = ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + ImGui::CalcTextSize(messages::popup::cIgnoreWarnings).x;

    ImGui::SameLine(0.0f, groupGap);

    const f32 countsRight = ImGui::GetCursorScreenPos().x;
    const f32 okLeft = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x - CalcDialogFooterWidth({messages::dialog::cOk});
    const f32 checkX = ImMax(countsRight, ImMin(viewLeft, okLeft - groupGap - checkWidth));

    ImGui::SetCursorScreenPos(ImVec2(checkX, ImGui::GetCursorScreenPos().y));
    ImGui::Checkbox(messages::popup::cIgnoreWarnings, ignore);

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("%s", messages::popup::cIgnoreWarningsDetail);
}

void PopupMgr::updateErrors()
{
    if (mProcessedErrors.empty())
    {
        sBfsar.clearGenWaveArchiveList();
        return;
    }

    if (!mPopupOpen)
    {
        if (IsGroupFileLocatePending())
            return;

        if (mErrorContext == ErrorContext::Opening)
            removeIgnoredWarnings();

        if (mProcessedErrors.empty())
        {
            sBfsar.clearGenWaveArchiveList();
            return;
        }

        ImGui::OpenPopup(cErrorsPopupId);
        mPopupOpen = true;
    }

    bool hasWarnings = false;
    size_t errorCount = 0;

    const std::vector<ListEntry> entries = BuildErrorEntries(mProcessedErrors, &errorCount, &hasWarnings);

    const size_t warningCount = entries.size() - errorCount;

    const bool isSaving = mErrorContext == ErrorContext::Saving;
    const bool hasErrors = errorCount != 0;
    const bool canIgnore = !isSaving && hasWarnings && !sBfsar.getFilePath().isEmpty();

    const f32 fontSize = ImGui::GetFontSize();

    SetNextErrorsWindowSize(fontSize);

    const char* title = messages::popup::cTitleWarningsOpening;

    if (isSaving)
        title = messages::popup::cTitleSkippedSaving;
    else if (hasErrors)
        title = messages::popup::cTitleErrorsOpening;

    if (!BeginDialog(sead::FormatFixedSafeString<96>(ICON_LC_ALERT_TRIANGLE " %s%s", title, cErrorsPopupId).cstr(), nullptr, ImGuiWindowFlags_NoScrollbar))
        return;

    static size_t sSelected = 0;
    static bool sIgnore = false;

    if (ImGui::IsWindowAppearing() || sSelected >= entries.size())
    {
        sSelected = 0;
    }

    if (ImGui::IsWindowAppearing())
        sIgnore = false;

    const bool scroll = MoveErrorSelection(&sSelected, entries.size());

    DrawErrorsHeading(isSaving, hasErrors);

    const f32 footerGap = ImFloor(fontSize * cFooterGapEm);
    const f32 paneHeight = -(CalcDialogFooterHeight() + footerGap);
    const f32 panePad = ImFloor(fontSize * cPanePadEm);

    ImGui::Dummy(ImVec2(0.0f, ImFloor(fontSize * cHeadingGapEm) - ImGui::GetStyle().ItemSpacing.y));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(panePad, panePad));

    const f32 listLeft = ImGui::GetCursorScreenPos().x;

    DrawErrorEntryList(entries, &sSelected, scroll, paneHeight);

    ImGui::SameLine(0.0f, ImFloor(fontSize * cPaneGapEm));

    const f32 viewLeft = ImGui::GetCursorScreenPos().x;

    {
        ImGui::BeginChild("ItemView", ImVec2(0.0f, paneHeight), ImGuiChildFlags_Border);

        const std::vector<ItemMessage>& itemMessages = *entries[sSelected].messages;

        DrawMessageGroup(itemMessages, Severity::Error);
        DrawMessageGroup(itemMessages, Severity::Warning);

        ImGui::EndChild();
    }

    ImGui::PopStyleVar();

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + footerGap - ImGui::GetStyle().ItemSpacing.y);
    ImGui::SetCursorScreenPos(ImVec2(listLeft, ImGui::GetCursorScreenPos().y));
    ImGui::AlignTextToFramePadding();
    DrawSeverityCount(ICON_LC_ALERT_OCTAGON, errorCount, cErrorColor);

    if (errorCount != 0 && warningCount != 0)
    {
        ImGui::SameLine();
        ImGui::TextDisabled(messages::popup::cCountSeparator);
        ImGui::SameLine();
    }

    DrawSeverityCount(ICON_LC_ALERT_TRIANGLE, warningCount, cWarningColor);

    if (canIgnore)
        DrawIgnoreWarningsCheck(fontSize, viewLeft, &sIgnore);

    ImGui::SameLine();
    BeginDialogFooter({messages::dialog::cOk}, false);

    if (DialogButton(messages::dialog::cOk))
    {
        if (canIgnore && sIgnore)
            ignoreProcessedWarnings();

        mCurrentProcessItem = nullptr;
        mErrorContext = ErrorContext::Opening;
        mProcessedErrors.clear();
        mPopupOpen = false;
        sSelected = 0;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void PopupMgr::closeFile()
{
    mCurrentProcessItem = nullptr;
    mErrorContext = ErrorContext::Opening;
    mProcessedErrors.clear();
}

void PopupMgr::loadIgnoredWarnings()
{
    mIgnoredWarnings.clear();

    json::Value archives;
    LoadConfigSection(cStateFile, cIgnoredSection, &archives);

    for (const json::Value& archive : archives.items)
    {
        const std::string* path = archive.findString(cIgnoredArchiveKey);
        const json::Value* warnings = archive.findArray(cIgnoredWarningsKey);

        if (!path || !warnings)
            continue;

        std::set<std::string>& ignored = mIgnoredWarnings[*path];

        for (const json::Value& warning : warnings->items)
        {
            if (warning.isString())
                ignored.insert(warning.string);
        }
    }
}

void PopupMgr::saveIgnoredWarnings() const
{
    json::Value archives;
    archives.type = json::Type::Array;

    for (const auto& archive : mIgnoredWarnings)
    {
        json::Value& entry = archives.pushObject();
        entry.setString(cIgnoredArchiveKey, archive.first.c_str());

        json::Value& warnings = entry.setArray(cIgnoredWarningsKey);

        for (const std::string& warning : archive.second)
            warnings.pushString(warning.c_str());
    }

    SaveConfigSection(cStateFile, cIgnoredSection, archives);
}

void PopupMgr::clearIgnoredWarnings()
{
    mIgnoredWarnings.clear();
    saveIgnoredWarnings();
}

void PopupMgr::removeIgnoredWarnings()
{
    const auto ignored = mIgnoredWarnings.find(GetArchiveKey());
    if (ignored == mIgnoredWarnings.end())
        return;

    for (auto it = mProcessedErrors.begin(); it != mProcessedErrors.end();)
    {
        const Item* item = it->first;
        std::vector<ItemMessage>& itemMessages = it->second;

        itemMessages.erase(std::remove_if(itemMessages.begin(), itemMessages.end(), [&](const ItemMessage& message)
        {
            return message.severity == Severity::Warning && ignored->second.count(GetWarningKey(item, message)) != 0;
        }), itemMessages.end());

        if (itemMessages.empty())
            it = mProcessedErrors.erase(it);
        else
            ++it;
    }
}

void PopupMgr::ignoreProcessedWarnings()
{
    const std::string archive = GetArchiveKey();
    if (archive.empty())
        return;

    std::set<std::string>& ignored = mIgnoredWarnings[archive];

    for (const auto& entry : mProcessedErrors)
    {
        for (const ItemMessage& message : entry.second)
        {
            if (message.severity == Severity::Warning)
                ignored.insert(GetWarningKey(entry.first, message));
        }
    }

    saveIgnoredWarnings();
}
