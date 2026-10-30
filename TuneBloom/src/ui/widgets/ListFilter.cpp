#include <ui/widgets/ListFilter.h>

#include <ui/widgets/ContextMenu.h>
#include <ui/file/DocumentState.h>
#include <ui/items/ItemList.h>
#include <messages/Messages.h>
#include <ui/app/Shortcuts.h>
#include <ui/settings/UiColors.h>
#include <ui/app/Workspace.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_custom.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <cstring>
#include <string>

static constexpr s32 cListPane   = 0;
static constexpr s32 cDetailPane = 1;

static DocumentState::TabFilter& GetTabFilter(s32 pane)
{
    return ActiveDocumentState().filters[static_cast<size_t>(GetUITab())][pane];
}

static DocumentState::TabFilter& GetCurrentTabFilter()
{
    return GetTabFilter(cListPane);
}

static DocumentState::TabFilter& GetCurrentDetailTabFilter()
{
    return GetTabFilter(cDetailPane);
}

static void CloseFilterPane(s32 pane)
{
    GetTabFilter(pane).filter.clear();
}

void CloseFilter()
{
    CloseFilterPane(cListPane);
}

static bool sTextFilterEnabled = true;

void SetItemTextFilterEnabled(bool enabled)
{
    sTextFilterEnabled = enabled;
}

static bool HasIdNameFormat(const Item* item)
{
    switch (item->getItemType())
    {
        case Item::ItemType::BankFileInstrument:
        case Item::ItemType::GroupItemInfo:
        case Item::ItemType::StreamTrack:
            return false;

        default:
            return true;
    }
}

static bool QueryCanMatchId(const char* query)
{
    return std::strpbrk(query, "0123456789[]") != nullptr;
}

bool ItemMatchesQuery(const Item* item, const char* query, bool caseSensitive)
{
    if (HasIdNameFormat(item) && !QueryCanMatchId(query))
        return ImGui::TextMatchesQuery(item->getNameOrNull().cstr(), query, caseSensitive);

    return ImGui::TextMatchesQuery(item->getFormattedName().cstr(), query, caseSensitive);
}

static bool ItemMatchesFilterPane(const Item* item, s32 pane)
{
    DocumentState::TabFilter& state = GetTabFilter(pane);

    if (state.filter.isEmpty() || !sTextFilterEnabled)
        return true;

    return ItemMatchesQuery(item, state.filter.cstr(), state.caseSensitive);
}

bool ItemMatchesFilter(const Item* item)
{
    return ItemMatchesFilterPane(item, cListPane);
}

bool ItemMatchesDetailFilter(const Item* item)
{
    return ItemMatchesFilterPane(item, cDetailPane);
}

ItemFilterCallback GetItemFilterCallback()
{
    return GetCurrentTabFilter().filter.isEmpty() ? nullptr : &ItemMatchesFilter;
}

ItemFilterCallback GetDetailFilterCallback()
{
    return GetCurrentDetailTabFilter().filter.isEmpty() ? nullptr : &ItemMatchesDetailFilter;
}

static bool DrawSearchToggle(const char* label, const char* tooltip, bool* value, f32 size)
{
    bool pressed = false;

    ImGui::SameLine(0.0f, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(*value ? ImGuiCol_Text : ImGuiCol_TextDisabled));
    ImGui::PushStyleColor(ImGuiCol_Button, *value ? ImGui::GetColorU32(ImGuiCol_ButtonActive) : uicolors::cTransparentU32);

    if (ImGui::Button(label, ImVec2(size, size)))
    {
        *value = !*value;
        pressed = true;
    }

    ImGui::PopStyleColor(2);
    ImGui::SetItemTooltip("%s", tooltip);

    return pressed;
}

static constexpr f32 cScopeChipPadEm      = 0.4f;
static constexpr f32 cScopeChipGapEm      = 0.3f;
static constexpr f32 cScopeChipInsetEm    = 0.2f;
static constexpr f32 cScopeChipFieldShare = 0.6f;

static constexpr const char* cTextCut = "...";

static f32 CalcScopeChipWidth(const char* scope, f32 fieldWidth)
{
    const f32 em = ImGui::GetFontSize();
    const f32 pad = ImFloor(em * cScopeChipPadEm);
    const f32 gap = ImFloor(em * cScopeChipGapEm);
    const f32 want = pad + ImGui::CalcTextSize(scope).x + gap + ImGui::CalcTextSize(ICON_LC_X).x + pad;

    return ImFloor(std::min(want, fieldWidth * cScopeChipFieldShare));
}

static void DrawMiddleCutText(ImDrawList* drawList, const ImVec2& pos, f32 width, ImU32 color, const char* text)
{
    const char* end = text + strlen(text);

    if (ImGui::CalcTextSize(text, end).x <= width)
    {
        drawList->AddText(pos, color, text, end);
        return;
    }

    const f32 cutWidth = ImGui::CalcTextSize(cTextCut).x;
    const char* headEnd = text;
    const char* tailStart = end;

    for (bool isHead = true; headEnd < tailStart; isHead = !isHead)
    {
        const char* nextHead = headEnd;
        const char* nextTail = tailStart;

        if (isHead)
        {
            unsigned int codepoint = 0;
            nextHead += ImTextCharFromUtf8(&codepoint, headEnd, tailStart);
        }
        else
        {
            do
                nextTail--;
            while (nextTail > headEnd && (static_cast<u8>(*nextTail) & 0xC0) == 0x80);
        }

        if (ImGui::CalcTextSize(text, nextHead).x + cutWidth + ImGui::CalcTextSize(nextTail, end).x > width)
            break;

        headEnd = nextHead;
        tailStart = nextTail;
    }

    const f32 headWidth = ImGui::CalcTextSize(text, headEnd).x;

    drawList->AddText(pos, color, text, headEnd);
    drawList->AddText(ImVec2(pos.x + headWidth, pos.y), color, cTextCut);
    drawList->AddText(ImVec2(pos.x + headWidth + cutWidth, pos.y), color, tailStart, end);
}

static void DrawSearchScopeChip(const ImVec2& pos, f32 width, f32 frameHeight, const SearchFieldExtra& extra)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 em = ImGui::GetFontSize();
    const f32 pad = ImFloor(em * cScopeChipPadEm);
    const f32 gap = ImFloor(em * cScopeChipGapEm);
    const f32 inset = std::max(ImFloor(em * cScopeChipInsetEm), 2.0f);
    const f32 textY = pos.y + style.FramePadding.y;
    const ImVec2 chipMin(pos.x, pos.y + inset);
    const ImVec2 chipMax(pos.x + width, pos.y + frameHeight - inset);
    const f32 closeWidth = ImGui::CalcTextSize(ICON_LC_X).x;
    const f32 closeLeft = chipMax.x - pad - closeWidth;
    const f32 nameLeft = chipMin.x + pad;
    const f32 nameRight = std::max(closeLeft - gap, nameLeft);
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    ImGui::AddRectFilledRaised(drawList, chipMin, chipMax, ImGui::GetColorU32(ImGuiCol_Button), style.FrameRounding);

    ImGui::SetCursorScreenPos(chipMin);
    ImGui::InvisibleButton("##Scope", ImVec2(closeLeft - gap * 0.5f - chipMin.x, chipMax.y - chipMin.y));

    if (extra.scopeHovered)
        *extra.scopeHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip);

    ImGui::SetCursorScreenPos(ImVec2(closeLeft - gap * 0.5f, chipMin.y));

    if (ImGui::InvisibleButton("##ScopeClear", ImVec2(chipMax.x - closeLeft + gap * 0.5f, chipMax.y - chipMin.y)) && extra.scopeCleared)
        *extra.scopeCleared = true;

    if (extra.scopeClearTooltip)
        ImGui::SetItemTooltip("%s", extra.scopeClearTooltip);

    const ImU32 closeColor = ImGui::GetHoverColorU32(ImGui::GetItemID(), ImGui::IsItemHovered(), ImGui::GetColorU32(ImGuiCol_TextDisabled), ImGui::GetColorU32(ImGuiCol_Text));

    drawList->AddText(ImVec2(closeLeft, textY), closeColor, ICON_LC_X);
    DrawMiddleCutText(drawList, ImVec2(nameLeft, textY), nameRight - nameLeft, ImGui::GetColorU32(ImGuiCol_Text), extra.scope);
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
    const bool hasScope = extra && extra->scope;
    const f32 chipWidth = hasScope ? CalcScopeChipWidth(extra->scope, width - iconWidth - buttonsWidth - (hasText ? 0.0f : height)) : 0.0f;

    ImGui::PushID(id);

    const ImGuiID inputId = ImGui::GetID("##Input");

    ImGui::RenderFrame(pos, ImVec2(pos.x + width, pos.y + height), ImGui::GetColorU32(ImGuiCol_FrameBg), true, style.FrameRounding);
    ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x + style.FramePadding.x + 1.0f, pos.y + style.FramePadding.y), ImGui::GetColorU32(ImGuiCol_TextDisabled), icon);

    if (hasScope)
        DrawSearchScopeChip(ImVec2(pos.x + iconWidth, pos.y), chipWidth, height, *extra);

    if (setFocus || sFocusId == inputId)
    {
        ImGui::SetKeyboardFocusHere();
        sFocusId = 0;
    }

    ImGui::PushStyleColor(ImGuiCol_FrameBg, uicolors::cTransparentU32);
    ImGui::PushStyleColor(ImGuiCol_Button, uicolors::cTransparentU32);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

    ImGui::SetCursorScreenPos(ImVec2(pos.x + iconWidth + chipWidth, pos.y));
    ImGui::SetNextItemWidth(width - iconWidth - chipWidth - buttonsWidth);

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

    if (hasScope && extra->scopeCleared && !hasText && ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Backspace, false))
        *extra->scopeCleared = true;

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

static void DrawTabFilterBarPane(s32 pane, const SearchFieldExtra* extra = nullptr)
{
    DocumentState::TabFilter& tabFilter = GetTabFilter(pane);

    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    const bool setFocus = focused && shortcuts::Pressed(shortcuts::Action::FocusFilter);

    if (focused && !ImGui::GetIO().WantTextInput && shortcuts::Pressed(shortcuts::Action::CloseFilter))
        CloseFilterPane(pane);

    const f32 padding = BeginListRow();

    ListSearchField(pane == cListPane ? "##Search" : "##SearchDetail", tabFilter.filter.getBuffer(), tabFilter.filter.getBufferSize(), ImGui::GetContentRegionAvail().x - padding, &tabFilter.caseSensitive, setFocus, extra);
}

void DrawTabFilterBar(const SearchFieldExtra* extra)
{
    DrawTabFilterBarPane(cListPane, extra);
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
    DrawTabFilterBarPane(cDetailPane);
}
