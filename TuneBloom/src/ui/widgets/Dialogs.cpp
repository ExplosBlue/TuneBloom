#include <ui/widgets/Dialogs.h>

#include <ui/settings/Config.h>
#include <ui/items/ItemColumns.h>
#include <ui/widgets/Properties.h>
#include <ui/settings/Theme.h>
#include <ui/settings/UiColors.h>
#include <ui/widgets/WindowAnimation.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_internal.h>

#include <cstring>

static constexpr f32 cDialogRowGapEm = 0.4f;

void CenterNextDialog()
{
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
}

static constexpr f32 cDialogButtonWidthEm = 6.0f;
static constexpr f32 cDialogPadEm         = 0.8f;
static constexpr f32 cDialogMinWidthEm    = 20.0f;
static constexpr f32 cDialogFooterGapEm   = 0.8f;
static constexpr f32 cDialogFooterPadEm   = 0.4f;

static f32 sDialogButtonWidth = 0.0f;
static bool sDialogButtonFirst = true;

f32 CalcDialogFooterHeight()
{
    return ImGui::GetFrameHeightWithSpacing();
}

static f32 CalcDialogButtonWidth(std::initializer_list<const char*> labels)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 em = ImGui::GetFontSize();

    f32 width = ImFloor(em * cDialogButtonWidthEm);

    for (const char* label : labels)
        width = ImMax(width, ImFloor(ImGui::CalcTextSize(label, nullptr, true).x + style.FramePadding.x * 2.0f + em));

    return width;
}

f32 CalcDialogFooterWidth(std::initializer_list<const char*> labels)
{
    const s32 buttonNum = static_cast<s32>(labels.size());

    return CalcDialogButtonWidth(labels) * buttonNum + ImGui::GetStyle().ItemSpacing.x * (buttonNum - 1);
}

void BeginDialogFooter(std::initializer_list<const char*> labels, bool separator)
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

    ImGui::SetCursorPosX(ImMax(cursorX, cursorX + ImGui::GetContentRegionAvail().x - totalWidth));
}

bool BeginDialog(const char* name, bool* open, ImGuiWindowFlags flags)
{
    const ImVec4 background = uicolors::Opaque(ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));

    const f32 pad = ImFloor(ImGui::GetFontSize() * cDialogPadEm);

    if ((flags & ImGuiWindowFlags_AlwaysAutoResize) && !(GImGui->NextWindowData.Flags & ImGuiNextWindowDataFlags_HasSizeConstraint))
        ImGui::SetNextWindowSizeConstraints(ImVec2(ImFloor(ImGui::GetFontSize() * cDialogMinWidthEm), 0.0f), ImVec2(FLT_MAX, FLT_MAX));

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

void DialogRow(const char* label, const char* help)
{
    DialogRowGap();
    PropertyRow(label, help);
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
    ICON_LC_FOLDERS
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

static ImFont* GetDialogIconFont(f32 fontSize)
{
    for (ImFont* font : ImGui::GetIO().Fonts->Fonts)
    {
        if (font->FontSize == fontSize)
            return font;
    }

    return ImGui::GetFont();
}

void BeginDialogMessage(const char* icon, ImGuiCol color)
{
    const f32 spacing = ImGui::GetStyle().ItemSpacing.y;
    const f32 fontSize = ScaleSize(cDialogIconFontSize);

    ImFont* font = GetDialogIconFont(fontSize);
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

bool BeginDialogList(const char* id, s32 columnNum, s32 rowNum)
{
    const f32 em = ImGui::GetFontSize();
    const ImVec2 cellPadding(ImFloor(em * cDialogListCellPadXEm), ImFloor(em * cDialogListCellPadYEm));
    const f32 rowHeight = ImGui::GetFrameHeight() + cellPadding.y * 2.0f;
    const f32 headerHeight = em + cellPadding.y * 2.0f;

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
