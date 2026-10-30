#include <ui/widgets/Dialogs.h>

#include <ui/settings/Config.h>
#include <ui/items/ItemColumns.h>
#include <ui/widgets/Properties.h>
#include <ui/settings/Theme.h>
#include <ui/settings/UiColors.h>
#include <ui/widgets/WindowAnimation.h>
#include <ui/widgets/Widgets.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_internal.h>

#include <cstring>

static constexpr f32 cDialogRowGapEm   = 0.4f;
static constexpr f32 cDialogGroupGapEm = 0.8f;

static ImVec2 sDetachedDialogArea;

void CenterNextDialog()
{
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
}

void SetDetachedDialogArea(const ImVec2& size)
{
    sDetachedDialogArea = size;
}

static bool HasDetachedDialogArea()
{
    return sDetachedDialogArea.x > 0.0f && sDetachedDialogArea.y > 0.0f;
}

bool AreDialogsDetached()
{
    return HasDetachedDialogArea() && (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable);
}

ImVec2 GetDialogAreaSize()
{
    if (HasDetachedDialogArea())
        return sDetachedDialogArea;

    return ImGui::GetMainViewport()->WorkSize;
}

static constexpr f32 cDialogButtonWidthEm = 6.0f;
static constexpr f32 cDialogPadEm         = 0.8f;
static constexpr f32 cDialogMinWidthEm    = 20.0f;
static constexpr f32 cDialogFooterGapEm   = 0.8f;
static constexpr f32 cDialogFooterPadEm   = 0.4f;
static constexpr f32 cFooterWarningGapEm  = 0.5f;

static f32 sDialogButtonWidth = 0.0f;
static bool sDialogButtonFirst = true;

f32 CalcDialogFooterHeight()
{
    return ImGui::GetFrameHeightWithSpacing();
}

static f32 CalcDialogButtonWidth(std::initializer_list<const char*> labels)
{
    f32 width = ImFloor(ImGui::GetFontSize() * cDialogButtonWidthEm);

    for (const char* label : labels)
        width = ImMax(width, CalcTextButtonWidth(label));

    return width;
}

f32 CalcDialogFooterWidth(std::initializer_list<const char*> labels)
{
    const s32 buttonNum = static_cast<s32>(labels.size());

    return CalcDialogButtonWidth(labels) * buttonNum + ImGui::GetStyle().ItemSpacing.x * (buttonNum - 1);
}

static void DrawFooterWarning(const char* warning, f32 right)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 pos = ImGui::GetCursorScreenPos() + ImVec2(0.0f, style.FramePadding.y);
    const ImVec2 iconSize = ImGui::CalcTextSize(ICON_LC_TRIANGLE_ALERT);
    const f32 textX = pos.x + iconSize.x + ImFloor(ImGui::GetFontSize() * cFooterWarningGapEm);
    const ImVec2 textMax(right, pos.y + ImGui::GetFontSize());
    const ImU32 color = ImGui::GetColorU32(uicolors::Warning());

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddText(pos, color, ICON_LC_TRIANGLE_ALERT);

    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::RenderTextEllipsis(drawList, ImVec2(textX, pos.y), textMax, right, right, warning, nullptr, nullptr);
    ImGui::PopStyleColor();

    const bool isCut = textX + ImGui::CalcTextSize(warning).x > right;

    if (isCut && ImGui::IsMouseHoveringRect(pos, textMax))
        ImGui::SetTooltip("%s", warning);
}

void BeginDialogFooter(std::initializer_list<const char*> labels, bool separator, const char* warning)
{
    sDialogButtonWidth = CalcDialogButtonWidth(labels);
    sDialogButtonFirst = true;

    if (separator)
    {
        const f32 em = ImGui::GetFontSize();
        const f32 spacing = ImGui::GetStyle().ItemSpacing.y;

        ImGui::Dummy(ImVec2(0.0f, ImFloor(em * cDialogFooterGapEm) - spacing * 2.0f));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, ImFloor(em * cDialogFooterPadEm) - spacing * 2.0f));
    }

    const f32 totalWidth = CalcDialogFooterWidth(labels);
    const f32 cursorX = ImGui::GetCursorPosX();
    const f32 buttonX = ImMax(cursorX, cursorX + ImGui::GetContentRegionAvail().x - totalWidth);

    if (warning)
        DrawFooterWarning(warning, ImGui::GetCursorScreenPos().x + buttonX - cursorX - ImGui::GetFontSize());

    ImGui::SetCursorPosX(buttonX);
}

bool BeginDialog(const char* name, bool* open, ImGuiWindowFlags flags)
{
    const ImVec4 background = uicolors::Opaque(ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));

    const f32 pad = ImFloor(ImGui::GetFontSize() * cDialogPadEm);

    if ((flags & ImGuiWindowFlags_AlwaysAutoResize) && !(GImGui->NextWindowData.Flags & ImGuiNextWindowDataFlags_HasSizeConstraint))
        ImGui::SetNextWindowSizeConstraints(ImVec2(ImFloor(ImGui::GetFontSize() * cDialogMinWidthEm), 0.0f), ImVec2(FLT_MAX, FLT_MAX));

    if (AreDialogsDetached())
    {
        ImGuiWindowClass windowClass;
        windowClass.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
        ImGui::SetNextWindowClass(&windowClass);
    }

    ImGui::PushStyleColor(ImGuiCol_PopupBg, background);
    ImGui::PushStyleColor(ImGuiCol_ResizeGrip, uicolors::cTransparent);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
    const bool visible = ImGui::BeginPopupModal(name, open, flags);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);

    if (visible)
        AnimateWindow(WindowAnimation::Dialog);

    return visible;
}

bool DialogButton(const char* label)
{
    if (!sDialogButtonFirst)
        ImGui::SameLine();

    sDialogButtonFirst = false;

    return ImGui::Button(label, ImVec2(sDialogButtonWidth, 0.0f));
}

void DialogRowGap()
{
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImFloor(ImGui::GetFontSize() * cDialogRowGapEm));
}

void DialogGroupGap()
{
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImFloor(ImGui::GetFontSize() * cDialogGroupGapEm));
}

void DialogRow(const char* label, const char* help)
{
    DialogRowGap();
    PropertyRow(label, help);
}

static constexpr f32 cNavRowGapEm   = 0.5f;
static constexpr f32 cNavPagePadXEm = 1.0f;
static constexpr f32 cNavPagePadYEm = 0.8f;
static constexpr f32 cNavBarWidthEm = 0.15f;
static constexpr f32 cNavTintAlpha  = 0.25f;

static f32 sNavPageHeight = 0.0f;

bool BeginNavDialog(const char* id, const ImVec2& size, f32 navWidth)
{
    const f32 em = ImGui::GetFontSize();
    const ImVec2 padding(ImFloor(em * cNavPagePadXEm), ImFloor(em * cNavPagePadYEm));

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, padding);

    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV, size))
    {
        ImGui::PopStyleVar();
        return false;
    }

    sNavPageHeight = size.y - padding.y * 2.0f;

    ImGui::TableSetupColumn("##Nav", ImGuiTableColumnFlags_WidthFixed, navWidth);
    ImGui::TableSetupColumn("##Page", ImGuiTableColumnFlags_WidthStretch);

    ImGui::TableNextColumn();
    ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(ImGuiCol_FrameBg, cNavTintAlpha));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetStyle().FramePadding.y);

    ImGui::PushItemFlag(ImGuiItemFlags_SelectableDontClosePopup, true);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, ImFloor(em * cNavRowGapEm)));

    return true;
}

bool DrawNavRow(const char* icon, const char* name, bool selected, const char* value, bool dim, bool title, f32 indent)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 iconWidth = ImGui::CalcTextSize(ICON_LC_INFO " ").x;

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const f32 width = ImGui::GetContentRegionAvail().x;

    const bool pressed = ImGui::Selectable("##Row", selected, ImGuiSelectableFlags_SpanAvailWidth);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec4 nameColor = ImGui::GetStyleColorVec4(dim ? ImGuiCol_TextDisabled : ImGuiCol_Text);

    f32 left = pos.x + style.FramePadding.x + indent;
    f32 right = pos.x + width - style.FramePadding.x;

    if (value)
    {
        const f32 textWidth = ImGui::CalcTextSize(value).x;
        const ImVec4 valueColor = dim ? nameColor : GetItemValueColor();

        drawList->AddText(ImVec2(right - textWidth, pos.y), ImGui::ColorConvertFloat4ToU32(valueColor), value);
        right -= textWidth + style.ItemSpacing.x;
    }

    drawList->AddText(ImVec2(left, pos.y), ImGui::ColorConvertFloat4ToU32(nameColor), icon);
    left += iconWidth;

    ImGui::PushStyleColor(ImGuiCol_Text, nameColor);
    ImGui::RenderTextEllipsis(drawList, ImVec2(left, pos.y), ImVec2(right, pos.y + ImGui::GetFontSize()), right, right, name, nullptr, nullptr);
    ImGui::PopStyleColor();

    if (title)
    {
        const f32 lineLeft = left + ImGui::CalcTextSize(name).x + style.ItemSpacing.x;
        const f32 lineY = ImFloor(pos.y + ImGui::GetFontSize() * 0.5f);

        if (lineLeft < right)
            drawList->AddLine(ImVec2(lineLeft, lineY), ImVec2(right, lineY), ImGui::GetColorU32(ImGuiCol_Separator));
    }

    return pressed;
}

bool DialogNavRow(const char* icon, const char* name, bool selected)
{
    const bool pressed = DrawNavRow(icon, name, selected);

    if (selected)
    {
        const ImVec2 rowMin = ImGui::GetItemRectMin();
        const ImVec2 rowMax = ImGui::GetItemRectMax();

        ImGui::GetWindowDrawList()->AddRectFilled(rowMin, ImVec2(rowMin.x + ImFloor(ImGui::GetFontSize() * cNavBarWidthEm), rowMax.y), ImGui::GetColorU32(ImGuiCol_CheckMark));
    }

    return pressed;
}

bool BeginNavDialogPage(s32 pageId)
{
    ImGui::PopStyleVar();
    ImGui::PopItemFlag();

    ImGui::TableNextColumn();
    ImGui::PushID(pageId);

    if (!ImGui::BeginChild("##Page", ImVec2(0.0f, sNavPageHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground))
        return false;

    ImGuiWindow* page = ImGui::GetCurrentWindow();
    page->WorkRect.Max.x -= 1.0f;
    page->ContentRegionRect.Max.x -= 1.0f;

    return true;
}

void EndNavDialog()
{
    ImGui::EndChild();
    ImGui::PopID();
    ImGui::EndTable();
    ImGui::PopStyleVar();
}

static constexpr f32 cDialogIconGapEm = 0.8f;

static constexpr const char* cDialogIcons[] = {
    ICON_LC_CIRCLE_HELP,
    ICON_LC_MUSIC_3,
    ICON_LC_DISC_3,
    ICON_LC_AUDIO_LINES,
    ICON_LC_FILE_QUESTION,
    ICON_LC_FILE_MUSIC,
    ICON_LC_FILE_AUDIO,
    ICON_LC_FOLDERS,
    ICON_LC_PACKAGE
};

static f32 sDialogMessageTextY = 0.0f;

const ImWchar* GetDialogIconRanges()
{
    static ImWchar ranges[IM_ARRAYSIZE(cDialogIcons) * 2 + 1] = {};

    for (s32 i = 0; i < IM_ARRAYSIZE(cDialogIcons); i++)
    {
        u32 code = 0;
        ImTextCharFromUtf8(&code, cDialogIcons[i], nullptr);

        ranges[i * 2] = static_cast<ImWchar>(code);
        ranges[i * 2 + 1] = static_cast<ImWchar>(code);
    }

    return ranges;
}

void BeginDialogMessage(const char* icon, ImGuiCol color)
{
    const f32 spacing = ImGui::GetStyle().ItemSpacing.y;
    const f32 fontSize = ScaleSize(cDialogIconFontSize);

    ImFont* font = FindFontOfSize(fontSize);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const char* iconEnd = strchr(icon, ' ');
    const ImVec2 iconSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, icon, iconEnd);

    ImGui::GetWindowDrawList()->AddText(font, fontSize, ImVec2(ImFloor(pos.x + (fontSize - iconSize.x) * 0.5f), ImFloor(pos.y + (fontSize - iconSize.y) * 0.5f)), ImGui::GetColorU32(color), icon, iconEnd);

    ImGui::Dummy(ImVec2(fontSize, fontSize));
    ImGui::SameLine(0.0f, ImFloor(ImGui::GetFontSize() * cDialogIconGapEm));

    const f32 textHeight = ImGui::GetStateStorage()->GetFloat(ImGui::GetID("##DialogMessage"), ImGui::GetTextLineHeight());
    const f32 offset = ImFloor((fontSize - textHeight) * 0.5f);

    ImGui::BeginGroup();

    if (offset > spacing)
        ImGui::Dummy(ImVec2(0.0f, offset - spacing));

    sDialogMessageTextY = ImGui::GetCursorPosY();
}

void EndDialogMessage()
{
    const f32 textHeight = ImGui::GetCursorPosY() - sDialogMessageTextY - ImGui::GetStyle().ItemSpacing.y;

    ImGui::EndGroup();
    ImGui::GetStateStorage()->SetFloat(ImGui::GetID("##DialogMessage"), textHeight);
}

void DialogNamedText(const char* format, const char* name)
{
    const char* mark = strstr(format, "%s");

    if (!mark)
    {
        ImGui::TextUnformatted(format);
        return;
    }

    ImGui::TextUnformatted(format, mark);
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextColored(GetAccentTextColor(), "%s", name);
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextUnformatted(mark + 2);
}

static constexpr f32 cDialogListCellPadXEm = 0.4f;
static constexpr f32 cDialogListCellPadYEm = 0.2f;

bool BeginDialogList(const char* id, s32 columnNum, s32 rowNum, bool frameHeader)
{
    const f32 em = ImGui::GetFontSize();
    const ImVec2 cellPadding(ImFloor(em * cDialogListCellPadXEm), ImFloor(em * cDialogListCellPadYEm));
    const f32 rowHeight = ImGui::GetFrameHeight() + cellPadding.y * 2.0f;
    const f32 headerHeight = (frameHeader ? ImGui::GetFrameHeight() : em) + cellPadding.y * 2.0f;

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, cellPadding);

    if (ImGui::BeginTable(id, columnNum, ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX, ImVec2(0.0f, headerHeight + rowHeight * rowNum)))
        return true;

    ImGui::PopStyleVar();

    return false;
}

void EndDialogList()
{
    ImGui::EndTable();
    ImGui::PopStyleVar();
}

void DrawDialogListTitles(const char* const* titles, s32 titleNum, bool alignLastRight)
{
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);

    for (s32 i = 0; i < titleNum; i++)
    {
        if (!ImGui::TableSetColumnIndex(i))
            continue;

        if (alignLastRight && i == titleNum - 1)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImMax(0.0f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(titles[i]).x));

        ImGui::TableHeader(titles[i]);
    }
}

bool DrawDialogListCheck(bool* on)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();

    bool rowHovered = false;

    ImGui::SetNextItemAllowOverlap();

    bool changed = DrawTableRowSelectable("##Row", false, &rowHovered);

    if (changed)
        *on = !*on;

    ImGui::SameLine(0.0f, 0.0f);

    if (ImGui::Checkbox("##On", on))
        changed = true;

    return changed;
}

void DrawDialogListText(const char* text, ImU32 color, bool alignRight, bool showEnd)
{
    ImVec2 pos = ImGui::GetCursorScreenPos();
    pos.y += ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset;

    const f32 maxX = pos.x + ImGui::GetContentRegionAvail().x;

    if (showEnd && ImGui::CalcTextSize(text).x > maxX - pos.x)
    {
        DrawTextEnd(ImGui::GetWindowDrawList(), pos, maxX, text, color);
        return;
    }

    if (alignRight)
        pos.x = ImMax(pos.x, maxX - ImGui::CalcTextSize(text).x);

    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), pos, ImVec2(maxX, pos.y + ImGui::GetFontSize()), maxX, maxX, text, nullptr, nullptr);
    ImGui::PopStyleColor();
}
