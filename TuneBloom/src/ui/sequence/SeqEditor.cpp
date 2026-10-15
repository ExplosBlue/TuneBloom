#include <ui/sequence/SeqEditor.h>

#include <ui/widgets/ContextMenu.h>
#include <ui/items/ItemColumns.h>
#include <ui/Messages.h>
#include <ui/sequence/SeqManual.h>
#include <ui/app/Shortcuts.h>
#include <ui/app/UI.h>
#include <ui/settings/UiColors.h>

#include <imgui/imgui.h>
#include <imgui/imgui_custom.h>
#include <imgui/imgui_internal.h>

#include <libs/TextEditor.h>

#include <cstdio>
#include <string>
#include <vector>

static constexpr f32 cBarWidthEm      = 26.0f;
static constexpr f32 cBarCountWidthEm = 5.0f;
static constexpr f32 cBarPaddingEm    = 0.35f;
static constexpr f32 cBarMarginEm     = 1.0f;
static constexpr f32 cBarNarrowEm     = 21.0f;
static constexpr f32 cGoToWidthEm     = 19.0f;
static constexpr f32 cLabelsWidthEm   = 24.0f;
static constexpr s32 cLabelsRowMax    = 12;
static constexpr f32 cStatusPadEm     = 0.15f;
static constexpr f32 cStatusGapEm     = 0.25f;

static f32 sBarInsetRight = 0.0f;

static char sFindBuffer[256] = "";
static char sReplaceBuffer[256] = "";

static const TextEditor* sGoToEditor = nullptr;
static char sGoToBuffer[32] = "";
static bool sGoToFocus = false;

static char sLabelFilter[128] = "";
static s32 sLabelSelected = 0;
static bool sLabelFocus = false;
static bool sLabelScroll = false;

static std::string FitText(std::string text, f32 width)
{
    if (ImGui::CalcTextSize(text.c_str()).x <= width)
        return text;

    while (!text.empty() && ImGui::CalcTextSize((text + "...").c_str()).x > width)
    {
        while (!text.empty())
        {
            const u8 c = static_cast<u8>(text.back());
            text.pop_back();

            if ((c & 0xC0) != 0x80)
                break;
        }
    }

    return text + "...";
}

static bool BarButton(const char* label, const char* tooltip, bool enabled = true)
{
    const f32 size = ImGui::GetFrameHeight();

    ImGui::BeginDisabled(!enabled);
    ImGui::PushStyleColor(ImGuiCol_Button, uicolors::cTransparentU32);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

    const bool pressed = ImGui::Button(label, ImVec2(size, size));

    ImGui::PopStyleColor(2);
    ImGui::EndDisabled();

    if (enabled)
        ImGui::SetItemTooltip("%s", tooltip);

    return pressed;
}

static bool BeginEditorBar(const ImVec2& editorMin, ImVec2 editorMax, f32 width, s32 rowNum)
{
    editorMax.x -= sBarInsetRight;

    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 em = ImGui::GetFontSize();
    const f32 padding = ImFloor(em * cBarPaddingEm);
    const f32 free = editorMax.x - style.ScrollbarSize - editorMin.x;
    const f32 margin = free - width < em * cBarMarginEm * 2.0f ? padding : ImFloor(em * cBarMarginEm);
    const f32 right = editorMax.x - style.ScrollbarSize - margin;

    width = ImMax(em * 10.0f, ImMin(width, free - margin * 2.0f));

    const f32 height = rowNum * ImGui::GetFrameHeight() + (rowNum - 1) * style.ItemSpacing.y + padding * 2.0f;

    const ImVec4 background = uicolors::Opaque(ImGui::GetStyleColorVec4(ImGuiCol_PopupBg));

    ImGui::SetCursorScreenPos(ImVec2(right - width, editorMin.y));

    ImGui::PushStyleColor(ImGuiCol_ChildBg, background);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding, padding));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ImFloor(em * 0.4f));

    const bool open = ImGui::BeginChild("##SeqEditorBar", ImVec2(width, height), ImGuiChildFlags_Border | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    return open;
}

static void DrawFindBar(TextEditor& editor, const ImVec2& editorMin, const ImVec2& editorMax, bool paneFocused)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 em = ImGui::GetFontSize();
    const bool replaceVisible = editor.IsReplaceVisible();

    if (BeginEditorBar(editorMin, editorMax, em * cBarWidthEm, replaceVisible ? 2 : 1))
    {
        const f32 button = ImGui::GetFrameHeight();
        const f32 gap = style.ItemInnerSpacing.x;
        const bool showCount = ImGui::GetContentRegionAvail().x >= em * cBarNarrowEm;
        const f32 countWidth = showCount ? ImFloor(em * cBarCountWidthEm) + gap * 2.0f : 0.0f;
        const f32 fieldWidth = ImMax(em * 4.0f, ImGui::GetContentRegionAvail().x - button * 4.0f - countWidth);

        if (editor.GetSearchQuery() != sFindBuffer)
            snprintf(sFindBuffer, sizeof(sFindBuffer), "%s", editor.GetSearchQuery().c_str());

        bool caseSensitive = editor.IsSearchCaseSensitive();
        bool wholeWord = editor.IsSearchWholeWord();
        bool findEnter = false;

        if (paneFocused && shortcuts::Pressed(shortcuts::Action::ToggleMatchCase))
            caseSensitive = !caseSensitive;

        if (paneFocused && shortcuts::Pressed(shortcuts::Action::ToggleWholeWord))
            wholeWord = !wholeWord;

        if (BarButton(replaceVisible ? ICON_LC_CHEVRON_DOWN "##ShowReplace" : ICON_LC_CHEVRON_RIGHT "##ShowReplace", replaceVisible ? messages::sequence::cHideReplace : messages::sequence::cShowReplace))
            editor.SetReplaceVisible(!replaceVisible);

        ImGui::SameLine(0.0f, 0.0f);

        SearchFieldExtra findExtra;
        findExtra.hint = messages::common::cFind;
        findExtra.wholeWord = &wholeWord;
        findExtra.enterPressed = &findEnter;

        SearchField("##Find", sFindBuffer, sizeof(sFindBuffer), fieldWidth, &caseSensitive, editor.ConsumeSearchFocusRequest(), &findExtra);

        editor.SetSearchOptions(caseSensitive, wholeWord);
        editor.SetSearchQuery(sFindBuffer);

        if (findEnter && shortcuts::Held(shortcuts::Modifier::FindPrevious))
            editor.FindPrevious();
        else if (findEnter)
            editor.FindNext();

        const s32 matchNum = editor.GetSearchMatchCount();

        if (showCount)
        {
            ImGui::SameLine(0.0f, gap * 2.0f);
            ImGui::AlignTextToFramePadding();

            if (matchNum > 0)
                ImGui::TextDisabled(messages::common::cCountOfFormat, editor.GetSearchCurrentMatch() + 1, matchNum);
            else if (sFindBuffer[0] != '\0')
                ImGui::TextDisabled("%s", messages::sequence::cFindNoResults);
        }

        ImGui::SameLine(ImGui::GetCursorStartPos().x + button + fieldWidth + countWidth, 0.0f);

        if (BarButton(ICON_LC_ARROW_UP "##Previous", messages::sequence::cFindPrevious, matchNum > 0))
            editor.FindPrevious();

        ImGui::SameLine(0.0f, 0.0f);

        if (BarButton(ICON_LC_ARROW_DOWN "##Next", messages::sequence::cFindNext, matchNum > 0))
            editor.FindNext();

        ImGui::SameLine(0.0f, 0.0f);

        if (BarButton(ICON_LC_X "##Close", messages::sequence::cFindClose))
            editor.CloseSearch();

        if (replaceVisible)
        {
            bool replaceEnter = false;

            SearchFieldExtra replaceExtra;
            replaceExtra.hint = messages::common::cReplace;
            replaceExtra.icon = ICON_LC_CORNER_DOWN_RIGHT;
            replaceExtra.enterPressed = &replaceEnter;

            const f32 replaceButtonsWidth = ImGui::CalcTextSize(messages::common::cReplace).x + ImGui::CalcTextSize(messages::common::cAll).x + style.FramePadding.x * 4.0f + gap * 3.0f;
            const f32 replaceWidth = ImMin(fieldWidth, ImGui::GetContentRegionAvail().x - button - replaceButtonsWidth);

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + button);
            SearchField("##Replace", sReplaceBuffer, sizeof(sReplaceBuffer), replaceWidth, nullptr, false, &replaceExtra);

            ImGui::SameLine(0.0f, gap * 2.0f);

            ImGui::BeginDisabled(matchNum == 0);

            if (ImGui::Button(messages::common::cReplace) || (replaceEnter && matchNum > 0))
                editor.ReplaceCurrent(sReplaceBuffer);

            ImGui::SameLine(0.0f, gap);

            if (ImGui::Button(messages::common::cAll))
                editor.ReplaceAll(sReplaceBuffer);

            ImGui::EndDisabled();
        }
    }

    ImGui::EndChild();
}

static void DrawGoToBar(TextEditor& editor, const ImVec2& editorMin, const ImVec2& editorMax)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 em = ImGui::GetFontSize();
    bool close = false;

    if (BeginEditorBar(editorMin, editorMax, em * cGoToWidthEm, 1))
    {
        const f32 button = ImGui::GetFrameHeight();
        const f32 gap = style.ItemInnerSpacing.x;

        sead::FormatFixedSafeString<32> count(messages::sequence::cGoToLineCountFormat, editor.GetTotalLines());
        const f32 countWidth = ImGui::CalcTextSize(count.cstr()).x;
        const f32 fieldWidth = ImMax(em * 4.0f, ImGui::GetContentRegionAvail().x - button - countWidth - gap * 4.0f);

        bool enter = false;

        SearchFieldExtra extra;
        extra.hint = messages::sequence::cGoToLineHint;
        extra.icon = ICON_LC_HASH;
        extra.enterPressed = &enter;

        SearchField("##GoTo", sGoToBuffer, sizeof(sGoToBuffer), fieldWidth, nullptr, sGoToFocus, &extra);
        sGoToFocus = false;

        ImGui::SameLine(0.0f, gap * 2.0f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", count.cstr());

        ImGui::SameLine(0.0f, gap * 2.0f);

        if (BarButton(ICON_LC_X "##Close", messages::sequence::cFindClose))
            close = true;

        if (enter)
        {
            int line = 0;
            int column = 1;

            if (sscanf(sGoToBuffer, "%d:%d", &line, &column) >= 1 && line >= 1)
                editor.GoToLine(line - 1, ImMax(0, column - 1));

            close = true;
        }

        if (ImGui::IsWindowFocused() && shortcuts::Pressed(shortcuts::Action::CloseFilter))
            close = true;
    }

    ImGui::EndChild();

    if (!close)
        return;

    sGoToEditor = nullptr;
    editor.RequestFocus();
}

static void DrawLabelsPopup(TextEditor& editor, const ImVec2& editorMin, const ImVec2& editorMax)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 em = ImGui::GetFontSize();
    const f32 width = ImMin(em * cLabelsWidthEm, editorMax.x - editorMin.x);

    const ImVec4 background = uicolors::Opaque(ImGui::GetStyleColorVec4(ImGuiCol_PopupBg));

    ImGui::SetNextWindowPos(ImVec2(ImFloor((editorMin.x + editorMax.x - width) * 0.5f), editorMin.y), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(width, 0.0f));

    ImGui::PushStyleColor(ImGuiCol_PopupBg, background);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, ImFloor(em * cMenuRoundingEm));

    const bool open = ImGui::BeginPopup("##SeqLabels");

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    if (!open)
        return;

    const std::vector<TextEditor::Symbol>& symbols = editor.GetSymbols();

    bool enter = false;

    SearchFieldExtra extra;
    extra.hint = messages::sequence::cLabelFilterHint;
    extra.icon = ICON_LC_TAG;
    extra.enterPressed = &enter;

    if (SearchField("##Filter", sLabelFilter, sizeof(sLabelFilter), 0.0f, nullptr, sLabelFocus, &extra))
    {
        sLabelSelected = 0;
        sLabelScroll = true;
    }

    sLabelFocus = false;

    std::vector<s32> rows;
    for (s32 i = 0; i < static_cast<s32>(symbols.size()); i++)
    {
        if (sLabelFilter[0] == '\0' || ImGui::TextMatchesQuery(symbols[i].mName.c_str(), sLabelFilter))
            rows.push_back(i);
    }

    const s32 rowNum = static_cast<s32>(rows.size());

    if (rowNum > 0 && shortcuts::Pressed(shortcuts::Action::SelectNext))
    {
        sLabelSelected = (sLabelSelected + 1) % rowNum;
        sLabelScroll = true;
    }
    else if (rowNum > 0 && shortcuts::Pressed(shortcuts::Action::SelectPrevious))
    {
        sLabelSelected = (sLabelSelected + rowNum - 1) % rowNum;
        sLabelScroll = true;
    }

    sLabelSelected = ImClamp(sLabelSelected, 0, ImMax(0, rowNum - 1));

    s32 target = enter && rowNum > 0 ? rows[sLabelSelected] : -1;

    if (rowNum == 0)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", messages::sequence::cNoLabelMatch);
    }
    else
    {
        const f32 rowHeight = ImGui::GetTextLineHeight() + style.ItemSpacing.y;

        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::GetColorU32(ImGuiCol_Header, 0.6f));

        if (ImGui::BeginChild("##List", ImVec2(0.0f, ImMin(rowNum, cLabelsRowMax) * rowHeight), ImGuiChildFlags_None))
        {
            const f32 indent = ImFloor(em * 1.2f);
            const ImVec4 dimColor = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);

            for (s32 i = 0; i < rowNum; i++)
            {
                const TextEditor::Symbol& symbol = symbols[rows[i]];
                const sead::FormatFixedSafeString<32> lineText("%d", symbol.mLine + 1);
                const f32 lineWidth = ImGui::CalcTextSize(lineText.cstr()).x;

                ImGui::PushID(rows[i]);

                const f32 rowStart = ImGui::GetCursorPosX();
                const f32 rowWidth = ImGui::GetContentRegionAvail().x;

                if (ImGui::Selectable("##Row", i == sLabelSelected, ImGuiSelectableFlags_None, ImVec2(0.0f, ImGui::GetTextLineHeight())))
                    target = rows[i];

                if (i == sLabelSelected && sLabelScroll)
                    ImGui::SetScrollHereY(0.5f);

                ImGui::SameLine(rowStart + style.FramePadding.x + (symbol.mGlobal ? 0.0f : indent), 0.0f);

                if (symbol.mGlobal)
                    ImGui::TextUnformatted(symbol.mName.c_str());
                else
                    ImGui::TextColored(GetItemValueColor(), "%s", symbol.mName.c_str());

                ImGui::SameLine(rowStart + rowWidth - lineWidth - style.FramePadding.x, 0.0f);
                ImGui::TextColored(dimColor, "%s", lineText.cstr());

                ImGui::PopID();
            }
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    sLabelScroll = false;

    if (target >= 0)
    {
        editor.GoToLine(symbols[target].mLine);
        editor.RequestFocus();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

static void DrawEditMenu(TextEditor& editor)
{
    if (!BeginContextMenu("##SeqEditMenu"))
        return;

    const TextEditor::Coordinates position = editor.GetContextMenuPosition();
    bool used = false;

    if (editor.CanGoToDefinition(position) && ContextMenuItem(ICON_LC_CORNER_DOWN_RIGHT, messages::sequence::cMenuGoToDefinition))
    {
        editor.GoToDefinition(position);
        used = true;
    }

    ContextMenuSeparator();

    if (ContextMenuItem(ICON_LC_SCISSORS, messages::sequence::cMenuCut))
    {
        editor.Cut();
        used = true;
    }

    if (ContextMenuItem(ICON_LC_COPY, messages::sequence::cMenuCopy))
    {
        editor.Copy();
        used = true;
    }

    if (ContextMenuItem(ICON_LC_CLIPBOARD_PASTE, messages::sequence::cMenuPaste))
    {
        editor.Paste();
        used = true;
    }

    ContextMenuSeparator();

    if (ContextMenuItem(ICON_LC_MESSAGE_SQUARE_CODE, messages::sequence::cMenuToggleComment))
    {
        editor.ToggleLineComment();
        used = true;
    }

    if (ContextMenuItem(ICON_LC_TEXT_SELECT, messages::common::cSelectAll))
    {
        editor.SelectAll();
        used = true;
    }

    if (used)
        editor.RequestFocus();

    EndContextMenu();
}

static void DrawStatusLine(TextEditor& editor, f32 padY, bool& openGoTo, bool& openLabels)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 startX = ImGui::GetCursorPosX();
    const f32 width = ImGui::GetContentRegionAvail().x;
    const f32 padX = style.FramePadding.x;

    const TextEditor::Coordinates cursor = editor.GetCursorPosition();

    sead::FixedSafeString<96> position;
    position.format(messages::sequence::cStatusPositionFormat, cursor.mLine + 1, cursor.mColumn + 1);

    int selectedLines = 0;
    int selectedChars = 0;
    editor.GetSelectionSize(selectedLines, selectedChars);

    sead::FixedSafeString<96> selection;
    if (selectedLines > 1)
        selection.format(messages::sequence::cStatusSelectedLinesFormat, selectedLines, selectedChars);
    else if (selectedChars > 0)
        selection.format(messages::sequence::cStatusSelectedFormat, selectedChars);

    const f32 gap = ImGui::GetFontSize();
    const f32 positionWidth = ImGui::CalcTextSize(position.cstr()).x + padX * 2.0f;
    const f32 selectionWidth = selection.isEmpty() ? 0.0f : ImGui::CalcTextSize(selection.cstr()).x + gap;
    const f32 overwriteWidth = editor.IsOverwrite() ? ImGui::CalcTextSize(messages::sequence::cStatusOverwrite).x + gap : 0.0f;
    const f32 rightWidth = positionWidth + selectionWidth + overwriteWidth;

    ImGui::PushStyleColor(ImGuiCol_Button, uicolors::cTransparentU32);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padX, padY));
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));

    f32 leftWidth = width - rightWidth - gap;

    int errorLine = 0;
    std::string errorText;

    if (editor.GetFirstError(errorLine, errorText) && leftWidth > gap * 6.0f)
    {
        char message[512];
        snprintf(message, sizeof(message), messages::sequence::cErrorLineFormat, errorLine + 1, errorText.c_str());

        const std::string label = FitText(std::string(ICON_LC_CIRCLE_X "  ") + message,leftWidth - padX * 2.0f) + "###SeqError";

        ImGui::PushStyleColor(ImGuiCol_Text, uicolors::Marker());

        if (ImGui::Button(label.c_str()))
        {
            editor.GoToLine(errorLine);
            editor.RequestFocus();
        }

        ImGui::PopStyleColor();
        ImGui::SetItemTooltip("%s", messages::sequence::cErrorJumpTooltip);
        ImGui::SameLine(0.0f, 0.0f);

        leftWidth -= ImGui::GetItemRectSize().x + gap;
    }

    // Label of the cursor
    const std::vector<TextEditor::Symbol>& symbols = editor.GetSymbols();

    if (!symbols.empty())
    {
        const s32 global = editor.FindCursorSymbol(true);
        const s32 local = editor.FindCursorSymbol(false);

        std::string path = ICON_LC_TAG "  ";

        if (global >= 0)
            path += symbols[global].mName;

        if (global >= 0 && local >= 0)
            path += "  " ICON_LC_CHEVRON_RIGHT "  ";

        if (local >= 0)
            path += symbols[local].mName;

        if (global < 0 && local < 0)
            path += messages::common::cLabels;

        const f32 pathWidth = ImMin(ImGui::CalcTextSize(path.c_str()).x + padX * 2.0f, leftWidth);

        path = FitText(path, pathWidth - padX * 2.0f) + "###SeqLabelPath";

        if (pathWidth > gap * 6.0f)
        {
            if (ImGui::Button(path.c_str(), ImVec2(pathWidth, 0.0f)))
                openLabels = true;

            ImGui::SetItemTooltip("%s", messages::sequence::cStatusLabelsTooltip);
            ImGui::SameLine(0.0f, 0.0f);
        }
    }

    ImGui::SetCursorPosX(startX + ImMax(0.0f, width - rightWidth));

    if (editor.IsOverwrite())
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(messages::sequence::cStatusOverwrite);
        ImGui::SameLine(0.0f, gap);
    }

    if (!selection.isEmpty())
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(selection.cstr());
        ImGui::SameLine(0.0f, gap);
    }

    position.appendWithFormat("###SeqPosition");

    if (ImGui::Button(position.cstr()))
        openGoTo = true;

    ImGui::SetItemTooltip("%s", messages::sequence::cStatusPositionTooltip);

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

void DrawSeqEditor(TextEditor& editor)
{
    const SeqEditorSettings& settings = gSeqEditorSettings;
    const f32 em = ImGui::GetFontSize();

    editor.SetWhitespaceMode(settings.showWhitespace ? TextEditor::WhitespaceMode::All : TextEditor::WhitespaceMode::Selection);
    editor.SetAutoComplete(settings.suggest);
    editor.SetMinimap(settings.minimap);
    editor.SetParameterHints(settings.parameterHints);

    const f32 statusPadY = ImFloor(em * cStatusPadEm);
    const f32 statusGap = ImFloor(em * cStatusGapEm);
    const f32 statusHeight = em + statusPadY * 2.0f;

    const bool paneFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
    const ImVec2 editorMin = ImGui::GetCursorScreenPos();

    editor.Render("SeqText", ImVec2(0.0f, -(statusHeight + statusGap)));

    const ImVec2 editorMax = ImGui::GetItemRectMax();

    bool openGoTo = false;
    bool openLabels = false;

    switch (editor.ConsumeRequest())
    {
    case TextEditor::Request::GoToLine:
        openGoTo = true;
        break;
    case TextEditor::Request::Symbols:
        openLabels = true;
        break;
    case TextEditor::Request::ContextMenu:
        ImGui::OpenPopup("##SeqEditMenu");
        break;
    default:
        break;
    }

    const f32 lineY = editorMax.y + ImFloor(statusGap * 0.5f) + 0.5f;
    ImGui::GetWindowDrawList()->AddLine(ImVec2(editorMin.x, lineY), ImVec2(editorMax.x, lineY), ImGui::GetColorU32(ImGuiCol_Separator));

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + statusGap);
    DrawStatusLine(editor, statusPadY, openGoTo, openLabels);

    if (openGoTo)
    {
        if (editor.IsSearchActive())
            editor.CloseSearch();

        sGoToEditor = &editor;
        sGoToFocus = true;
        sGoToBuffer[0] = '\0';
    }

    if (openLabels && !editor.GetSymbols().empty())
    {
        const s32 global = editor.FindCursorSymbol(true);
        const s32 local = editor.FindCursorSymbol(false);

        sLabelFilter[0] = '\0';
        sLabelSelected = ImMax(0, local >= 0 ? local : global);
        sLabelFocus = true;
        sLabelScroll = true;

        ImGui::OpenPopup("##SeqLabels");
    }

    if (editor.IsSearchActive() && sGoToEditor == &editor)
        sGoToEditor = nullptr;

    sBarInsetRight = editor.GetMinimapWidth();

    if (editor.IsSearchActive())
        DrawFindBar(editor, editorMin, editorMax, paneFocused);
    else if (sGoToEditor == &editor)
        DrawGoToBar(editor, editorMin, editorMax);

    DrawEditMenu(editor);
    DrawLabelsPopup(editor, editorMin, editorMax);
}
