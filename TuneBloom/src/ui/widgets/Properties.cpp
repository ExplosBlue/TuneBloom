#include <ui/widgets/Properties.h>

#include <ui/widgets/Dialogs.h>
#include <ui/items/ItemColumns.h>
#include <ui/widgets/Knob.h>
#include <messages/Messages.h>
#include <ui/widgets/PanelLayout.h>
#include <ui/widgets/Widgets.h>

#include <imgui/imgui_custom.h>
#include <imgui/imgui_internal.h>

#include <cstdio>
#include <cstring>

static constexpr f32 cGroupBoxTintAlpha = 0.25f;
static constexpr f32 cGroupBoxGapEm     = 0.5f;
static constexpr f32 cPageHeaderGapEm   = 0.5f;
static constexpr f32 cChoiceGapEm       = 1.0f;

static constexpr f32 cPropertyLabelWidthEm = 6.4f;

PageColumn BeginPageColumn(f32 widthEm)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const f32 columnMaxX = ImGui::GetCursorScreenPos().x + ImFloor(ImGui::GetFontSize() * widthEm);

    PageColumn column;
    column.workMaxX = window->WorkRect.Max.x;
    column.contentMaxX = window->ContentRegionRect.Max.x;

    window->WorkRect.Max.x = sead::Mathf::min(column.workMaxX, columnMaxX);
    window->ContentRegionRect.Max.x = sead::Mathf::min(column.contentMaxX, columnMaxX);

    return column;
}

void EndPageColumn(const PageColumn& column)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();

    window->WorkRect.Max.x = column.workMaxX;
    window->ContentRegionRect.Max.x = column.contentMaxX;
}

bool DrawPageHeader(const char* icon, const char* title, const char* description, const char* action)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 em = ImGui::GetFontSize();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const f32 width = ImGui::GetContentRegionAvail().x;
    const f32 actionWidth = action ? CalcTextButtonWidth(action) : 0.0f;

    bool pressed = false;

    if (description)
    {
        BeginDialogMessage(icon, ImGuiCol_CheckMark);
        ImGui::TextUnformatted(title);
        ImGui::TextDisabled("%s", description);
        EndDialogMessage();
    }
    else
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), "%s", icon);
        ImGui::SameLine();
        ImGui::TextUnformatted(title);
    }

    if (action)
    {
        const ImVec2 next = ImGui::GetCursorScreenPos();

        ImGui::SetCursorScreenPos(ImVec2(pos.x + width - actionWidth, pos.y));
        pressed = TextButton(action);
        ImGui::SetCursorScreenPos(next);
    }

    const f32 lineY = ImFloor(ImGui::GetCursorScreenPos().y + em * cPageHeaderGapEm * 0.5f);

    ImGui::GetWindowDrawList()->AddLine(ImVec2(pos.x, lineY), ImVec2(pos.x + width, lineY), ImGui::GetColorU32(ImGuiCol_Separator));
    ImGui::Dummy(ImVec2(0.0f, ImFloor(em * cPageHeaderGapEm)));

    return pressed;
}

static bool BeginBoxTable(const char* id, ImGuiTableFlags flags)
{
    const f32 em = ImGui::GetFontSize();

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ImFloor(em * cSoundPanelPadXEm), ImFloor(em * cSoundPanelPadYEm)));

    if (!ImGui::BeginTable(id, 1, flags))
    {
        ImGui::PopStyleVar();
        return false;
    }

    ImGui::TableNextColumn();

    return true;
}

static bool BeginTintedBoxTable(const char* title)
{
    if (!BeginBoxTable(title, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_SizingStretchSame))
        return false;

    TintGroupBoxCell();

    return true;
}

void TintGroupBoxCell()
{
    ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(ImGuiCol_FrameBg, cGroupBoxTintAlpha));
}

void GroupBoxGap()
{
    ImGui::Dummy(ImVec2(0.0f, ImFloor(ImGui::GetFontSize() * cGroupBoxGapEm) - ImGui::GetStyle().ItemSpacing.y));
}

bool BeginGroupBox(const char* title, const char* action, bool* pressed, f32 actionMinWidth)
{
    if (!BeginTintedBoxTable(title))
        return false;

    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const f32 width = ImGui::GetContentRegionAvail().x;
    const f32 actionWidth = action ? ImMax(actionMinWidth, CalcTextButtonWidth(action)) : 0.0f;

    RegionHeader(title, action ? actionWidth + style.ItemSpacing.x : 0.0f);

    if (action)
    {
        ImGui::SameLine();
        ImGui::SetCursorScreenPos(ImVec2(pos.x + width - actionWidth, pos.y));
        *pressed = ImGui::Button(action, ImVec2(actionWidth, 0.0f));
    }

    return true;
}

bool BeginGroupBoxWithTools(const char* title, f32 toolsWidth)
{
    if (!BeginTintedBoxTable(title))
        return false;

    RegionHeader(title, toolsWidth + ImGui::GetStyle().ItemSpacing.x);

    return true;
}

bool BeginGroupBoxWithStatus(const char* title, const char* status)
{
    const f32 statusWidth = ImGui::CalcTextSize(status).x;

    if (!BeginGroupBoxWithTools(title, statusWidth))
        return false;

    const ImVec2 pos(ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x - statusWidth, ImGui::GetItemRectMin().y + ImGui::GetStyle().FramePadding.y);

    ImGui::GetWindowDrawList()->AddText(pos, ImGui::GetColorU32(GetItemValueColor()), status);

    return true;
}

f32 CalcGroupBoxWidth(f32 contentWidth)
{
    return ImCeil(contentWidth) + ImFloor(ImGui::GetFontSize() * cSoundPanelPadXEm) * 2.0f + 2.0f;
}

f32 CalcChoiceRowWidth(const char* const* labels, s32 labelNum)
{
    const ImGuiStyle& style = ImGui::GetStyle();

    f32 width = ImFloor(ImGui::GetFontSize() * cChoiceGapEm) * (labelNum - 1);

    for (s32 i = 0; i < labelNum; i++)
        width += ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + ImGui::CalcTextSize(labels[i], nullptr, true).x;

    return width;
}

bool DrawChoiceRow(s32* selected, const char* const* labels, s32 labelNum, const char* const* details)
{
    const s32 oldSelected = *selected;
    const f32 gap = ImFloor(ImGui::GetFontSize() * cChoiceGapEm);

    for (s32 i = 0; i < labelNum; i++)
    {
        if (i > 0)
            ImGui::SameLine(0.0f, gap);

        ImGui::RadioButton(labels[i], selected, i);

        if (details)
            DetailTooltip(details[i]);
    }

    return *selected != oldSelected;
}

bool BeginGroupBoxWithChoice(const char* title, s32* selected, const char* const* labels, s32 labelNum)
{
    const f32 choiceWidth = CalcChoiceRowWidth(labels, labelNum);

    if (!BeginGroupBoxWithTools(title, choiceWidth))
        return false;

    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - choiceWidth);

    DrawChoiceRow(selected, labels, labelNum);

    return true;
}

void EndGroupBox()
{
    ImGui::EndTable();
    ImGui::PopStyleVar();
    GroupBoxGap();
}

void GroupBoxRowLabel(const char* label, f32 labelWidth)
{
    DialogRowGap();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-FLT_MIN);
}

f32 CalcPropertyLabelWidth()
{
    return ImFloor(ImGui::GetFontSize() * cPropertyLabelWidthEm);
}

void PropertyRow(const char* label, const char* help)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);

    if (help)
    {
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        HelpMarker(help);
    }

    PropertyValueColumn();
}

void PropertyValueColumn()
{
    const ImGuiWindow* window = ImGui::GetCurrentWindow();

    ImGui::SameLine(CalcPropertyLabelWidth() - window->DC.ColumnsOffset.x - window->DC.GroupOffset.x);
    ImGui::SetNextItemWidth(-FLT_MIN);
}

bool PropertyToggleRow(const char* label, bool* enabled)
{
    PropertyRow(label);

    ImGui::PushID(label);
    const bool changed = ImGui::Checkbox("##enable", enabled);
    ImGui::PopID();

    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::SetNextItemWidth(-FLT_MIN);

    return changed;
}

struct PropertyFoldState
{
    char title[32];
    bool open;
};

static constexpr s32 cPropertyFoldStateMax = 32;

static PropertyFoldState sPropertyFoldStates[cPropertyFoldStateMax];
static s32 sPropertyFoldStateNum = 0;

static bool* FindPropertyFoldState(const char* title, bool defaultOpen)
{
    for (s32 i = 0; i < sPropertyFoldStateNum; i++)
    {
        if (strcmp(sPropertyFoldStates[i].title, title) == 0)
            return &sPropertyFoldStates[i].open;
    }

    if (sPropertyFoldStateNum >= cPropertyFoldStateMax)
    {
        static bool sOverflowOpen;
        sOverflowOpen = defaultOpen;

        return &sOverflowOpen;
    }

    PropertyFoldState& state = sPropertyFoldStates[sPropertyFoldStateNum++];
    snprintf(state.title, sizeof(state.title), "%s", title);
    state.open = defaultOpen;

    return &state.open;
}

static void* PropertySettingsReadOpen(ImGuiContext*, ImGuiSettingsHandler*, const char*)
{
    return sPropertyFoldStates;
}

static void PropertySettingsReadLine(ImGuiContext*, ImGuiSettingsHandler*, void*, const char* line)
{
    const char* separator = strrchr(line, '=');

    if (!separator || separator == line || sPropertyFoldStateNum >= cPropertyFoldStateMax)
        return;

    const sead::FormatFixedSafeString<32> title("%.*s", static_cast<s32>(separator - line), line);

    *FindPropertyFoldState(title.cstr(), true) = separator[1] != '0';
}

static void PropertySettingsWriteAll(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buf)
{
    buf->appendf("[%s][Sections]\n", handler->TypeName);

    for (s32 i = 0; i < sPropertyFoldStateNum; i++)
        buf->appendf("%s=%d\n", sPropertyFoldStates[i].title, sPropertyFoldStates[i].open ? 1 : 0);

    buf->append("\n");
}

void RegisterPropertySettings()
{
    ImGuiSettingsHandler handler;
    handler.TypeName = "Properties";
    handler.TypeHash = ImHashStr("Properties");
    handler.ReadOpenFn = PropertySettingsReadOpen;
    handler.ReadLineFn = PropertySettingsReadLine;
    handler.WriteAllFn = PropertySettingsWriteAll;

    ImGui::AddSettingsHandler(&handler);
}

static constexpr f32 cPropertySectionBandPadEm = 0.15f;
static constexpr f32 cPropertySectionIconGapEm = 0.4f;
static constexpr f32 cPropertySectionPadXEm    = 0.5f;
static constexpr f32 cPropertySectionPadYEm    = 0.4f;

static void DrawSectionBand(const ImVec2& min, const ImVec2& max, ImU32 fill)
{
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 bandMax(max.x + 1.0f, max.y);

    draw->AddRectFilled(min, bandMax, fill);
    draw->AddRect(min, bandMax, ImGui::GetColorU32(ImGuiCol_TableBorderStrong));
}

static bool BeginSectionBody(s32 columnNum, ImGuiTableFlags flags, const f32* columnWeights, f32 minContentHeight, s32 line = 0)
{
    const f32 em = ImGui::GetFontSize();
    const ImVec2 padding(ImFloor(em * cPropertySectionPadXEm), ImFloor(em * cPropertySectionPadYEm));

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, padding);

    sead::FixedSafeString<16> id("##body");
    if (line > 0)
        id.appendWithFormat("%d", line);

    if (!ImGui::BeginTable(id.cstr(), columnNum, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV | flags))
    {
        ImGui::PopStyleVar();
        ImGui::PopID();
        return false;
    }

    if (columnWeights)
    {
        for (s32 i = 0; i < columnNum; i++)
            ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch, columnWeights[i]);
    }

    if (minContentHeight > 0.0f)
        ImGui::TableNextRow(ImGuiTableRowFlags_None, minContentHeight + padding.y * 2.0f);

    ImGui::TableNextColumn();

    return true;
}

f32 CalcSectionTitleHeight()
{
    return ImGui::GetFrameHeight() + ImFloor(ImGui::GetFontSize() * cPropertySectionBandPadEm) * 2.0f;
}

static f32 CalcCapCenterTextY(f32 top, f32 height)
{
    return ImFloor(top + height * 0.5f - CalcCapCenterOffset(ImGui::GetFont(), ImGui::GetFontSize()) + 0.5f);
}

bool DrawSectionTitle(const char* title, f32 width, const char* status, const char* action, const char* icon, u32 iconColor, f32 textInset)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 bandPad = ImFloor(ImGui::GetFontSize() * cPropertySectionBandPadEm);
    const f32 inset = textInset >= 0.0f ? textInset : bandPad + style.FramePadding.x;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 max(pos.x + width, pos.y + CalcSectionTitleHeight());
    ImVec2 textPos(pos.x + inset, CalcCapCenterTextY(pos.y, max.y - pos.y));
    const f32 actionWidth = action ? CalcTextActionWidth(action) : 0.0f;
    const f32 actionX = max.x - inset + style.FramePadding.x - actionWidth;
    const f32 statusRight = action ? actionX + style.FramePadding.x - ImFloor(ImGui::GetFontSize()) : max.x - inset;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    bool pressed = false;

    DrawSectionBand(pos, max, ImGui::GetColorU32(ImGuiCol_FrameBg));

    if (icon)
    {
        draw->AddText(textPos, iconColor, icon);
        textPos.x += ImGui::CalcTextSize(icon).x + ImFloor(ImGui::GetFontSize() * cPropertySectionIconGapEm);
    }

    draw->AddText(textPos, ImGui::GetColorU32(ImGuiCol_Text), title);

    if (status)
        draw->AddText(ImVec2(statusRight - ImGui::CalcTextSize(status).x, textPos.y), ImGui::GetColorU32(GetItemValueColor()), status);

    if (action)
    {
        ImGui::SetCursorScreenPos(ImVec2(actionX, pos.y));
        pressed = DrawTextAction("##SectionAction", action, max.y - pos.y, textPos.y - pos.y);
        ImGui::SetCursorScreenPos(pos);
    }

    ImGui::Dummy(ImVec2(width, max.y - pos.y));
    ImGui::SetCursorScreenPos(ImVec2(pos.x, max.y - 1.0f));

    return pressed;
}

bool BeginPropertySection(const char* title, bool* enabled, bool* changed, bool defaultOpen, s32 columnNum, const char* detail, const char* value, const char* enableDetail)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 em = ImGui::GetFontSize();
    const f32 frameHeight = ImGui::GetFrameHeight();
    const f32 bandPad = ImFloor(em * cPropertySectionBandPadEm);
    const f32 width = ImGui::GetContentRegionAvail().x;
    const f32 height = frameHeight + bandPad * 2.0f;
    const f32 checkArea = enabled ? frameHeight + bandPad * 2.0f : 0.0f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 max(pos.x + width, pos.y + height);

    const char* key = strstr(title, "###");
    bool* fold = FindPropertyFoldState(key ? key + 3 : title, defaultOpen);
    const bool on = !enabled || *enabled;
    bool enableChanged = false;

    ImGui::PushID(title);

    const bool toggle = ImGui::InvisibleButton("##fold", ImVec2(width - checkArea, height)) && on;
    const bool hovered = on && ImGui::IsItemHovered();
    const bool held = on && ImGui::IsItemActive();

    DetailTooltip(detail);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    DrawSectionBand(pos, max, ImGui::GetHoverColorU32(ImGui::GetID("##fold") + 1, hovered, held, ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive));

    const ImU32 textColor = ImGui::GetColorU32(on ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    const ImVec2 arrowPos(pos.x + bandPad + style.FramePadding.x, pos.y + bandPad + style.FramePadding.y);

    ImGui::RenderArrow(draw, arrowPos, textColor, on && *fold ? ImGuiDir_Down : ImGuiDir_Right);
    draw->AddText(ImVec2(arrowPos.x + em + style.ItemInnerSpacing.x, arrowPos.y), textColor, title, ImGui::FindRenderedTextEnd(title));

    if (value)
        draw->AddText(ImVec2(max.x - checkArea - bandPad - style.FramePadding.x - ImGui::CalcTextSize(value).x, arrowPos.y), ImGui::GetColorU32(GetItemValueColor()), value);

    if (enabled)
    {
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::SetCursorScreenPos(ImVec2(max.x - bandPad - frameHeight, pos.y + bandPad));

        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
        enableChanged = ImGui::Checkbox("##enable", enabled);
        ImGui::PopStyleColor();

        if (enableDetail)
            ImGui::SetItemTooltip("%s", enableDetail);

        ImGui::SetCursorScreenPos(ImVec2(pos.x, max.y));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    }

    if (toggle)
        *fold = !*fold;
    else if (enableChanged && *enabled)
        *fold = true;

    if (toggle || enableChanged)
        ImGui::MarkIniSettingsDirty();

    if (changed)
        *changed = enableChanged;

    if (!*fold || (enabled && !*enabled))
    {
        ImGui::PopID();
        return false;
    }

    ImGui::SetCursorScreenPos(ImVec2(pos.x, max.y - 1.0f));

    return BeginSectionBody(columnNum, ImGuiTableFlags_None, nullptr, 0.0f);
}

void EndPropertySection()
{
    ImGui::EndTable();
    ImGui::PopStyleVar();
    ImGui::PopID();
}

f32 CalcCardPadX()
{
    return ImFloor(ImGui::GetFontSize() * cPropertySectionPadXEm);
}

f32 CalcCardBottomExtra()
{
    return ImFloor(ImGui::GetFontSize() * cPropertySectionPadYEm) + 1.0f;
}

bool BeginCard(const char* title, const char* status, s32 columnNum, const f32* columnWeights, f32 minContentHeight)
{
    ImGui::PushID(title);

    DrawSectionTitle(title, ImGui::GetContentRegionAvail().x, status, nullptr, nullptr, 0, CalcCardPadX() + 1.0f);

    return BeginSectionBody(columnNum, ImGuiTableFlags_BordersInnerH, columnWeights, minContentHeight);
}

bool NextCardLine(s32 line, s32 columnNum, const f32* columnWeights)
{
    ImGui::EndTable();
    ImGui::PopStyleVar();

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y - 1.0f);

    return BeginSectionBody(columnNum, ImGuiTableFlags_BordersInnerH, columnWeights, 0.0f, line);
}

void EndCard()
{
    EndPropertySection();
}

static constexpr f32 cPanelHeaderGapEm  = 0.3f;
static constexpr f32 cPanelComboExtraEm = 0.5f;

bool BeginPropertyBox(const char* id)
{
    return BeginBoxTable(id, ImGuiTableFlags_BordersOuter);
}

void EndPropertyBox()
{
    ImGui::EndTable();
    ImGui::PopStyleVar();
}

bool DrawPanelHeader(const char* title, bool* enabled, const char* detail)
{
    const ImVec2 headerPos = ImGui::GetCursorScreenPos();
    const f32 checkWidth = ImGui::GetFrameHeight();
    const f32 checkX = headerPos.x + ImGui::GetContentRegionAvail().x - checkWidth;

    RegionHeader(title, checkWidth + ImGui::GetStyle().ItemSpacing.x);

    ImGui::SameLine();
    ImGui::SetCursorScreenPos(ImVec2(checkX, headerPos.y));

    ImGui::PushID(title);
    const bool changed = ImGui::Checkbox("##enable", enabled);
    ImGui::PopID();

    if (detail)
        ImGui::SetItemTooltip("%s", detail);

    return changed;
}

bool DrawPanelHeaderCombo(const char* title, const char* id, s32* current, const char* const items[], const char* const details[], s32 itemNum, const char* detail, bool rowsBelow)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 em = ImGui::GetFontSize();

    f32 comboWidth = 0.0f;
    for (s32 i = 0; i < itemNum; i++)
        comboWidth = sead::Mathf::max(comboWidth, ImGui::CalcTextSize(items[i]).x);

    comboWidth += ImGui::GetFrameHeight() + style.FramePadding.x * 2.0f + ImFloor(em * cPanelComboExtraEm);

    const ImVec2 headerPos = ImGui::GetCursorScreenPos();
    const f32 comboX = headerPos.x + ImGui::GetContentRegionAvail().x - comboWidth;

    RegionHeader(title, comboWidth + style.ItemSpacing.x);
    DetailTooltip(detail);

    ImGui::SameLine();
    ImGui::SetCursorScreenPos(ImVec2(comboX, headerPos.y));
    ImGui::SetNextItemWidth(comboWidth);

    const bool changed = details ? ComboScroll(id, current, items, details, itemNum, detail) : ComboScroll(id, current, items, itemNum);

    if (rowsBelow)
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImFloor(em * cPanelHeaderGapEm));

    return changed;
}
