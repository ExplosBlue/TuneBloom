#include <ui/app/Notifications.h>

#include <ui/widgets/ContextMenu.h>
#include <ui/items/ItemColumns.h>
#include <ui/widgets/Knob.h>
#include <ui/Messages.h>
#include <ui/settings/Theme.h>
#include <ui/widgets/TimeUtil.h>
#include <ui/widgets/WindowAnimation.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_custom.h>
#include <imgui/imgui_internal.h>

#include <prim/seadSafeString.h>

#include <cmath>
#include <ctime>
#include <mutex>

namespace notify
{

static constexpr s32 cHistoryMax     = 50;
static constexpr s32 cPendingMax     = 8;
static constexpr s32 cToastMax       = 3;
static constexpr u32 cToastWindowNum = 16;

static constexpr f32 cToastSeconds        = 5.0f;
static constexpr f32 cToastWarningSeconds = 8.0f;
static constexpr f32 cFadeInSeconds       = 0.12f;
static constexpr f32 cFadeOutSeconds      = 0.10f;
static constexpr f32 cMoveHalfLife        = 0.04f;

static constexpr f32 cToastWidthEm     = 24.0f;
static constexpr f32 cToastMarginEm    = 0.8f;
static constexpr f32 cToastGapEm       = 0.4f;
static constexpr f32 cToastPadXEm      = 0.7f;
static constexpr f32 cToastPadYEm      = 0.6f;
static constexpr f32 cToastSlideEm     = 0.4f;
static constexpr f32 cIconGapEm        = 0.5f;
static constexpr f32 cListWidthEm      = 24.0f;
static constexpr f32 cListMaxHeightEm  = 20.0f;
static constexpr f32 cListGapEm        = 0.3f;
static constexpr f32 cListRowGapEm     = 0.6f;
static constexpr f32 cBellDotRadiusEm  = 0.15f;

static constexpr const char* cListId = "###Notifications";

struct Entry
{
    Kind kind = Kind::Info;
    sead::FixedSafeString<192> title;
    sead::FixedSafeString<512> detail;
    std::time_t time = 0;
    u32 serial = 0;

    bool isUnread = false;
    bool isToast = false;
    bool isClosing = false;
    bool hasOffset = false;
    f32 age = 0.0f;
    f32 alpha = 0.0f;
    f32 offset = 0.0f;
};

struct Pending
{
    Kind kind = Kind::Info;
    sead::FixedSafeString<192> title;
    sead::FixedSafeString<512> detail;
};

// Oldest first
static Entry sEntries[cHistoryMax];
static s32 sEntryNum = 0;
static u32 sNextSerial = 1;

static std::mutex sPendingMutex;
static Pending sPending[cPendingMax];
static s32 sPendingNum = 0;

static bool sWantsList = false;
static bool sListOpen = false;
static ImVec2 sListAnchor;

static const char* GetKindIcon(Kind kind)
{
    switch (kind)
    {
        case Kind::Success: return ICON_LC_CIRCLE_CHECK;
        case Kind::Warning: return ICON_LC_TRIANGLE_ALERT;
        case Kind::Error:   return ICON_LC_OCTAGON_ALERT;
        default:            return ICON_LC_INFO;
    }
}

static ImVec4 GetKindColor(Kind kind)
{
    switch (kind)
    {
        case Kind::Success: return gThemeIsDark ? ImVec4(0.45f, 0.82f, 0.52f, 1.0f) : ImVec4(0.13f, 0.55f, 0.25f, 1.0f);
        case Kind::Warning: return gThemeIsDark ? ImVec4(1.0f, 0.78f, 0.35f, 1.0f) : ImVec4(0.75f, 0.50f, 0.05f, 1.0f);
        case Kind::Error:   return gThemeIsDark ? ImVec4(1.0f, 0.35f, 0.35f, 1.0f) : ImVec4(0.80f, 0.20f, 0.20f, 1.0f);
        default:            return ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
    }
}

static f32 GetToastSeconds(Kind kind)
{
    return kind == Kind::Warning ? cToastWarningSeconds : cToastSeconds;
}

void Add(Kind kind, const char* title, const char* text)
{
    if (!title || title[0] == '\0')
        return;

    std::lock_guard<std::mutex> lock(sPendingMutex);

    if (sPendingNum == cPendingMax)
        return;

    Pending& pending = sPending[sPendingNum++];
    pending.kind = kind;
    pending.title.cutOffCopy(title);
    pending.detail.cutOffCopy(text ? text : "");
}

static void AddEntry(const Pending& pending)
{
    if (sEntryNum != 0)
    {
        Entry& last = sEntries[sEntryNum - 1];

        if (last.isToast && !last.isClosing && last.kind == pending.kind && last.title == pending.title && last.detail == pending.detail)
        {
            last.age = 0.0f;
            last.time = std::time(nullptr);
            return;
        }
    }

    if (sEntryNum == cHistoryMax)
    {
        for (s32 i = 1; i < cHistoryMax; i++)
            sEntries[i - 1] = sEntries[i];

        sEntryNum--;
    }

    Entry& entry = sEntries[sEntryNum++];
    entry = Entry();
    entry.kind = pending.kind;
    entry.title.copy(pending.title);
    entry.detail.copy(pending.detail);
    entry.time = std::time(nullptr);
    entry.serial = sNextSerial++;
    entry.isUnread = !sListOpen;
    entry.isToast = !sListOpen;
}

static void TakePending()
{
    {
        std::lock_guard<std::mutex> lock(sPendingMutex);

        for (s32 i = 0; i < sPendingNum; i++)
            AddEntry(sPending[i]);

        sPendingNum = 0;
    }

    s32 toastNum = 0;

    for (s32 i = sEntryNum - 1; i >= 0; i--)
    {
        Entry& entry = sEntries[i];

        if (!entry.isToast || entry.isClosing)
            continue;

        if (++toastNum <= cToastMax)
            continue;

        if (entry.alpha <= 0.0f)
            entry.isToast = false;
        else
            entry.isClosing = true;
    }
}

static void DrawEntryText(const Entry& entry, f32 reserveWidth)
{
    const f32 em = ImGui::GetFontSize();
    const ImVec2 start = ImGui::GetCursorPos();
    const f32 textX = start.x + ImGui::CalcTextSize(ICON_LC_INFO).x + ImFloor(em * cIconGapEm);

    ImGui::PushStyleColor(ImGuiCol_Text, GetKindColor(entry.kind));
    ImGui::TextUnformatted(GetKindIcon(entry.kind));
    ImGui::PopStyleColor();

    ImGui::PushTextWrapPos(ImGui::GetWindowContentRegionMax().x - reserveWidth);

    ImGui::SetCursorPos(ImVec2(textX, start.y));
    ImGui::TextUnformatted(entry.title.cstr());

    ImGui::PopTextWrapPos();
    ImGui::PushTextWrapPos(ImGui::GetWindowContentRegionMax().x);

    if (!entry.detail.isEmpty())
    {
        ImGui::SetCursorPosX(textX);
        ImGui::PushStyleColor(ImGuiCol_Text, GetItemValueColor());
        ImGui::TextUnformatted(entry.detail.cstr());
        ImGui::PopStyleColor();
    }

    ImGui::PopTextWrapPos();
}

static f32 CalcToastBottom(const ImGuiViewport* viewport)
{
    const f32 bottom = viewport->WorkPos.y + viewport->WorkSize.y;
    const ImGuiWindow* player = ImGui::FindWindowByName("###PlayerWindow");

    if (!player || !player->WasActive || !player->DockIsActive || !player->DockNode || player->Viewport != viewport)
        return bottom;

    const f32 top = player->DockNode->Pos.y;

    if (top < viewport->GetCenter().y)
        return bottom;

    return top;
}

static f32 DrawToast(Entry& entry, const ImGuiViewport* viewport, f32 right, f32 bottom, bool animated)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImGuiIO& io = ImGui::GetIO();
    const f32 em = ImGui::GetFontSize();

    const f32 fade = 1.0f - (1.0f - entry.alpha) * (1.0f - entry.alpha) * (1.0f - entry.alpha);
    const f32 slide = entry.isClosing ? 0.0f : ImFloor(em * cToastSlideEm * (1.0f - fade));

    ImVec4 background = style.Colors[ImGuiCol_PopupBg];
    background.w = 1.0f;

    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowPos(ImVec2(right, ImFloor(bottom - entry.offset + slide)), ImGuiCond_Always, ImVec2(1.0f, 1.0f));
    ImGui::SetNextWindowSize(ImVec2(ImMin(ImFloor(em * cToastWidthEm), viewport->WorkSize.x - ImFloor(em * cToastMarginEm) * 2.0f), 0.0f));

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, style.Alpha * ImMax(fade, 0.001f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ImFloor(em * cMenuRoundingEm));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ImFloor(em * cToastPadXEm), ImFloor(em * cToastPadYEm)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, background);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking;

    f32 height = 0.0f;
    bool hovered = false;
    bool close = false;

    if (ImGui::Begin(sead::FormatFixedSafeString<32>("##Toast%u", entry.serial % cToastWindowNum).cstr(), nullptr, flags))
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();

        if (GImGui->OpenPopupStack.Size == 0)
            ImGui::BringWindowToDisplayFront(window);

        if (!window->Hidden)
            height = window->Size.y;

        const f32 closeSize = em;
        const ImVec2 start = ImGui::GetCursorPos();

        DrawEntryText(entry, closeSize + style.ItemSpacing.x);

        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowContentRegionMax().x - closeSize, start.y));

        if (ImGui::InvisibleButton("##Close", ImVec2(closeSize, closeSize)))
            close = true;

        const ImU32 closeColor = ImGui::GetHoverColorU32(ImGui::GetItemID(), ImGui::IsItemHovered(), ImGui::GetColorU32(ImGuiCol_TextDisabled), ImGui::GetColorU32(ImGuiCol_Text));
        const ImVec2 closeMin = ImGui::GetItemRectMin();
        const ImVec2 iconSize = ImGui::CalcTextSize(ICON_LC_X);

        window->DrawList->AddText(ImVec2(closeMin.x + ImFloor((closeSize - iconSize.x) * 0.5f), closeMin.y + ImFloor((closeSize - iconSize.y) * 0.5f)), closeColor, ICON_LC_X);

        hovered = ImGui::IsWindowHovered();

        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            close = true;
    }

    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(5);

    if (close)
        entry.isClosing = true;

    if (!hovered && animated && entry.kind != Kind::Error && !entry.isClosing)
    {
        entry.age += io.DeltaTime;

        if (entry.age >= GetToastSeconds(entry.kind))
            entry.isClosing = true;
    }

    if (!animated)
        entry.alpha = entry.isClosing ? 0.0f : 1.0f;
    else if (entry.isClosing)
        entry.alpha = ImMax(0.0f, entry.alpha - io.DeltaTime / cFadeOutSeconds);
    else if (height > 0.0f)
        entry.alpha = ImMin(1.0f, entry.alpha + io.DeltaTime / cFadeInSeconds);

    if (entry.isClosing && entry.alpha <= 0.0f)
        entry.isToast = false;

    return height;
}

static void DrawToasts()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const f32 em = ImGui::GetFontSize();
    const f32 margin = ImFloor(em * cToastMarginEm);
    const f32 gap = ImFloor(em * cToastGapEm);
    const f32 right = viewport->WorkPos.x + viewport->WorkSize.x - margin;
    const f32 bottom = CalcToastBottom(viewport) - margin;

    const bool animated = IsAnimationEnabled();
    const f32 moveRatio = animated ? 1.0f - std::exp2(-ImGui::GetIO().DeltaTime / cMoveHalfLife) : 1.0f;

    f32 offset = 0.0f;

    for (s32 i = sEntryNum - 1; i >= 0; i--)
    {
        Entry& entry = sEntries[i];

        if (!entry.isToast)
            continue;

        if (!entry.hasOffset)
        {
            entry.offset = offset;
            entry.hasOffset = true;
        }
        else
        {
            entry.offset += (offset - entry.offset) * moveRatio;
        }

        const f32 height = DrawToast(entry, viewport, right, bottom, animated);

        if (height > 0.0f)
            offset += height + gap;
    }
}

static void DrawList()
{
    if (sWantsList)
    {
        ImGui::OpenPopup(cListId);
        sWantsList = false;

        for (s32 i = 0; i < sEntryNum; i++)
            sEntries[i].isClosing = true;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 em = ImGui::GetFontSize();

    ImVec4 background = style.Colors[ImGuiCol_PopupBg];
    background.w = 1.0f;

    ImGui::SetNextWindowPos(ImVec2(sListAnchor.x, sListAnchor.y + ImFloor(em * cListGapEm)), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(ImFloor(em * cListWidthEm), 0.0f));

    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, ImFloor(em * cMenuRoundingEm));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ImFloor(em * cToastPadXEm), ImFloor(em * cToastPadYEm)));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, background);

    sListOpen = ImGui::BeginPopup(cListId, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

    if (sListOpen)
    {
        AnimateWindow(WindowAnimation::Menu);

        const f32 clearWidth = ImGui::CalcTextSize(messages::notify::cClear).x + style.FramePadding.x * 2.0f;

        RegionHeader(messages::notify::cTitle, clearWidth + style.ItemSpacing.x);

        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - clearWidth);

        ImGui::BeginDisabled(sEntryNum == 0);

        if (ImGui::Button(messages::notify::cClear))
            sEntryNum = 0;

        ImGui::EndDisabled();

        ImGui::Dummy(ImVec2(0.0f, ImFloor(em * cListGapEm) - style.ItemSpacing.y));

        if (sEntryNum == 0)
        {
            ImGui::TextDisabled("%s", messages::notify::cEmpty);
        }
        else
        {
            ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(FLT_MAX, ImFloor(em * cListMaxHeightEm)));

            if (ImGui::BeginChild("##List", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AutoResizeY))
            {
                for (s32 i = sEntryNum - 1; i >= 0; i--)
                {
                    Entry& entry = sEntries[i];
                    entry.isUnread = false;

                    if (i != sEntryNum - 1)
                        ImGui::Dummy(ImVec2(0.0f, ImFloor(em * cListRowGapEm) - style.ItemSpacing.y));

                    const ImVec2 start = ImGui::GetCursorPos();

                    char text[16];
                    timeutil::FormatTimeOfDay(text, sizeof(text), static_cast<s64>(entry.time));

                    const f32 timeWidth = ImGui::CalcTextSize(text).x;

                    DrawEntryText(entry, timeWidth + style.ItemSpacing.x);

                    const ImVec2 end = ImGui::GetCursorPos();

                    ImGui::SetCursorPos(ImVec2(ImGui::GetWindowContentRegionMax().x - timeWidth, start.y));
                    ImGui::TextDisabled("%s", text);

                    ImGui::SetCursorPos(end);
                }
            }

            ImGui::EndChild();
        }

        ImGui::EndPopup();
    }

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

f32 DrawBell(f32 right, f32 top, f32 bottom)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 em = ImGui::GetFontSize();
    const ImVec2 iconSize = ImGui::CalcTextSize(ICON_LC_BELL);
    const f32 width = iconSize.x + style.ItemSpacing.x * 2.0f;
    const ImVec2 min(right - width, top);
    const ImVec2 max(right, bottom);

    ImGui::SetCursorScreenPos(min);

    if (ImGui::InvisibleButton("##Notifications", ImVec2(width, bottom - top)))
        sWantsList = !sListOpen;

    const ImGuiID id = ImGui::GetItemID();
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !sListOpen)
        ImGui::SetTooltip("%s", messages::notify::cTitle);

    sListAnchor = ImVec2(right, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y);

    bool hasEntry = sEntryNum != 0;
    bool hasUnread = false;
    Kind unreadKind = Kind::Info;

    for (s32 i = 0; i < sEntryNum; i++)
    {
        if (!sEntries[i].isUnread)
            continue;

        if (!hasUnread || sEntries[i].kind > unreadKind)
            unreadKind = sEntries[i].kind;

        hasUnread = true;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    ImVec4 fill = style.Colors[held ? ImGuiCol_HeaderActive : ImGuiCol_HeaderHovered];
    ImVec4 idle = fill;
    idle.w = 0.0f;

    if (sListOpen)
        drawList->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_Header), style.FrameRounding);
    else
        drawList->AddRectFilled(min, max, ImGui::GetHoverColorU32(id, hovered, ImGui::GetColorU32(idle), ImGui::GetColorU32(fill)), style.FrameRounding);

    const ImVec2 iconPos(min.x + style.ItemSpacing.x, ImFloor((top + bottom - iconSize.y) * 0.5f));

    drawList->AddText(iconPos, ImGui::GetColorU32(hasEntry ? ImGuiCol_Text : ImGuiCol_TextDisabled), ICON_LC_BELL);

    if (hasUnread)
    {
        const f32 radius = em * cBellDotRadiusEm;
        const ImVec2 center(iconPos.x + iconSize.x - radius, iconPos.y + radius * 2.0f);

        drawList->AddCircleFilled(center, radius + 1.5f, ImGui::GetColorU32(ImGuiCol_MenuBarBg));
        drawList->AddCircleFilled(center, radius, ImGui::GetColorU32(GetKindColor(unreadKind)));
    }

    return width;
}

void Draw()
{
    TakePending();
    DrawList();
    DrawToasts();
}

}
