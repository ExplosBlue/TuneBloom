#define IMGUI_DEFINE_MATH_OPERATORS
#include <ui/widgets/ListFilter.h>

#include <ui/widgets/ContextMenu.h>
#include <ui/items/ItemList.h>
#include <ui/Messages.h>
#include <ui/app/Shortcuts.h>
#include <ui/app/Workspace.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_custom.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <string>

struct TabFilterState
{
    sead::FixedSafeString<256> filter;
    bool caseSensitive = false;
};

static TabFilterState sTabFilterState[(size_t)UIType::Max + 1][2];

static TabFilterState &GetTabFilter(int pane)
{
    return sTabFilterState[(size_t)sSelectedUIType][pane];
}

static TabFilterState &GetCurrentTabFilter()
{
    return GetTabFilter(0);
}

static TabFilterState &GetCurrentDetailTabFilter()
{
    return GetTabFilter(1);
}

static void CloseFilterPane_(int pane)
{
    TabFilterState &state = GetTabFilter(pane);
    state.filter.clear();
}

void CloseFilter()
{
    CloseFilterPane_(0);
}

static bool sTextFilterEnabled = true;

void SetItemTextFilterEnabled(bool enabled)
{
    sTextFilterEnabled = enabled;
}

static bool ItemMatchesFilterPane_(const Item *item, int pane)
{
    TabFilterState &state = GetTabFilter(pane);

    if (state.filter.isEmpty() || !sTextFilterEnabled)
    {
        return true;
    }

    return ImGui::TextMatchesQuery(item->getFormattedName().cstr(), state.filter.cstr(), state.caseSensitive);
}

bool ItemMatchesFilter(const Item* item)
{
    return ItemMatchesFilterPane_(item, 0);
}

bool ItemMatchesDetailFilter(const Item* item)
{
    return ItemMatchesFilterPane_(item, 1);
}

ItemFilterCallback GetItemFilterCallback()
{
    TabFilterState& state = GetCurrentTabFilter();
    return !state.filter.isEmpty() ? &ItemMatchesFilter : nullptr;
}

ItemFilterCallback GetDetailFilterCallback()
{
    TabFilterState& state = GetCurrentDetailTabFilter();
    return !state.filter.isEmpty() ? &ItemMatchesDetailFilter : nullptr;
}

static bool DrawSearchToggle(const char* label, const char* tooltip, bool* value, f32 size)
{
    bool pressed = false;

    ImGui::SameLine(0.0f, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(*value ? ImGuiCol_Text : ImGuiCol_TextDisabled));
    ImGui::PushStyleColor(ImGuiCol_Button, *value ? ImGui::GetColorU32(ImGuiCol_ButtonActive) : IM_COL32(0, 0, 0, 0));

    if (ImGui::Button(label, ImVec2(size, size)))
    {
        *value = !*value;
        pressed = true;
    }

    ImGui::PopStyleColor(2);
    ImGui::SetItemTooltip("%s", tooltip);

    return pressed;
}

bool SearchField(const char* id, char* buffer, size_t bufferSize, f32 width, bool* caseSensitive, bool setFocus, const SearchFieldExtra* extra)
{
    static ImGuiID sFocusId = 0;

    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 height = ImGui::GetFrameHeight();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool hasText = buffer[0] != '\0';
    bool changed = false;

    if (width <= 0.0f)
        width = ImGui::GetContentRegionAvail().x;

    const char* icon = extra && extra->icon ? extra->icon : ICON_LC_SEARCH;
    const char* hint = extra && extra->hint ? extra->hint : messages::item::cSearchHint;
    bool* wholeWord = extra ? extra->wholeWord : nullptr;
    bool* enterPressed = extra ? extra->enterPressed : nullptr;

    const f32 iconWidth = ImGui::CalcTextSize(icon).x + style.FramePadding.x + ImFloor(style.ItemInnerSpacing.x * 0.5f);
    bool* menuPressed = extra ? extra->menuPressed : nullptr;

    const f32 buttonsWidth = height * ((hasText ? 1.0f : 0.0f) + (caseSensitive ? 1.0f : 0.0f) + (wholeWord ? 1.0f : 0.0f) + (menuPressed ? 1.0f : 0.0f));

    ImGui::PushID(id);

    const ImGuiID inputId = ImGui::GetID("##Input");

    ImGui::RenderFrame(pos, ImVec2(pos.x + width, pos.y + height), ImGui::GetColorU32(ImGuiCol_FrameBg), true, style.FrameRounding);
    ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x + style.FramePadding.x + 1.0f, pos.y + style.FramePadding.y), ImGui::GetColorU32(ImGuiCol_TextDisabled), icon);

    if (setFocus || sFocusId == inputId)
    {
        ImGui::SetKeyboardFocusHere();
        sFocusId = 0;
    }

    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

    ImGui::SetCursorScreenPos(ImVec2(pos.x + iconWidth, pos.y));
    ImGui::SetNextItemWidth(width - iconWidth - buttonsWidth);

    if (enterPressed)
    {
        static bool sCursorToEnd = false;

        const auto cursorToEnd = [](ImGuiInputTextCallbackData* data) -> int
        {
            bool* pending = static_cast<bool*>(data->UserData);

            if (*pending)
                data->CursorPos = data->SelectionStart = data->SelectionEnd = data->BufTextLen;

            *pending = false;
            return 0;
        };

        const std::string oldText = buffer;
        const ImGuiInputTextFlags flags = ImGuiInputTextFlags_EscapeClearsAll | ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackAlways;

        if (ImGui::InputTextWithHint("##Input", hint, buffer, bufferSize, flags, cursorToEnd, &sCursorToEnd))
        {
            *enterPressed = true;
            sFocusId = inputId;
            sCursorToEnd = true;
        }

        changed = oldText != buffer;
    }
    else if (ImGui::InputTextWithHint("##Input", hint, buffer, bufferSize, ImGuiInputTextFlags_EscapeClearsAll))
    {
        changed = true;
    }

    if (hasText)
    {
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

        if (ImGui::Button(ICON_LC_X "##Clear", ImVec2(height, height)))
        {
            buffer[0] = '\0';
            sFocusId = inputId;
            changed = true;
        }

        ImGui::PopStyleColor();
        ImGui::SetItemTooltip("%s", messages::keys::cClearSearch);
    }

    if (caseSensitive && DrawSearchToggle(ICON_LC_CASE_SENSITIVE "##Case", messages::item::cCaseSensitive, caseSensitive, height))
        changed = true;

    if (wholeWord && DrawSearchToggle(ICON_LC_WHOLE_WORD "##Word", messages::item::cWholeWord, wholeWord, height))
        changed = true;

    if (menuPressed)
    {
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(extra->menuActive ? ImGuiCol_CheckMark : ImGuiCol_TextDisabled));

        if (ImGui::Button(ICON_LC_ELLIPSIS "##Menu", ImVec2(height, height)))
            *menuPressed = true;

        ImGui::PopStyleColor();
        ImGui::SetItemTooltip("%s", messages::item::cListMenu);
    }

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    ImGui::PopID();

    return changed;
}

constexpr f32 cListSearchHeightEm = 1.5f;
constexpr f32 cListSearchGapEm    = 0.4f;

bool ListSearchField(const char* id, char* buffer, size_t bufferSize, f32 width, bool* caseSensitive, bool setFocus, const SearchFieldExtra* extra)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 em = ImGui::GetFontSize();
    const f32 padY = std::max(style.FramePadding.y, ImFloor((em * cListSearchHeightEm - em) * 0.5f));
    const f32 gap = std::max(style.ItemSpacing.y, ImFloor(em * cListSearchGapEm));

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, padY));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, gap));

    const bool changed = SearchField(id, buffer, bufferSize, width, caseSensitive, setFocus, extra);

    ImGui::PopStyleVar(2);

    return changed;
}

static bool IsListHostWindow()
{
    const ImGuiWindow* window = ImGui::GetCurrentWindow();

    return window == ImGui::FindWindowByName("###InfoWindow") || window == ImGui::FindWindowByName("###SubInfoWindow");
}

f32 BeginListRow()
{
    static ImGuiID sFilledWindow = 0;
    static s32 sFilledFrame = -1;

    if (!IsListHostWindow())
        return 0.0f;

    const ImGuiStyle& style = ImGui::GetStyle();
    ImGuiWindow* window = ImGui::GetCurrentWindow();

    if (sFilledWindow != window->ID || sFilledFrame != ImGui::GetFrameCount())
    {
        sFilledWindow = window->ID;
        sFilledFrame = ImGui::GetFrameCount();

        window->DrawList->AddRectFilled(window->InnerRect.Min, window->InnerRect.Max, ImGui::GetColorU32(ImGuiCol_ChildBg));
    }

    if (ImGui::GetCursorPosY() == ImGui::GetCursorStartPos().y)
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + style.WindowPadding.y);

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + style.WindowPadding.x);

    return style.WindowPadding.x;
}

static void DrawTabFilterBarPane_(int pane, const SearchFieldExtra* extra = nullptr)
{
    TabFilterState& tabFilter = GetTabFilter(pane);

    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    const bool setFocus = focused && shortcuts::Pressed(shortcuts::Action::FocusFilter);

    if (focused && !ImGui::GetIO().WantTextInput && shortcuts::Pressed(shortcuts::Action::CloseFilter))
        CloseFilterPane_(pane);

    const f32 padding = BeginListRow();

    ListSearchField(pane == 0 ? "##Search" : "##SearchDetail", tabFilter.filter.getBuffer(), tabFilter.filter.getBufferSize(), ImGui::GetContentRegionAvail().x - padding, &tabFilter.caseSensitive, setFocus, extra);
}

void DrawTabFilterBar(const SearchFieldExtra* extra)
{
    DrawTabFilterBarPane_(0, extra);
}

bool BeginListOptions(bool active)
{
    bool menuPressed = false;

    SearchFieldExtra extra;
    extra.menuPressed = &menuPressed;
    extra.menuActive = active;

    DrawTabFilterBar(&extra);

    if (menuPressed)
        ImGui::OpenPopup("##ListMenu");

    return BeginContextMenu("##ListMenu");
}

void DrawDetailTabFilterBar()
{
    DrawTabFilterBarPane_(1);
}
