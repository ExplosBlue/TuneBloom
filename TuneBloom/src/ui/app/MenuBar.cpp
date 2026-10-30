#include <ui/app/MenuBar.h>

#include <ui/app/About.h>
#include <ui/backup/Backup.h>
#include <ui/browser/FilesPanel.h>
#include <ui/settings/Config.h>
#include <ui/widgets/ContextMenu.h>
#include <ui/file/DocumentMgr.h>
#include <ui/file/File.h>
#include <ui/file/FileFlow.h>
#include <ui/file/NewFileDialog.h>
#include <ui/app/FileTransferMenu.h>
#include <ui/items/ItemColumns.h>
#include <ui/items/ItemList.h>
#include <messages/Messages.h>
#include <ui/app/Notifications.h>
#include <ui/player/OutputMeter.h>
#include <ui/player/PlayerFrame.h>
#include <ui/settings/Preferences.h>
#include <ui/file/RecentFiles.h>
#include <ui/items/Selection.h>
#include <ui/app/Shortcuts.h>
#include <ui/app/ShortcutsDialog.h>
#include <ui/app/TitleBar.h>
#include <ui/file/SystemShell.h>
#include <ui/settings/Theme.h>
#include <ui/settings/UiColors.h>
#include <ui/file/UnsavedChanges.h>
#include <ui/widgets/WindowAnimation.h>
#include <ui/app/Workspace.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_custom.h>
#include <imgui/imgui_internal.h>

#include <optional>

static constexpr f32 cMenuBarPadYEm      = 0.2f;
static constexpr f32 cTitleBarPadYEm     = 0.35f;
static constexpr f32 cMenuBarPillInsetEm = 0.25f;
static constexpr f32 cMenuBarHoverAlpha  = 0.6f;
static constexpr f32 cMenuBarTextRaiseEm = 0.05f;

static f32 sMenuBarPadY = 0.0f;

ImDrawList* BeginMenuHighlight()
{
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->ChannelsSplit(2);
    drawList->ChannelsSetCurrent(1);

    ImGui::PushStyleColor(ImGuiCol_Header, uicolors::cTransparent);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, uicolors::cTransparent);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, uicolors::cTransparent);

    return drawList;
}

void EndMenuHighlight(ImDrawList* drawList, bool open, f32 hoverAlpha, const ImVec2* rangeY)
{
    ImGui::PopStyleColor(3);

    const bool focused = ImGui::IsItemFocused() && !GImGui->NavDisableHighlight;
    const bool hovered = ImGui::IsItemHovered();
    const bool held = hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left);

    drawList->ChannelsSetCurrent(0);

    const f32 hoverRatio = ImGui::GetHoverRatio(ImGui::GetItemID() + 1, open || hovered || focused);

    if (hoverRatio > 0.0f)
    {
        ImVec2 min = ImGui::GetItemRectMin();
        ImVec2 max = ImGui::GetItemRectMax();

        if (rangeY)
        {
            min.y = rangeY->x;
            max.y = rangeY->y;
        }

        const ImU32 color = ImGui::GetColorU32(held ? ImGuiCol_HeaderActive : ImGuiCol_Header, ((open || held) ? 1.0f : hoverAlpha) * hoverRatio);
        drawList->AddRectFilled(min, max, color, ImGui::GetStyle().FrameRounding);
    }

    drawList->ChannelsMerge();
}

static ImVec2 CalcMenuBarPillRangeY()
{
    const f32 inset = ImFloor(GImGui->FontBaseSize * cMenuBarPillInsetEm);
    const f32 top = ImGui::GetWindowPos().y;

    return ImVec2(top + inset, top + ImGui::GetWindowSize().y - 1.0f - inset);
}

f32 CalcMenuBarIconButtonWidth(const char* icon)
{
    return ImGui::CalcTextSize(icon).x + ImGui::GetStyle().ItemSpacing.x * 2.0f;
}

bool MenuBarIconButton(const char* id, const char* icon, f32 right, f32 top, f32 bottom, bool isOpen, const ImVec4* iconColor, ImVec2* iconPos)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 iconSize = ImGui::CalcTextSize(icon);
    const ImVec2 min(right - CalcMenuBarIconButtonWidth(icon), top);
    const ImVec2 max(right, bottom);

    ImGui::SetCursorScreenPos(min);
    const bool pressed = ImGui::InvisibleButton(id, max - min);

    const ImGuiID itemId = ImGui::GetItemID();
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();

    ImVec4 fill = style.Colors[held ? ImGuiCol_HeaderActive : ImGuiCol_HeaderHovered];
    ImVec4 idle = fill;
    idle.w = 0.0f;

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    if (isOpen)
        drawList->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_Header), style.FrameRounding);
    else
        drawList->AddRectFilled(min, max, ImGui::GetHoverColorU32(itemId, hovered, ImGui::GetColorU32(idle), ImGui::GetColorU32(fill)), style.FrameRounding);

    const ImVec4 barColor = ImLerp(GetItemValueColor(), style.Colors[ImGuiCol_Text], ImGui::GetHoverRatio(itemId + 2, hovered || held || isOpen));
    const ImVec2 pos(min.x + style.ItemSpacing.x, ImFloor((top + bottom - iconSize.y) * 0.5f));

    drawList->AddText(pos, ImGui::GetColorU32(iconColor ? *iconColor : barColor), icon);

    if (iconPos)
        *iconPos = pos;

    return pressed;
}

static void PushMenuBarTextColor(const char* label)
{
    const ImGuiID id = ImGui::GetID(label);
    const bool lit = GImGui->HoveredIdPreviousFrame == id || ImGui::IsPopupOpen(label);

    ImGui::PushStyleColor(ImGuiCol_Text, ImLerp(GetItemValueColor(), ImGui::GetStyleColorVec4(ImGuiCol_Text), ImGui::GetHoverRatio(id + 2, lit)));
}

static bool BeginBarMenu(const char* label)
{
    const ImVec2 barPadding = ImGui::GetStyle().FramePadding;
    const ImVec2 rangeY = CalcMenuBarPillRangeY();

    PushMenuBarTextColor(label);
    ImDrawList* drawList = BeginMenuHighlight();
    const bool open = ImGui::BeginMenu(label);
    EndMenuHighlight(drawList, open, cMenuBarHoverAlpha, &rangeY);
    ImGui::PopStyleColor();

    if (open)
    {
        AnimateWindow(WindowAnimation::Menu);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(barPadding.x, barPadding.y - sMenuBarPadY));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, ImFloor(ImGui::GetFontSize() * cMenuItemGapEm)));
    }

    return open;
}

void SetMenuBarTooltip(const char* tooltip)
{
    ImGui::SetItemTooltip("%s", tooltip);
}

static void EndBarMenu()
{
    ImGui::PopStyleVar(2);
    ImGui::EndMenu();
}

static constexpr f32 cHoverMenuDelay      = 0.1f;
static constexpr f32 cHoverMenuCloseDelay = 0.2f;

static ImGuiID sHoverMenuId = 0;
static ImGuiID sHoverMenuClickedId = 0;
static ImRect sHoverMenuItemRect;
static f64 sHoverMenuLeaveTime = -1.0;

static void CloseHoverMenuOnLeave()
{
    if (sHoverMenuId == 0)
        return;

    const ImGuiContext& g = *GImGui;

    for (s32 level = 0; level < g.OpenPopupStack.Size; level++)
    {
        const ImGuiPopupData& popup = g.OpenPopupStack[level];

        if (popup.PopupId != sHoverMenuId)
            continue;

        const ImVec2 mouse = ImGui::GetMousePos();

        if (!popup.Window || sHoverMenuItemRect.Contains(mouse) || popup.Window->Rect().Contains(mouse))
        {
            sHoverMenuLeaveTime = -1.0;
            return;
        }

        if (sHoverMenuLeaveTime < 0.0)
            sHoverMenuLeaveTime = ImGui::GetTime();

        const bool onBar = ImGui::GetCurrentWindow()->Rect().Contains(mouse);

        if (!onBar && ImGui::GetTime() - sHoverMenuLeaveTime < cHoverMenuCloseDelay)
            return;

        ImGui::ClosePopupToLevel(level, true);
        break;
    }

    sHoverMenuId = 0;
    sHoverMenuLeaveTime = -1.0;
}

static bool BeginHoverBarMenu(const char* label, bool* clicked)
{
    ImGuiContext& g = *GImGui;
    const ImGuiID id = ImGui::GetID(label);
    const f32 barTop = ImGui::GetWindowPos().y;
    const f32 barBottom = barTop + ImGui::GetWindowSize().y;
    const bool open = BeginBarMenu(label);

    sHoverMenuItemRect = ImRect(ImVec2(ImGui::GetItemRectMin().x, barTop), ImVec2(ImGui::GetItemRectMax().x, barBottom));

    const bool hovered = g.HoveredId == id;
    *clicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);

    if (!hovered)
        sHoverMenuClickedId = 0;

    if (*clicked)
    {
        sHoverMenuClickedId = id;
        sHoverMenuId = 0;

        if (open)
            ImGui::CloseCurrentPopup();
    }
    else if (!open && hovered && sHoverMenuClickedId != id && g.OpenPopupStack.Size == 0 && g.HoveredIdTimer >= cHoverMenuDelay)
    {
        ImGui::OpenPopup(id);
        sHoverMenuId = id;
        sHoverMenuLeaveTime = -1.0;
    }

    return open;
}

bool MenuBarItem(const char* label, const char* shortcut, bool selected, bool enabled)
{
    const f32 posY = ImGui::GetCursorScreenPos().y;

    ImDrawList* drawList = BeginMenuHighlight();
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, uicolors::cTransparent);
    const bool pressed = ImGui::MenuItem(label, shortcut, false, enabled);
    ImGui::PopStyleColor();
    EndMenuHighlight(drawList, false);

    if (selected)
        DrawMenuCheck(enabled);

    if (shortcut && shortcut[0] != '\0')
    {
        const f32 right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;

        ImVec4 color = GetItemValueColor();

        if (!enabled && !(GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled))
            color.w *= ImGui::GetStyle().DisabledAlpha;

        drawList->AddText(ImVec2(right - ImGui::CalcTextSize(shortcut).x, posY), ImGui::GetColorU32(color), shortcut);
    }

    return pressed;
}

bool BeginSubMenu(const char* label)
{
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const f32 right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    const bool enabled = !(GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled);

    ImDrawList* drawList = BeginMenuHighlight();
    ImGui::PushStyleColor(ImGuiCol_Text, uicolors::cTransparent);
    const bool open = ImGui::BeginMenu(label, enabled);
    ImGui::PopStyleColor();
    EndMenuHighlight(drawList, open);

    drawList->AddText(pos, ImGui::GetColorU32(ImGuiCol_Text), label);
    drawList->AddText(ImVec2(right - ImGui::CalcTextSize(ICON_LC_CHEVRON_RIGHT).x, pos.y), ImGui::GetColorU32(GetItemValueColor()), ICON_LC_CHEVRON_RIGHT);

    if (open)
        AnimateWindow(WindowAnimation::Menu);

    return open;
}

bool MenuBarIconItem(const char* icon, const char* label, const char* shortcut, bool enabled, const char* disabledReason)
{
    const bool pressed = MenuBarItem(sead::FormatFixedSafeString<96>("%s %s", icon, label).cstr(), shortcut, false, enabled);

    if (!enabled && disabledReason)
        ImGui::SetItemTooltip("%s", disabledReason);

    return pressed;
}

static s32 sMenuFindFrame = 0;
static std::optional<bool> sMenuThemeRequest;

static void DrawBackupsMenu()
{
    if (!BeginSubMenu(sead::FormatFixedSafeString<64>(ICON_LC_HISTORY " %s", messages::common::cBackups).cstr()))
        return;

    if (MenuBarIconItem(ICON_LC_DATABASE_BACKUP, messages::mainmenu::cCreateBackup) && backup::CreateBackup())
        backup::OpenRestoreDialog();

    if (MenuBarIconItem(ICON_LC_ARCHIVE_RESTORE, messages::backup::cMenuRestore))
        backup::OpenRestoreDialog();

    const std::string folder = backup::GetFolder();

    if (MenuBarIconItem(ICON_LC_FOLDER_CLOCK, messages::mainmenu::cShowBackupFolder, nullptr, !folder.empty()))
        OpenFolder(folder);

    ImGui::EndMenu();
}

static void DrawEditMenu()
{
    const bool documentOpen = IsDocumentOpen();
    const bool hasItem = documentOpen && sSelectedItem != nullptr;
    const Item* activeItem = documentOpen ? GetActiveSelection() : nullptr;
    const bool canDuplicate = activeItem && CanDuplicateSelection(activeItem);

    if (MenuBarIconItem(ICON_LC_TEXT_CURSOR_INPUT, messages::common::cRename, shortcuts::Label(shortcuts::Action::RenameSelection), hasItem && CanRenameItem(sSelectedItem)))
        shortcuts::Trigger(shortcuts::Action::RenameSelection);

    if (MenuBarIconItem(ICON_LC_COPY, messages::common::cDuplicate, shortcuts::Label(shortcuts::Action::DuplicateSelection), canDuplicate))
        shortcuts::Trigger(shortcuts::Action::DuplicateSelection);

    if (MenuBarIconItem(ICON_LC_LIST_CHECKS, messages::common::cSelectAll, shortcuts::Label(shortcuts::Action::SelectAll), documentOpen))
        shortcuts::Trigger(shortcuts::Action::SelectAll);

    ImGui::Separator();

    if (MenuBarIconItem(ICON_LC_SEARCH, messages::common::cFind, shortcuts::Label(shortcuts::Action::FocusFilter), documentOpen))
    {
        sMenuFindFrame = ImGui::GetFrameCount() + 1;
        shortcuts::Trigger(shortcuts::Action::FocusFilter);
    }

    ImGui::Separator();

    if (MenuBarIconItem(ICON_LC_TRASH_2, messages::common::cDelete, nullptr, hasItem))
        shortcuts::Trigger(shortcuts::Action::DeleteSelection);
}

static void DrawViewMenu()
{
    const bool meterVisible = IsOutputMeterWindowVisible();

    if (MenuBarItem(messages::mainmenu::cOutputMeter, nullptr, meterVisible))
        SetOutputMeterWindowVisible(!meterVisible);

    if (MenuBarItem(messages::mainmenu::cSystemWindow, nullptr, IsSystemWindowShown()))
        SetSystemWindowShown(!IsSystemWindowShown());

    ImGui::Separator();

    if (BeginSubMenu(messages::common::cTheme))
    {
        if (MenuBarItem(messages::mainmenu::cThemeDark, nullptr, IsDarkTheme()))
            sMenuThemeRequest = true;

        if (MenuBarItem(messages::mainmenu::cThemeLight, nullptr, !IsDarkTheme()))
            sMenuThemeRequest = false;

        ImGui::EndMenu();
    }

    ImGui::Separator();

    if (MenuBarItem(messages::mainmenu::cResetLayout))
        RequestResetLayout();
}

static void DrawHelpMenu()
{
    if (MenuBarIconItem(ICON_LC_KEYBOARD, messages::mainmenu::cShortcuts))
        OpenShortcutsDialog();

    ImGui::Separator();

    if (MenuBarIconItem(ICON_LC_FOLDER_GIT_2, messages::mainmenu::cRepository))
        OpenURL(cRepositoryUrl);

    if (MenuBarIconItem(ICON_LC_BUG, messages::mainmenu::cReportIssue))
        OpenURL(cIssuesUrl);

    if (MenuBarIconItem(ICON_LC_MESSAGE_CIRCLE, messages::mainmenu::cDiscord))
        OpenURL(cDiscordUrl);

    ImGui::Separator();

    if (MenuBarIconItem(ICON_LC_INFO, messages::mainmenu::cAbout))
        RequestAbout();
}

static void DrawRecentFilesMenu()
{
    if (!BeginSubMenu(sead::FormatFixedSafeString<64>(ICON_LC_CLOCK " %s", messages::mainmenu::cOpenRecent).cstr()))
        return;

    std::string path;

    if (DrawRecentFilesList(&path))
        RequestOpenFile(path.c_str());

    ImGui::EndMenu();
}

static void FocusInfoWindowForFind()
{
    if (sMenuFindFrame == 0 || ImGui::GetFrameCount() < sMenuFindFrame)
        return;

    sMenuFindFrame = 0;

    const ImGuiWindow* infoWindow = ImGui::FindWindowByName("###InfoWindow");
    const ImGuiWindow* navWindow = GImGui->NavWindow;

    if (infoWindow && (!navWindow || navWindow->RootWindow != infoWindow))
        ImGui::SetWindowFocus("###InfoWindow");
}

static bool IsSlimMenuBar()
{
    return IsPlayerFrameShown() || IsFileEditorShown();
}

static void DrawFileMenu()
{
    const bool documentOpen = IsDocumentOpen();
    const bool archiveOpen = IsSoundArchiveOpen();
    const bool isSlim = IsSlimMenuBar();
    const bool isPlayer = IsPlayerFrameShown();

    if (!isPlayer && MenuBarItem(sead::FormatFixedSafeString<64>(ICON_LC_FILE " %s", messages::mainmenu::cNew).cstr(), shortcuts::Label(shortcuts::Action::NewProject)))
        OpenNewFileDialog();

    if (MenuBarItem(sead::FormatFixedSafeString<64>(ICON_LC_FOLDER_OPEN " %s", messages::common::cOpen).cstr(), shortcuts::Label(shortcuts::Action::OpenProject)))
        RequestOpenFile();

    if (!isSlim && MenuBarIconItem(ICON_LC_FOLDER_PLUS, messages::common::cOpenFolder))
        RequestFilesPanelFolderDialog();

    DrawRecentFilesMenu();

    ImGui::Separator();
    ImGui::BeginDisabled(!documentOpen || ActiveDocument()->isPlayerOnly());

    if (MenuBarItem(sead::FormatFixedSafeString<64>(ICON_LC_SAVE " %s", messages::common::cSave).cstr(), shortcuts::Label(shortcuts::Action::SaveProject)))
        RequestSaveFile();

    if (MenuBarItem(sead::FormatFixedSafeString<64>(ICON_LC_SAVE_ALL " %s", messages::mainmenu::cSaveAs).cstr(), shortcuts::Label(shortcuts::Action::SaveProjectAs)))
        RequestSaveDocumentAs(ActiveDocument());

    if (!isSlim && MenuBarIconItem(ICON_LC_FILE_STACK, messages::mainmenu::cSaveAll, shortcuts::Label(shortcuts::Action::SaveAllProjects), HasUnsavedDocuments()))
        RequestSaveAll();

    if (!isPlayer)
        DrawBackupsMenu();

    ImGui::EndDisabled();
    ImGui::Separator();

    if (!isSlim)
    {
        ImGui::BeginDisabled(!archiveOpen);

        DrawImportMenu();
        DrawExportMenu();

        ImGui::EndDisabled();
        ImGui::Separator();
    }

    const bool hasPath = documentOpen && !GetDocumentPath().isEmpty();

    if (MenuBarIconItem(ICON_LC_FOLDER_SEARCH, messages::recent::cShowInFileManager, nullptr, hasPath))
        ShowInFileManager(GetDocumentPath().cstr());

    if (MenuBarIconItem(ICON_LC_COPY, messages::mainmenu::cCopyPath, nullptr, hasPath))
        ImGui::SetClipboardText(GetDocumentPath().cstr());

    ImGui::Separator();

    if (isSlim && MenuBarIconItem(ICON_LC_LAYOUT_PANEL_LEFT, messages::mainmenu::cOpenFullEditor))
        RequestFullEditor();

    if (!isPlayer && MenuBarIconItem(ICON_LC_FILE_OUTPUT, messages::common::cClose, shortcuts::Label(shortcuts::Action::CloseProject), documentOpen))
        RequestCloseFile();

    if (MenuBarItem(sead::FormatFixedSafeString<64>(ICON_LC_DOOR_OPEN " %s", messages::mainmenu::cExit).cstr()))
    {
        TryExit();
    }
}

static f32 DrawSaveStatusChip(f32 chipRight, f32 textY)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const sead::FormatFixedSafeString<64> statusLabel(HasUnsavedChanges() ? ICON_LC_FILE " %s" : ICON_LC_CHECK_CHECK " %s", HasUnsavedChanges() ? messages::mainmenu::cUnsaved : messages::mainmenu::cSaved);
    const char* statusText = statusLabel.cstr();
    const ImVec4 statusColor = HasUnsavedChanges() ? uicolors::Warning() : style.Colors[ImGuiCol_TextDisabled];

    const ImVec2 rangeY = CalcMenuBarPillRangeY();
    const f32 chipLeft = chipRight - ImGui::CalcTextSize(statusText).x - style.ItemSpacing.x * 2.0f;

    ImGuiCol chipColor = ImGuiCol_FrameBg;

    if (HasUnsavedChanges())
    {
        ImGui::SetCursorScreenPos(ImVec2(chipLeft, rangeY.x));

        if (ImGui::InvisibleButton("##SaveStatus", ImVec2(chipRight - chipLeft, rangeY.y - rangeY.x)))
            OpenUnsavedChangesDialog();

        if (ImGui::IsItemHovered())
        {
            chipColor = ImGui::IsItemActive() ? ImGuiCol_HeaderActive : ImGuiCol_HeaderHovered;
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }

        SetMenuBarTooltip(messages::mainmenu::cShowUnsaved);
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(ImVec2(chipLeft, rangeY.x), ImVec2(chipRight, rangeY.y), ImGui::GetColorU32(chipColor), style.FrameRounding);
    drawList->AddText(ImVec2(chipLeft + style.ItemSpacing.x, textY), ImGui::GetColorU32(statusColor), statusText);

    return chipLeft;
}

static f32 DrawMenuBarTools(f32 right, f32 top, f32 bottom, f32 textY)
{
    if (IsPlayerFrameShown())
        return DrawPlayerFrameTools(right, top, bottom);

    const f32 bellLeft = right - notify::DrawBell(right, top, bottom);

    if (!IsDocumentOpen())
        return bellLeft;

    return DrawSaveStatusChip(bellLeft - ImGui::GetStyle().ItemInnerSpacing.x, textY);
}

static void DrawBarMenu(const char* label, void (*drawMenu)())
{
    if (!BeginBarMenu(label))
        return;

    drawMenu();
    EndBarMenu();
}

static void DrawBarMenus()
{
    CloseHoverMenuOnLeave();

    DrawBarMenu(messages::common::cFile, &DrawFileMenu);

    if (!IsSlimMenuBar())
    {
        DrawBarMenu(messages::mainmenu::cEdit, &DrawEditMenu);
        DrawBarMenu(messages::mainmenu::cView, &DrawViewMenu);
    }

    bool preferencesClicked = false;

    if (BeginHoverBarMenu(messages::mainmenu::cPreferences, &preferencesClicked))
    {
        DrawPreferencesMenu();
        EndBarMenu();
    }

    if (preferencesClicked)
        OpenPreferencesWindow(PreferencesPage::File);

    DrawBarMenu(messages::common::cHelp, &DrawHelpMenu);
}

static void DrawMenuBarItems(f32 em)
{
    const ImVec2 barMin = ImGui::GetWindowPos();
    const ImVec2 barMax = barMin + ImGui::GetWindowSize();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(barMin.x, barMax.y - 1.0f), ImVec2(barMax.x, barMax.y - 1.0f), ImGui::GetColorU32(ImGuiCol_Border));

    const f32 textOffsetY = -ImFloor(em * cMenuBarTextRaiseEm);
    ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(ImFloor(ImGui::GetStyle().ItemSpacing.x * 0.5f), textOffsetY));

    const bool hasTitleBar = HasCustomTitleBar();
    const f32 lineBottom = barMax.y - 1.0f;

    if (hasTitleBar)
        ImGui::SetCursorScreenPos(ImVec2(DrawTitleBarIcon(barMin.x, barMin.y, lineBottom), ImGui::GetCursorScreenPos().y));

    const f32 menuLeft = ImGui::GetCursorScreenPos().x;
    DrawBarMenus();
    const f32 menuRight = ImGui::GetCursorScreenPos().x;

    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 pillRangeY = CalcMenuBarPillRangeY();
    const f32 edge = hasTitleBar ? DrawWindowButtons(barMax.x, barMin.y, lineBottom) : barMax.x;
    const f32 toolsLeft = DrawMenuBarTools(edge - style.WindowPadding.x, pillRangeY.x, pillRangeY.y, barMin.y + style.FramePadding.y + textOffsetY);

    if (hasTitleBar)
        FinishTitleBar(menuLeft, menuRight, toolsLeft, barMax.y);
}

static void ApplyMenuThemeRequest()
{
    if (!sMenuThemeRequest)
        return;

    const bool dark = *sMenuThemeRequest;
    sMenuThemeRequest.reset();

    if (dark != IsDarkTheme())
    {
        SetDarkTheme(dark);
        ApplyTheme();
        SaveThemeConfig();
    }
}

void DrawMenuBar()
{
    FocusInfoWindowForFind();

    const f32 em = ImGui::GetFontSize();
    const ImVec2 framePadding = ImGui::GetStyle().FramePadding;

    const ImVec4 hoverColor = ImGui::GetStyleColorVec4(ImGuiCol_Header);
    const ImVec4 heldColor = ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive);
    const ImVec4 popupColor = uicolors::Opaque(ImGui::GetStyleColorVec4(ImGuiCol_PopupBg));

    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, hoverColor);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, heldColor);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, popupColor);

    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, ImFloor(em * cMenuRoundingEm));
    sMenuBarPadY = ImFloor(em * (HasCustomTitleBar() ? cTitleBarPadYEm : cMenuBarPadYEm));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(framePadding.x, framePadding.y + sMenuBarPadY));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const bool barOpen = ImGui::BeginMainMenuBar();
    ImGui::PopStyleVar();

    if (barOpen)
    {
        DrawMenuBarItems(em);
        ImGui::EndMainMenuBar();
    }

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);

    ApplyMenuThemeRequest();
}
