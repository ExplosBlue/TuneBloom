#include <ui/items/ItemSelector.h>

#include <ui/widgets/ContextMenu.h>
#include <ui/widgets/Dialogs.h>
#include <ui/file/File.h>
#include <ui/items/ItemBrowser.h>
#include <ui/items/ItemColumns.h>
#include <ui/Messages.h>
#include <core/TestHooks.h>
#include <ui/items/Selection.h>
#include <ui/widgets/Widgets.h>
#include <ui/settings/UiColors.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_custom.h>
#include <imgui/imgui_internal.h>

#include <cmath>

static constexpr f32 cSelectorScrollSpeed  = 60.0f;
static constexpr f32 cSelectorScrollPause  = 0.6f;
static constexpr s32 cWaveArchiveAutoCount = 2;

static void DrawGoToContextMenu(Item* target, const char* disabledReason)
{
    if (!BeginItemContextMenu())
        return;

    if (ContextMenuItem(ICON_LC_EXTERNAL_LINK, messages::common::cGoTo, target != nullptr, disabledReason))
        SelectItem(target);

    EndContextMenu();
}

void DrawSelectorName(ImGuiID id, const char* name, const ImVec2& min, const ImVec2& max, bool hovered, bool showEnd)
{
    static ImGuiID sScrollId = 0;
    static f32 sScrollTime = 0.0f;

    const f32 overflow = ImGui::CalcTextSize(name).x - (max.x - min.x);

    if (!hovered || overflow <= 0.0f)
    {
        if (sScrollId == id)
            sScrollId = 0;

        if (showEnd && overflow > 0.0f)
        {
            DrawTextEnd(ImGui::GetWindowDrawList(), min, max.x, name, ImGui::GetColorU32(ImGuiCol_Text), true);
            return;
        }

        ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), min, max, max.x, max.x, name, nullptr, nullptr);
        return;
    }

    if (sScrollId != id)
    {
        sScrollId = id;
        sScrollTime = 0.0f;
    }

    const f32 scrollTime = overflow / cSelectorScrollSpeed;
    const f32 halfTime = scrollTime + cSelectorScrollPause;
    sScrollTime = fmodf(sScrollTime + ImGui::GetIO().DeltaTime, halfTime * 2.0f);

    const f32 phase = sScrollTime < halfTime ? sScrollTime - cSelectorScrollPause : halfTime * 2.0f - sScrollTime;
    const f32 t = sead::Mathf::clamp2(0.0f, phase / scrollTime, 1.0f);
    const f32 ease = t * t * (3.0f - 2.0f * t);
    const f32 offset = overflow * (showEnd ? 1.0f - ease : ease);

    ImGui::PushClipRect(min, max, true);
    ImGui::GetWindowDrawList()->AddText(ImVec2(ImFloor(min.x - offset), min.y), ImGui::GetColorU32(ImGuiCol_Text), name);
    ImGui::PopClipRect();
}

static s32 FindItemIndex(const Item::List& list, const Item* item)
{
    s32 index = 0;

    for (auto it = list.robustBegin(); it != list.robustEnd(); ++it, ++index)
    {
        if ((*it).val() == item)
            return index;
    }

    return -1;
}

static bool FindItemAt(const Item::List& list, s32 index, Item** outItem)
{
    s32 i = 0;

    for (auto it = list.robustBegin(); it != list.robustEnd(); ++it, ++i)
    {
        if (i != index)
            continue;

        *outItem = (*it).val();
        return true;
    }

    return false;
}

static bool StepChoiceIndex(s32* index, s32 delta, s32 total)
{
    const s32 newIndex = sead::MathCalcCommon<s32>::clamp2(0, *index + delta, total - 1);

    if (newIndex == *index)
        return false;

    *index = newIndex;

    return true;
}

static void DrawSelectorLabel(const char* name, bool missing)
{
    if (name[0] == '#' && name[1] == '#')
        return;

    ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);

    if (missing)
    {
        ImGui::PushStyleColor(ImGuiCol_Text,         uicolors::Marker());
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, uicolors::MarkerDim());
    }

    ImGui::Text("%s", name);

    if (missing)
        ImGui::PopStyleColor(2);
}

static bool StepItemSelectorIndex(const Item::List& list, Item** itemPtr, bool allowNone, s32 delta)
{
    const s32 noneCount = allowNone ? 1 : 0;
    const s32 total = static_cast<s32>(list.size()) + noneCount;

    if (delta == 0 || total <= noneCount)
        return false;

    s32 curIdx = allowNone ? 0 : -1;
    const s32 itemIdx = *itemPtr ? FindItemIndex(list, *itemPtr) : -1;

    if (itemIdx >= 0)
        curIdx = itemIdx + noneCount;

    if (!StepChoiceIndex(&curIdx, delta, total))
        return false;

    if (allowNone && curIdx == 0)
        *itemPtr = nullptr;
    else
        FindItemAt(list, curIdx - noneCount, itemPtr);

    return true;
}

static bool DrawItemSelectorTrigger(const char* name, const Item::List& list, Item** itemPtr, bool allowNone, bool compact, f32 triggerWidth)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const bool isWaveList = &list == static_cast<const Item::List*>(&sBfsar.getWaveFileList());

    sead::FixedSafeString<256> itemName;

    if (*itemPtr)
        itemName = (*itemPtr)->getFormattedName();
    else
        itemName.copy(messages::item::cSelectorNone);

    const ImGuiID triggerId = ImGui::GetID(sead::FormatFixedSafeString<64>("##%sCombo", name).cstr());
    const bool popupOpen = IsItemBrowserOpen(name);
    const ImVec2 frameMin = ImGui::GetCursorScreenPos();
    const f32 frameHeight = ImGui::GetFrameHeight();
    const ImVec2 frameSize = ImVec2(triggerWidth, frameHeight);
    const ImVec2 frameMax = ImVec2(frameMin.x + frameSize.x, frameMin.y + frameSize.y);

    ImGui::ItemSize(frameSize);

    const ImRect frameRect = ImRect(frameMin.x, frameMin.y, frameMax.x, frameMax.y);

    if (!ImGui::ItemAdd(frameRect, triggerId))
        return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(frameRect, triggerId, &hovered, &held);

    const f32 arrowSize = compact ? 0.0f : frameHeight;
    const f32 valueX2 = frameMax.x - arrowSize;

    const ImU32 frameCol = ImGui::GetHoverColorU32(triggerId, hovered, false, ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgHovered);
    ImGui::GetWindowDrawList()->AddRectFilled(frameMin, ImVec2(valueX2, frameMax.y), frameCol, style.FrameRounding, compact ? ImDrawFlags_RoundCornersAll : ImDrawFlags_RoundCornersLeft);

    if (!compact)
    {
        const ImU32 arrowBgCol = ImGui::GetHoverColorU32(triggerId + 1, popupOpen || hovered, false, ImGuiCol_Button, ImGuiCol_ButtonHovered, ImGuiCol_ButtonHovered);
        const ImU32 arrowTextCol = ImGui::GetColorU32(ImGuiCol_Text);
        ImGui::AddRectFilledRaised(ImGui::GetWindowDrawList(), ImVec2(valueX2, frameMin.y), frameMax, arrowBgCol, style.FrameRounding, ImDrawFlags_RoundCornersRight);
        const ImVec2 iconSize = ImGui::CalcTextSize(ICON_LC_FOLDER_OPEN);
        ImGui::GetWindowDrawList()->AddText(ImVec2(ImFloor(valueX2 + (arrowSize - iconSize.x) * 0.5f), ImFloor(frameMin.y + (frameHeight - iconSize.y) * 0.5f)), arrowTextCol, ICON_LC_FOLDER_OPEN);
    }

    ImGui::RenderFrameBorder(frameMin, frameMax, style.FrameRounding);

    const f32 nameWidth = valueX2 - frameMin.x - style.FramePadding.x * 2.0f;
    const bool nameCut = compact && ImGui::CalcTextSize(itemName.cstr()).x > nameWidth;

    const bool isEmptySlot = *itemPtr == nullptr && allowNone;

    if (isEmptySlot)
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

    DrawSelectorName(triggerId, itemName.cstr(), ImVec2(frameMin.x + style.FramePadding.x, frameMin.y + style.FramePadding.y), ImVec2(valueX2 - style.FramePadding.x, frameMax.y - style.FramePadding.y), hovered, compact);

    if (isEmptySlot)
        ImGui::PopStyleColor();

    if (testhooks::TakeBrowserRequest(name, isWaveList))
        pressed = true;

    if (pressed)
        OpenItemBrowser(name, list, *itemPtr);

    if (hovered)
        ImGui::SetItemTooltip("%s", nameCut ? itemName.cstr() : messages::browser::cBrowseTip);

    if (popupOpen || !hovered)
        return false;

    ImGui::SetKeyOwner(ImGuiKey_MouseWheelY, triggerId);

    if (ImGui::GetIO().MouseWheel == 0.0f)
        return false;

    return StepItemSelectorIndex(list, itemPtr, allowNone, -static_cast<s32>(ImGui::GetIO().MouseWheel));
}

bool ItemSelector(const char* name, const Item::List& list, Item** itemPtr, bool allowNone, bool compact)
{
    SEAD_ASSERT(name);
    SEAD_ASSERT(itemPtr);

    Item* const oldItem = *itemPtr;

    bool changed = false;

    if (DrawItemSelectorTrigger(name, list, itemPtr, allowNone, compact, ImGui::CalcItemWidth()))
        changed = true;

    if (DrawItemBrowser(name, list, itemPtr, allowNone))
        changed = true;

    DrawGoToContextMenu(*itemPtr, messages::reference::cNothingAttached);

    if (compact)
        return changed && *itemPtr != oldItem;

    DrawSelectorLabel(name, *itemPtr == nullptr && !allowNone);

    return changed && *itemPtr != oldItem;
}

bool ItemSelectorDialogRow(const char* label, const Item::List& list, Item** itemPtr, bool allowNone)
{
    SEAD_ASSERT(itemPtr);

    const bool missing = *itemPtr == nullptr && !allowNone;

    if (missing)
        ImGui::PushStyleColor(ImGuiCol_Text, uicolors::Marker());

    DialogRow(label);

    if (missing)
        ImGui::PopStyleColor();

    return ItemSelector(sead::FormatFixedSafeString<64>("##%s", label).cstr(), list, itemPtr, allowNone);
}

static void SetWaveArchive(WaveArchiveType* warcType, Item** warcPtr, WaveArchiveType type, Item* warc)
{
    *warcType = type;
    *warcPtr = warc;
}

static void FormatWaveArchiveChoice(WaveArchiveType warcType, const Item* warc, sead::BufferedSafeString* out)
{
    if (warc)
        out->copy(warc->getFormattedName());
    else if (warcType == WaveArchiveType::AutomaticShared)
        out->copy(messages::item::cSelectorAutomaticShared);
    else if (warcType == WaveArchiveType::AutomaticIndividual)
        out->copy(messages::item::cSelectorAutomaticIndividual);
    else
        out->copy(messages::item::cSelectorInvalid);
}

static bool DrawWaveArchiveComboEntry(const char* label, WaveArchiveType type, Item* warc, WaveArchiveType* warcType, Item** warcPtr)
{
    const bool selected = warc ? *warcPtr == warc : *warcType == type;
    const bool pressed = ImGui::Selectable(label, selected);

    if (pressed)
        SetWaveArchive(warcType, warcPtr, type, warc);

    if (selected)
        ImGui::SetItemDefaultFocus();

    return pressed;
}

static bool DrawWaveArchiveComboEntries(WaveArchiveType* warcType, Item** warcPtr, const Item::List& warcList)
{
    bool changed = false;

    if (DrawWaveArchiveComboEntry(messages::item::cSelectorAutomaticShared, WaveArchiveType::AutomaticShared, nullptr, warcType, warcPtr))
        changed = true;

    if (DrawWaveArchiveComboEntry(messages::item::cSelectorAutomaticIndividual, WaveArchiveType::AutomaticIndividual, nullptr, warcType, warcPtr))
        changed = true;

    for (auto it = warcList.robustBegin(); it != warcList.robustEnd(); ++it)
    {
        Item* warc = (*it).val();

        if (DrawWaveArchiveComboEntry(warc->getFormattedName().cstr(), WaveArchiveType::Explicit, warc, warcType, warcPtr))
            changed = true;
    }

    return changed;
}

static bool StepWaveArchiveIndex(WaveArchiveType* warcType, Item** warcPtr, const Item::List& warcList, s32 delta)
{
    if (delta == 0)
        return false;

    s32 curIdx = 0;

    if (*warcType == WaveArchiveType::AutomaticIndividual)
    {
        curIdx = 1;
    }
    else if (*warcType == WaveArchiveType::Explicit && *warcPtr)
    {
        const s32 warcIdx = FindItemIndex(warcList, *warcPtr);

        if (warcIdx >= 0)
            curIdx = cWaveArchiveAutoCount + warcIdx;
    }

    if (!StepChoiceIndex(&curIdx, delta, cWaveArchiveAutoCount + static_cast<s32>(warcList.size())))
        return false;

    Item* warc = nullptr;

    if (curIdx == 0)
        SetWaveArchive(warcType, warcPtr, WaveArchiveType::AutomaticShared, nullptr);
    else if (curIdx == 1)
        SetWaveArchive(warcType, warcPtr, WaveArchiveType::AutomaticIndividual, nullptr);
    else if (FindItemAt(warcList, curIdx - cWaveArchiveAutoCount, &warc))
        SetWaveArchive(warcType, warcPtr, WaveArchiveType::Explicit, warc);

    return true;
}

bool WaveArchiveSelector(const char* name, WaveArchiveType* warcType, Item** warcPtr, const Item::List& warcList)
{
    SEAD_ASSERT(name);
    SEAD_ASSERT(warcType);
    SEAD_ASSERT(warcPtr);
    SEAD_ASSERT(*warcType != WaveArchiveType::Explicit || *warcPtr != nullptr);

    const WaveArchiveType oldWarcType = *warcType;
    Item* const oldWarc = *warcPtr;

    sead::FixedSafeString<256> itemName;
    FormatWaveArchiveChoice(*warcType, *warcPtr, &itemName);

    bool changed = false;

    if (ImGui::BeginCombo(sead::FormatFixedSafeString<64>("##%sCombo", name).cstr(), itemName.cstr()))
    {
        if (DrawWaveArchiveComboEntries(warcType, warcPtr, warcList))
            changed = true;

        ImGui::EndCombo();
    }
    else
    {
        ImGui::SetItemTooltip("%s", messages::browser::cBrowseTip);
    }

    if (StepWaveArchiveIndex(warcType, warcPtr, warcList, GetItemWheelSteps()))
        changed = true;

    if (*warcType == WaveArchiveType::Explicit)
        DrawGoToContextMenu(*warcPtr, messages::reference::cNothingAttached);
    else
        DrawGoToContextMenu(nullptr, messages::reference::cWaveArchiveNotExplicit);

    DrawSelectorLabel(name, false);

    return changed && (*warcType != oldWarcType || *warcPtr != oldWarc);
}
