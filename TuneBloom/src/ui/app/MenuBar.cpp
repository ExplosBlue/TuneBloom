#include <ui/app/MenuBar.h>

#include <core/MenuTree.h>

#include <ui/app/About.h>
#include <ui/backup/Backup.h>
#include <ui/browser/FilesPanel.h>
#include <ui/export/Export.h>
#include <ui/settings/Config.h>
#include <ui/widgets/ContextMenu.h>
#include <ui/file/DocumentMgr.h>
#include <ui/file/DocumentTabs.h>
#include <ui/file/File.h>
#include <ui/file/FileFlow.h>
#include <ui/file/NewFileDialog.h>
#include <ui/file/PathText.h>
#include <ui/app/FileTransferMenu.h>
#include <ui/app/NativeMenuBar.h>
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

enum class MenuMode
{
    Draw,
    Record,
    Activate,
};

static constexpr s32 cMenuDepthMax    = 8;
static constexpr s32 cMenuDisabledMax = 32;

struct MenuVisit
{
    MenuMode mode = MenuMode::Draw;
    MenuTree* tree = nullptr;
    MenuRole nextRole = MenuRole::None;
    u32 pendingId = 0;
    u32 pathIds[cMenuDepthMax] = {};
    s32 pathNodes[cMenuDepthMax] = {};
    s32 depth = 0;
    u32 disabledBits = 0;
    s32 disabledLevel = 0;
};

static MenuVisit sMenuVisit;
static u32 sPendingMenuId = 0;

static bool IsDrawingMenus()
{
    return sMenuVisit.mode == MenuMode::Draw;
}

static bool IsVisitDisabled()
{
    return sMenuVisit.disabledBits != 0;
}

static u32 GetVisitParentId()
{
    return sMenuVisit.depth > 0 ? sMenuVisit.pathIds[sMenuVisit.depth - 1] : 0;
}

static MenuNode* AddVisitNode(MenuNodeKind kind, u32 id, const char* label)
{
    MenuNode node;
    node.kind = kind;
    node.role = sMenuVisit.nextRole;
    node.id = id;
    node.parent = sMenuVisit.depth > 0 ? sMenuVisit.pathNodes[sMenuVisit.depth - 1] : -1;
    node.label = label ? label : "";
    node.enabled = !IsVisitDisabled();

    sMenuVisit.nextRole = MenuRole::None;
    sMenuVisit.tree->nodes.push_back(node);
    return &sMenuVisit.tree->nodes.back();
}

void SetNextMenuRole(MenuRole role)
{
    if (sMenuVisit.mode == MenuMode::Record)
        sMenuVisit.nextRole = role;
}

static bool VisitItem(const char* idKey, const char* label, std::optional<shortcuts::Action> shortcut, bool selected, bool enabled, const char* tooltip)
{
    const u32 id = ImHashStr(idKey, 0, GetVisitParentId());
    const bool isEnabled = enabled && !IsVisitDisabled();

    if (sMenuVisit.mode == MenuMode::Activate)
    {
        if (id != sMenuVisit.pendingId || !isEnabled)
            return false;

        sMenuVisit.pendingId = 0;
        return true;
    }

    MenuNode* node = AddVisitNode(MenuNodeKind::Item, id, label);
    node->enabled = isEnabled;
    node->checked = selected;
    node->keyChord = shortcut ? shortcuts::Chord(*shortcut) : 0;

    if (tooltip)
        node->tooltip = tooltip;

    return false;
}

static bool VisitMenu(const char* label, bool* clicked)
{
    const u32 id = ImHashStr(label, 0, GetVisitParentId());
    const bool isEnabled = !IsVisitDisabled();
    s32 nodeIndex = -1;

    if (sMenuVisit.mode == MenuMode::Record)
    {
        MenuNode* node = AddVisitNode(MenuNodeKind::Menu, id, label);
        node->clickable = clicked != nullptr;
        nodeIndex = static_cast<s32>(sMenuVisit.tree->nodes.size()) - 1;
    }
    else if (clicked && isEnabled && id == sMenuVisit.pendingId)
    {
        sMenuVisit.pendingId = 0;
        *clicked = true;
    }

    if (!isEnabled)
        return false;

    SEAD_ASSERT(sMenuVisit.depth < cMenuDepthMax);

    sMenuVisit.pathIds[sMenuVisit.depth] = id;
    sMenuVisit.pathNodes[sMenuVisit.depth] = nodeIndex;
    sMenuVisit.depth++;

    return true;
}

static void EndVisitMenu()
{
    SEAD_ASSERT(sMenuVisit.depth > 0);

    sMenuVisit.depth--;
}

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
    if (!IsDrawingMenus())
        return VisitMenu(label, nullptr);

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
    if (!IsDrawingMenus())
    {
        EndVisitMenu();
        return;
    }

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
    if (!IsDrawingMenus())
        return VisitMenu(label, clicked);

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

bool MenuBarItem(const char* label, std::optional<shortcuts::Action> shortcut, bool selected, bool enabled)
{
    if (!IsDrawingMenus())
        return VisitItem(label, label, shortcut, selected, enabled, nullptr);

    const f32 posY = ImGui::GetCursorScreenPos().y;
    const char* shortcutLabel = shortcut ? shortcuts::Label(*shortcut) : nullptr;

    ImDrawList* drawList = BeginMenuHighlight();
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, uicolors::cTransparent);
    const bool pressed = ImGui::MenuItem(label, shortcutLabel, false, enabled);
    ImGui::PopStyleColor();
    EndMenuHighlight(drawList, false);

    if (selected)
        DrawMenuCheck(enabled);

    if (shortcutLabel && shortcutLabel[0] != '\0')
    {
        const f32 right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;

        ImVec4 color = GetItemValueColor();

        if (!enabled && !(GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled))
            color.w *= ImGui::GetStyle().DisabledAlpha;

        drawList->AddText(ImVec2(right - ImGui::CalcTextSize(shortcutLabel).x, posY), ImGui::GetColorU32(color), shortcutLabel);
    }

    return pressed;
}

bool BeginSubMenu(const char* label)
{
    if (!IsDrawingMenus())
        return VisitMenu(label, nullptr);

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

bool BeginIconSubMenu(const char* icon, const char* label)
{
    if (!IsDrawingMenus())
        return VisitMenu(label, nullptr);

    return BeginSubMenu(sead::FormatFixedSafeString<96>("%s %s", icon, label).cstr());
}

void EndSubMenu()
{
    if (!IsDrawingMenus())
    {
        EndVisitMenu();
        return;
    }

    ImGui::EndMenu();
}

bool MenuBarIconItem(const char* icon, const char* label, std::optional<shortcuts::Action> shortcut, bool enabled, const char* disabledReason)
{
    if (!IsDrawingMenus())
        return VisitItem(label, label, shortcut, false, enabled, enabled ? nullptr : disabledReason);

    const bool pressed = MenuBarItem(sead::FormatFixedSafeString<96>("%s %s", icon, label).cstr(), shortcut, false, enabled);

    if (!enabled && disabledReason)
        ImGui::SetItemTooltip("%s", disabledReason);

    return pressed;
}

bool VisitMenuFileItem(const char* path)
{
    SEAD_ASSERT(!IsDrawingMenus());

    return VisitItem(path, pathtext::FileName(path), std::nullopt, false, true, pathtext::Folder(path).c_str());
}

void MenuSeparator()
{
    if (sMenuVisit.mode == MenuMode::Record)
        AddVisitNode(MenuNodeKind::Separator, 0, nullptr);
    else if (IsDrawingMenus())
        ImGui::Separator();
}

void BeginMenuDisabled(bool disabled)
{
    if (IsDrawingMenus())
    {
        ImGui::BeginDisabled(disabled);
        return;
    }

    SEAD_ASSERT(sMenuVisit.disabledLevel < cMenuDisabledMax);

    if (disabled)
        sMenuVisit.disabledBits |= 1u << sMenuVisit.disabledLevel;

    sMenuVisit.disabledLevel++;
}

void EndMenuDisabled()
{
    if (IsDrawingMenus())
    {
        ImGui::EndDisabled();
        return;
    }

    SEAD_ASSERT(sMenuVisit.disabledLevel > 0);

    sMenuVisit.disabledLevel--;
    sMenuVisit.disabledBits &= ~(1u << sMenuVisit.disabledLevel);
}

static s32 sMenuFindFrame = 0;
static std::optional<bool> sMenuThemeRequest;

static void DrawBackupsMenu()
{
    if (!BeginIconSubMenu(ICON_LC_HISTORY, messages::common::cBackups))
        return;

    if (MenuBarIconItem(ICON_LC_DATABASE_BACKUP, messages::mainmenu::cCreateBackup) && backup::CreateBackup())
        backup::OpenRestoreDialog();

    if (MenuBarIconItem(ICON_LC_ARCHIVE_RESTORE, messages::backup::cMenuRestore))
        backup::OpenRestoreDialog();

    const std::string folder = backup::GetFolder();

    if (MenuBarIconItem(ICON_LC_FOLDER_CLOCK, messages::mainmenu::cShowBackupFolder, std::nullopt, !folder.empty()))
        OpenFolder(folder);

    EndSubMenu();
}

static void DrawEditMenu()
{
    const bool documentOpen = IsDocumentOpen();
    const bool hasItem = documentOpen && sSelectedItem != nullptr;
    const Item* activeItem = documentOpen ? GetActiveSelection() : nullptr;
    const bool canDuplicate = activeItem && CanDuplicateSelection(activeItem);

    if (MenuBarIconItem(ICON_LC_TEXT_CURSOR_INPUT, messages::common::cRename, shortcuts::Action::RenameSelection, hasItem && CanRenameItem(sSelectedItem)))
        shortcuts::Trigger(shortcuts::Action::RenameSelection);

    if (MenuBarIconItem(ICON_LC_COPY, messages::common::cDuplicate, shortcuts::Action::DuplicateSelection, canDuplicate))
        shortcuts::Trigger(shortcuts::Action::DuplicateSelection);

    if (MenuBarIconItem(ICON_LC_LIST_CHECKS, messages::common::cSelectAll, shortcuts::Action::SelectAll, documentOpen))
        shortcuts::Trigger(shortcuts::Action::SelectAll);

    MenuSeparator();

    if (MenuBarIconItem(ICON_LC_SEARCH, messages::common::cFind, shortcuts::Action::FocusFilter, documentOpen))
    {
        sMenuFindFrame = ImGui::GetFrameCount() + 1;
        shortcuts::Trigger(shortcuts::Action::FocusFilter);
    }

    MenuSeparator();

    if (MenuBarIconItem(ICON_LC_TRASH_2, messages::common::cDelete, std::nullopt, hasItem))
        shortcuts::Trigger(shortcuts::Action::DeleteSelection);
}

static void DrawViewMenu()
{
    const bool meterVisible = IsOutputMeterWindowVisible();

    if (MenuBarItem(messages::mainmenu::cOutputMeter, std::nullopt, meterVisible))
        SetOutputMeterWindowVisible(!meterVisible);

    if (MenuBarItem(messages::mainmenu::cSystemWindow, std::nullopt, IsSystemWindowShown()))
        SetSystemWindowShown(!IsSystemWindowShown());

    MenuSeparator();

    if (BeginSubMenu(messages::common::cTheme))
    {
        if (MenuBarItem(messages::mainmenu::cThemeDark, std::nullopt, IsDarkTheme()))
            sMenuThemeRequest = true;

        if (MenuBarItem(messages::mainmenu::cThemeLight, std::nullopt, !IsDarkTheme()))
            sMenuThemeRequest = false;

        EndSubMenu();
    }

    MenuSeparator();

    if (MenuBarItem(messages::mainmenu::cResetLayout))
        RequestResetLayout();
}

static void DrawHelpMenu()
{
    if (MenuBarIconItem(ICON_LC_KEYBOARD, messages::mainmenu::cShortcuts))
        OpenShortcutsDialog();

    MenuSeparator();

    if (MenuBarIconItem(ICON_LC_FOLDER_GIT_2, messages::mainmenu::cRepository))
        OpenURL(cRepositoryUrl);

    if (MenuBarIconItem(ICON_LC_BUG, messages::mainmenu::cReportIssue))
        OpenURL(cIssuesUrl);

    if (MenuBarIconItem(ICON_LC_MESSAGE_CIRCLE, messages::mainmenu::cDiscord))
        OpenURL(cDiscordUrl);

    MenuSeparator();

    SetNextMenuRole(MenuRole::About);

    if (MenuBarIconItem(ICON_LC_INFO, messages::mainmenu::cAbout))
        RequestAbout();
}

static void DrawRecentFilesMenu()
{
    if (!BeginIconSubMenu(ICON_LC_CLOCK, messages::mainmenu::cOpenRecent))
        return;

    std::string path;
    const bool chosen = IsDrawingMenus() ? DrawRecentFilesList(&path) : VisitRecentFilesList(&path);

    if (chosen)
        RequestOpenFile(path.c_str());

    EndSubMenu();
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

bool IsSlimMenuBar()
{
    return IsPlayerFrameShown() || IsFileEditorShown();
}

static void DrawFileMenu()
{
    const bool documentOpen = IsDocumentOpen();
    const bool archiveOpen = IsSoundArchiveOpen();
    const bool isSlim = IsSlimMenuBar();
    const bool isPlayer = IsPlayerFrameShown();

    if (!isPlayer && MenuBarIconItem(ICON_LC_FILE, messages::mainmenu::cNew, shortcuts::Action::NewProject))
        OpenNewFileDialog();

    if (MenuBarIconItem(ICON_LC_FOLDER_OPEN, messages::common::cOpen, shortcuts::Action::OpenProject))
        RequestOpenFile();

    if (!isSlim && MenuBarIconItem(ICON_LC_FOLDER_PLUS, messages::common::cOpenFolder))
        RequestFilesPanelFolderDialog();

    DrawRecentFilesMenu();

    MenuSeparator();
    BeginMenuDisabled(!documentOpen || ActiveDocument()->isPlayerOnly());

    if (MenuBarIconItem(ICON_LC_SAVE, messages::common::cSave, shortcuts::Action::SaveProject))
        RequestSaveFile();

    if (MenuBarIconItem(ICON_LC_SAVE_ALL, messages::mainmenu::cSaveAs, shortcuts::Action::SaveProjectAs))
        RequestSaveDocumentAs(ActiveDocument());

    if (!isSlim && MenuBarIconItem(ICON_LC_FILE_STACK, messages::mainmenu::cSaveAll, shortcuts::Action::SaveAllProjects, HasUnsavedDocuments()))
        RequestSaveAll();

    if (isSlim && MenuBarIconItem(ICON_LC_FILE_AUDIO, messages::menu::cExportWav, std::nullopt, CanExportActiveFileWav()))
        RequestExportActiveFileWav();

    if (!isPlayer)
        DrawBackupsMenu();

    EndMenuDisabled();
    MenuSeparator();

    if (!isSlim)
    {
        BeginMenuDisabled(!archiveOpen);
        DrawImportMenu();
        EndMenuDisabled();

        BeginMenuDisabled(!archiveOpen && !CanExportActiveFileWav());
        DrawExportMenu();
        EndMenuDisabled();

        MenuSeparator();
    }

    const bool hasPath = documentOpen && !GetDocumentPath().isEmpty();

    if (MenuBarIconItem(ICON_LC_FOLDER_SEARCH, messages::recent::cShowInFileManager, std::nullopt, hasPath))
        ShowInFileManager(GetDocumentPath().cstr());

    if (MenuBarIconItem(ICON_LC_COPY, messages::mainmenu::cCopyPath, std::nullopt, hasPath))
        ImGui::SetClipboardText(GetDocumentPath().cstr());

    MenuSeparator();

    if (isSlim && MenuBarIconItem(ICON_LC_LAYOUT_PANEL_LEFT, messages::mainmenu::cOpenFullEditor))
        RequestFullEditor();

    if (!isPlayer && MenuBarIconItem(ICON_LC_FILE_OUTPUT, messages::common::cClose, shortcuts::Action::CloseProject, documentOpen))
        RequestCloseFile();

    SetNextMenuRole(MenuRole::Quit);

    if (MenuBarIconItem(ICON_LC_DOOR_OPEN, messages::mainmenu::cExit))
        TryExit();
}

static f32 DrawSaveStatusChip(f32 bellLeft, const ImVec2& rangeY, f32 textY, f32 minLeft)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const bool isUnsaved = HasUnsavedChanges();
    const char* statusIcon = isUnsaved ? ICON_LC_FILE : ICON_LC_CHECK_CHECK;
    const sead::FormatFixedSafeString<64> statusLabel("%s %s", statusIcon, isUnsaved ? messages::mainmenu::cUnsaved : messages::mainmenu::cSaved);
    const ImVec4 statusColor = isUnsaved ? uicolors::Warning() : style.Colors[ImGuiCol_TextDisabled];

    const f32 chipRight = bellLeft - style.ItemInnerSpacing.x;
    const f32 padding = style.ItemSpacing.x * 2.0f;
    const bool fits = chipRight - ImGui::CalcTextSize(statusLabel.cstr()).x - padding >= minLeft;

    if (!isUnsaved && !fits)
        return bellLeft;

    const char* statusText = fits ? statusLabel.cstr() : statusIcon;
    const f32 chipLeft = chipRight - ImGui::CalcTextSize(statusText).x - padding;

    ImGuiCol chipColor = ImGuiCol_FrameBg;

    if (isUnsaved)
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

static f32 DrawMenuBarTools(f32 right, const ImVec2& rangeY, f32 textY, f32 minChipLeft)
{
    if (IsPlayerFrameShown())
        return DrawPlayerFrameTools(right, rangeY.x, rangeY.y);

    const f32 bellLeft = right - notify::DrawBell(right, rangeY.x, rangeY.y);

    if (!IsDocumentOpen())
        return bellLeft;

    return DrawSaveStatusChip(bellLeft, rangeY, textY, minChipLeft);
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
    DrawBarMenu(messages::common::cFile, &DrawFileMenu);

    if (!IsSlimMenuBar())
    {
        DrawBarMenu(messages::mainmenu::cEdit, &DrawEditMenu);
        DrawBarMenu(messages::mainmenu::cView, &DrawViewMenu);
    }

    bool preferencesClicked = false;

    SetNextMenuRole(MenuRole::Settings);

    if (BeginHoverBarMenu(messages::mainmenu::cPreferences, &preferencesClicked))
    {
        DrawPreferencesMenu();
        EndBarMenu();
    }

    if (preferencesClicked)
        OpenPreferencesWindow(PreferencesPage::File);

    SetNextMenuRole(MenuRole::Help);
    DrawBarMenu(messages::common::cHelp, &DrawHelpMenu);
}

static constexpr f32 cTitleDragMinEm = 3.0f;

static void PushTitleRowStyle(f32 framePadY)
{
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, framePadY));
}

static f32 DrawTitleRowContent(const ImVec2& barMin, f32 lineBottom, f32 toolsRight, f32 framePadY, f32* outLeft, f32* outRight)
{
    const f32 dragGap = ImFloor(GImGui->FontBaseSize * cTitleDragMinEm);

    PushTitleRowStyle(framePadY);

    TitleRowPlace place;
    place.columnLeft = barMin.x;
    place.left = GetTitleBarContentLeft(barMin.x);
    place.top = barMin.y + ImFloor((lineBottom - barMin.y - ImGui::GetFrameHeight()) * 0.5f + 0.5f);
    place.bottom = place.top + ImGui::GetFrameHeight();

    const f32 minChipLeft = CalcTitleRowStripMinRight(place) + dragGap;

    ImGui::PopStyleVar();

    const f32 textY = ImFloor((place.top + place.bottom - ImGui::GetFontSize()) * 0.5f);
    const f32 toolsLeft = DrawMenuBarTools(toolsRight, ImVec2(place.top, place.bottom), textY, minChipLeft);

    place.right = toolsLeft - dragGap;
    place.maxRight = toolsLeft - ImGui::GetStyle().ItemInnerSpacing.x;

    PushTitleRowStyle(framePadY);

    *outLeft = place.left;
    *outRight = DrawDocumentTabsInTitleRow(place);

    ImGui::PopStyleVar();

    return toolsLeft;
}

static void DrawMenuBarItems(f32 em, f32 framePadY)
{
    const ImVec2 barMin = ImGui::GetWindowPos();
    const ImVec2 barMax = barMin + ImGui::GetWindowSize();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(barMin.x, barMax.y - 1.0f), ImVec2(barMax.x, barMax.y - 1.0f), ImGui::GetColorU32(ImGuiCol_Border));

    const f32 textOffsetY = -ImFloor(em * cMenuBarTextRaiseEm);
    ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(ImFloor(ImGui::GetStyle().ItemSpacing.x * 0.5f), textOffsetY));

    const bool hasTitleBar = HasCustomTitleBar();
    const f32 lineBottom = barMax.y - 1.0f;

    if (hasTitleBar)
        ImGui::SetCursorScreenPos(ImVec2(DrawTitleBarLeft(barMin.x, barMin.y, lineBottom), ImGui::GetCursorScreenPos().y));

    const f32 menuLeft = ImGui::GetCursorScreenPos().x;
    if (!IsNativeMenuBar())
    {
        CloseHoverMenuOnLeave();
        DrawBarMenus();
    }
    const f32 menuRight = ImGui::GetCursorScreenPos().x;

    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 edge = hasTitleBar ? DrawTitleBarRight(barMax.x, barMin.y, lineBottom) : barMax.x;
    const f32 toolsRight = edge - style.WindowPadding.x;

    f32 contentLeft = menuLeft;
    f32 contentRight = menuRight;

    const f32 toolsLeft = IsTabStripInTitleRow() ? DrawTitleRowContent(barMin, lineBottom, toolsRight, framePadY, &contentLeft, &contentRight)
                                                 : DrawMenuBarTools(toolsRight, CalcMenuBarPillRangeY(), barMin.y + style.FramePadding.y + textOffsetY, -FLT_MAX);

    if (hasTitleBar)
        FinishTitleBar(contentLeft, contentRight, toolsLeft, barMax.y);
}

static f32 CalcMenuBarPadY(f32 em, f32 framePadY)
{
    if (!HasCustomTitleBar())
        return ImFloor(em * cMenuBarPadYEm);

    const f32 padY = ImFloor(em * cTitleBarPadYEm);
    const f32 minHeight = em + (framePadY + padY) * 2.0f;
    const f32 height = CalcTitleBarHeight(minHeight);

    return height > minHeight ? (height - em) * 0.5f - framePadY : padY;
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

void RecordMenuBar(MenuTree* tree)
{
    SEAD_ASSERT(tree && IsDrawingMenus());

    tree->nodes.clear();

    sMenuVisit = MenuVisit();
    sMenuVisit.mode = MenuMode::Record;
    sMenuVisit.tree = tree;

    DrawBarMenus();

    SEAD_ASSERT(sMenuVisit.depth == 0 && sMenuVisit.disabledLevel == 0);
    sMenuVisit = MenuVisit();
}

void RequestMenuActivation(u32 id)
{
    sPendingMenuId = id;
}

static void RunPendingMenuActivation()
{
    if (sPendingMenuId == 0)
        return;

    sMenuVisit = MenuVisit();
    sMenuVisit.mode = MenuMode::Activate;
    sMenuVisit.pendingId = sPendingMenuId;
    sPendingMenuId = 0;

    DrawBarMenus();

    SEAD_ASSERT(sMenuVisit.depth == 0 && sMenuVisit.disabledLevel == 0);
    sMenuVisit = MenuVisit();
}

void DrawMenuBar()
{
    FocusInfoWindowForFind();
    UpdateNativeMenuBar();
    RunPendingMenuActivation();

    const f32 em = ImGui::GetFontSize();
    const ImVec2 framePadding = ImGui::GetStyle().FramePadding;

    const ImVec4 hoverColor = ImGui::GetStyleColorVec4(ImGuiCol_Header);
    const ImVec4 heldColor = ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive);
    const ImVec4 popupColor = uicolors::Opaque(ImGui::GetStyleColorVec4(ImGuiCol_PopupBg));

    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, hoverColor);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, heldColor);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, popupColor);

    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, ImFloor(em * cMenuRoundingEm));
    sMenuBarPadY = CalcMenuBarPadY(em, framePadding.y);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(framePadding.x, framePadding.y + sMenuBarPadY));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const bool barOpen = ImGui::BeginMainMenuBar();
    ImGui::PopStyleVar();

    if (barOpen)
    {
        DrawMenuBarItems(em, framePadding.y);
        ImGui::EndMainMenuBar();
    }

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);

    ApplyMenuThemeRequest();
}
