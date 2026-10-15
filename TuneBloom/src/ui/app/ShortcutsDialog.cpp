#include <ui/app/ShortcutsDialog.h>

#include <ui/widgets/Dialogs.h>
#include <ui/items/ItemColumns.h>
#include <ui/widgets/ListFilter.h>
#include <ui/Messages.h>
#include <ui/widgets/Properties.h>
#include <ui/app/Shortcuts.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_internal.h>

#include <cstdio>
#include <cstring>

static constexpr f32 cColumnWidthEm = 19.0f;
static constexpr f32 cColumnGapEm   = 0.8f;
static constexpr f32 cRowGapEm      = 0.1f;
static constexpr f32 cKeyPadXEm     = 0.45f;
static constexpr f32 cKeyPadYEm     = 0.15f;
static constexpr f32 cKeyGapEm      = 0.3f;
static constexpr f32 cKeyMinWidthEm = 1.5f;
static constexpr f32 cFooterGapEm   = 0.6f;

static constexpr s32 cKeyMax = 4;

static const char* cPopupName = "###KeyboardShortcuts";

namespace
{

using Action = shortcuts::Action;
using Modifier = shortcuts::Modifier;

struct ActionRow
{
    const char* name;
    Action action;
};

struct MouseRow
{
    const char* name;
    Modifier modifier;
    const char* input;
};

struct KeyText
{
    const char* key;
    const char* text;
};

const ActionRow cArchiveRows[] = {
    { messages::common::cNewArchive,      Action::NewProject },
    { messages::common::cOpenArchive,     Action::OpenProject },
    { messages::common::cSave,            Action::SaveProject },
    { messages::mainmenu::cSaveAs,          Action::SaveProjectAs },
    { messages::keys::cCloseFileWindow, Action::CloseFileWindow },
};

const ActionRow cListRows[] = {
    { messages::common::cSelectAll,     Action::SelectAll },
    { messages::keys::cPreviousItem,  Action::SelectPrevious },
    { messages::keys::cNextItem,      Action::SelectNext },
    { messages::common::cPageUp,        Action::SelectPageUp },
    { messages::common::cPageDown,      Action::SelectPageDown },
    { messages::keys::cFirstItem,     Action::SelectFirst },
    { messages::keys::cLastItem,      Action::SelectLast },
    { messages::keys::cShowSelection, Action::ScrollToSelection },
    { messages::common::cRename,        Action::RenameSelection },
    { messages::common::cDuplicate,     Action::DuplicateSelection },
    { messages::common::cDelete,        Action::DeleteSelection },
};

const ActionRow cPlaybackRows[] = {
    { messages::keys::cPlayPause, Action::PlayPause },
};

const ActionRow cSearchRows[] = {
    { messages::common::cFind,           Action::FocusFilter },
    { messages::keys::cClearSearch,    Action::CloseFilter },
    { messages::keys::cMatchCase,      Action::ToggleMatchCase },
    { messages::keys::cMatchWholeWord, Action::ToggleWholeWord },
};

const ActionRow cPianoRows[] = {
    { messages::keys::cZoomIn,      Action::ZoomIn },
    { messages::keys::cZoomOut,     Action::ZoomOut },
    { messages::keys::cOctaveDown,  Action::OctaveDown },
    { messages::keys::cOctaveUp,    Action::OctaveUp },
    { messages::keys::cOctaveReset, Action::OctaveReset },
};

const MouseRow cMouseRows[] = {
    { messages::keys::cAddToSelection, Modifier::ToggleSelection, messages::keys::cKeyClick },
    { messages::keys::cSelectRange,    Modifier::ExtendSelection, messages::keys::cKeyClick },
    { messages::keys::cZoomHorizontal, Modifier::ZoomHorizontal,  messages::keys::cKeyWheel },
    { messages::keys::cZoomVertical,   Modifier::ZoomVertical,    messages::keys::cKeyWheel },
    { messages::keys::cPanHorizontal,  Modifier::PanHorizontal,   messages::keys::cKeyWheel },
    { messages::keys::cFineAdjust,     Modifier::FineAdjust,      messages::keys::cKeyDrag },
};

const KeyText cKeyTexts[] = {
    { "UpArrow",    ICON_LC_ARROW_UP },
    { "DownArrow",  ICON_LC_ARROW_DOWN },
    { "LeftArrow",  ICON_LC_ARROW_LEFT },
    { "RightArrow", ICON_LC_ARROW_RIGHT },
    { "Equal",      "=" },
    { "Minus",      "-" },
    { "Escape",     messages::keys::cKeyEscape },
    { "PageUp",     messages::common::cPageUp },
    { "PageDown",   messages::common::cPageDown },
};

}

static bool sWantsOpen = false;
static f32 sColumnHeights[2] = {};

static const char* GetKeyText(const char* key)
{
    for (const KeyText& keyText : cKeyTexts)
    {
        if (std::strcmp(keyText.key, key) == 0)
            return keyText.text;
    }

    return key;
}

static void DrawShortcutRow(const char* name, const char* const* keys, s32 keyNum)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 em = ImGui::GetFontSize();

    const f32 padX = ImFloor(em * cKeyPadXEm);
    const f32 padY = ImFloor(em * cKeyPadYEm);
    const f32 gap = ImFloor(em * cKeyGapEm);
    const f32 minWidth = ImFloor(em * cKeyMinWidthEm);
    const f32 height = em + padY * 2.0f;

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImFloor(em * cRowGapEm));

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const f32 width = ImGui::GetContentRegionAvail().x;

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    f32 keyWidths[cKeyMax];
    f32 keysWidth = 0.0f;

    for (s32 i = 0; i < keyNum; i++)
    {
        keyWidths[i] = ImMax(ImGui::CalcTextSize(keys[i]).x + padX * 2.0f, minWidth);
        keysWidth += keyWidths[i] + (i > 0 ? gap : 0.0f);
    }

    drawList->AddText(ImVec2(pos.x, pos.y + padY), ImGui::GetColorU32(ImGuiCol_Text), name);

    f32 keyX = pos.x + width - keysWidth;

    for (s32 i = 0; i < keyNum; i++)
    {
        const ImVec2 min(keyX, pos.y);
        const ImVec2 max(keyX + keyWidths[i], pos.y + height);
        const f32 textWidth = ImGui::CalcTextSize(keys[i]).x;

        drawList->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_FrameBg), style.FrameRounding);
        drawList->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_Border), style.FrameRounding);
        drawList->AddText(ImVec2(ImFloor(keyX + (keyWidths[i] - textWidth) * 0.5f), pos.y + padY), ImGui::GetColorU32(GetItemValueColor()), keys[i]);

        keyX += keyWidths[i] + gap;
    }

    ImGui::Dummy(ImVec2(width, height));
}

static void DrawActionRow(const ActionRow& row)
{
    char chord[64];
    std::snprintf(chord, sizeof(chord), "%s", shortcuts::Label(row.action));

    const char* keys[cKeyMax];
    s32 keyNum = 0;

    for (char* key = std::strtok(chord, "+"); key && keyNum < cKeyMax; key = std::strtok(nullptr, "+"))
        keys[keyNum++] = GetKeyText(key);

    DrawShortcutRow(row.name, keys, keyNum);
}

static void EndShortcutColumn(s32 column, f32 top)
{
    const f32 height = ImGui::GetCursorScreenPos().y - top;
    const f32 lastHeight = sColumnHeights[column];
    const f32 extra = ImMax(sColumnHeights[0], sColumnHeights[1]) - lastHeight - ImGui::GetStyle().ItemSpacing.y;

    sColumnHeights[column] = height;

    if (lastHeight > 0.0f && extra > 0.0f)
        ImGui::Dummy(ImVec2(0.0f, extra));

    EndGroupBox();
}

template <size_t N>
static bool BeginActionBox(const char* title, const ActionRow (&rows)[N])
{
    if (!BeginGroupBox(title))
        return false;

    for (const ActionRow& row : rows)
        DrawActionRow(row);

    return true;
}

static bool BeginMouseBox()
{
    if (!BeginGroupBox(messages::keys::cGroupMouse))
        return false;

    for (const MouseRow& row : cMouseRows)
    {
        const char* keys[] = { shortcuts::Label(row.modifier), row.input };

        DrawShortcutRow(row.name, keys, IM_ARRAYSIZE(keys));
    }

    return true;
}

void OpenShortcutsDialog()
{
    sWantsOpen = true;
}

void DrawShortcutsDialog()
{
    if (sWantsOpen)
    {
        sWantsOpen = false;
        ImGui::OpenPopup(cPopupName);
    }

    const f32 em = ImGui::GetFontSize();

    CenterNextDialog();

    if (!BeginDialog(sead::FormatFixedSafeString<96>(ICON_LC_KEYBOARD " %s%s", messages::mainmenu::cShortcuts, cPopupName).cstr(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ImFloor(em * cColumnGapEm * 0.5f), 0.0f));

    if (ImGui::BeginTable("##ShortcutColumns", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoPadOuterX))
    {
        ImGui::TableSetupColumn("##Left", ImGuiTableColumnFlags_WidthFixed, ImFloor(em * cColumnWidthEm));
        ImGui::TableSetupColumn("##Right", ImGuiTableColumnFlags_WidthFixed, ImFloor(em * cColumnWidthEm));

        ImGui::TableNextColumn();

        const f32 top = ImGui::GetCursorScreenPos().y;

        if (BeginActionBox(messages::common::cArchive, cArchiveRows))
            EndGroupBox();

        if (BeginActionBox(messages::keys::cGroupSearch, cSearchRows))
            EndGroupBox();

        if (BeginActionBox(messages::common::cPlayback, cPlaybackRows))
            EndGroupBox();

        if (BeginMouseBox())
            EndShortcutColumn(0, top);

        ImGui::TableNextColumn();

        if (BeginActionBox(messages::keys::cGroupLists, cListRows))
            EndGroupBox();

        if (BeginActionBox(messages::keys::cGroupPiano, cPianoRows))
            EndShortcutColumn(1, top);

        ImGui::EndTable();
    }

    ImGui::PopStyleVar();

    ImGui::Dummy(ImVec2(0.0f, ImFloor(em * cFooterGapEm) - ImGui::GetStyle().ItemSpacing.y));

    BeginDialogFooter({messages::common::cClose}, false);

    if (DialogButton(messages::common::cClose) || shortcuts::Pressed(Action::CloseDialog))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}
