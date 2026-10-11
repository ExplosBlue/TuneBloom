#define IMGUI_DEFINE_MATH_OPERATORS
#include <ui/file/RecentFiles.h>

#include <ui/settings/Config.h>
#include <ui/settings/ConfigFile.h>
#include <ui/widgets/ContextMenu.h>
#include <ui/items/ItemColumns.h>
#include <ui/app/MenuBar.h>
#include <ui/Messages.h>
#include <core/TestHooks.h>
#include <ui/file/SystemShell.h>
#include <ui/widgets/Widgets.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_custom.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>

enum class RecentSort
{
    Min = 0,

    LastOpened = Min,
    Name,
    Folder,

    Max = Folder
};

enum class RecentAction
{
    None,
    Open,
    TogglePin,
    Remove,
    MoveUp,
    MoveDown
};

static constexpr s32    cRecentVisibleRows  = 12;

static constexpr f32    cRecentNameMaxEm    = 16.0f;
static constexpr f32    cRecentFolderMaxEm  = 7.0f;
static constexpr f32    cRecentFolderGapEm  = 1.0f;

static constexpr const char* cFilesSection = "files";
static constexpr const char* cRecentKey    = "recent";
static constexpr const char* cPinnedKey    = "pinned";

static RecentSort sRecentSort = RecentSort::LastOpened;
static bool sRecentSortDescending = false;
static bool sRecentDragMoved = false;

std::vector<std::string>& GetRecentFiles()
{
    static std::vector<std::string>* files = new std::vector<std::string>();
    return *files;
}

static std::vector<std::string>& GetPinnedFiles()
{
    static std::vector<std::string>* files = new std::vector<std::string>();
    return *files;
}

static std::vector<std::string>& GetMissingPinnedFiles()
{
    static std::vector<std::string>* files = new std::vector<std::string>();
    return *files;
}

static s32 ComparePathText(const char* a, const char* b)
{
    for (; *a != '\0' && *b != '\0'; a++, b++)
    {
        const s32 diff = tolower(static_cast<u8>(*a)) - tolower(static_cast<u8>(*b));
        if (diff != 0)
            return diff;
    }

    return static_cast<u8>(*a) - static_cast<u8>(*b);
}

static bool IsSamePath(const std::string& a, const std::string& b)
{
#if defined(SEAD_PLATFORM_WINDOWS)
    return ComparePathText(a.c_str(), b.c_str()) == 0;
#else
    return a == b;
#endif
}

static bool ContainsPath(const std::vector<std::string>& files, const std::string& path)
{
    return std::any_of(files.begin(), files.end(), [&path](const std::string& file) { return IsSamePath(file, path); });
}

static void RemovePath(std::vector<std::string>* files, const std::string& path)
{
    files->erase(std::remove_if(files->begin(), files->end(), [&path](const std::string& file) { return IsSamePath(file, path); }), files->end());
}

static std::string NormalizePath(const char* path)
{
    if (path[0] == '\0')
        return std::string();

    std::error_code ec;
    const std::filesystem::path absolute = std::filesystem::absolute(std::filesystem::path(path), ec);

    std::string normalized = ec ? std::string(path) : absolute.lexically_normal().string();

#if defined(SEAD_PLATFORM_WINDOWS)
    std::replace(normalized.begin(), normalized.end(), '/', '\\');
#endif

    return normalized;
}

static bool IsFileMissing(const std::string& path)
{
    std::error_code ec;
    return !std::filesystem::is_regular_file(std::filesystem::path(path), ec);
}

static void UpdateMissingPinnedFiles()
{
    std::vector<std::string>& missing = GetMissingPinnedFiles();
    missing.clear();

    for (const std::string& path : GetPinnedFiles())
    {
        if (IsFileMissing(path))
            missing.push_back(path);
    }
}

static void WritePathList(json::Value* section, const char* key, const std::vector<std::string>& files)
{
    json::Value& list = section->setArray(key);

    for (const std::string& file : files)
        list.pushString(file.c_str());
}

static void ReadPathList(const json::Value& section, const char* key, std::vector<std::string>* files)
{
    files->clear();

    const json::Value* list = section.findArray(key);

    if (!list)
        return;

    for (const json::Value& item : list->items)
    {
        if (!item.isString())
            continue;

        const std::string file = NormalizePath(item.string.c_str());

        if (!file.empty() && !ContainsPath(*files, file))
            files->push_back(file);
    }
}

static void SaveRecentFiles()
{
    if (testhooks::IsEnabled())
        return;

    json::Value section;
    WritePathList(&section, cRecentKey, GetRecentFiles());
    WritePathList(&section, cPinnedKey, GetPinnedFiles());

    SaveConfigSection(cStateFile, cFilesSection, section);
}

static void PruneRecentFiles()
{
    std::vector<std::string>& files = GetRecentFiles();

    auto it = std::remove_if(files.begin(), files.end(), IsFileMissing);
    if (it == files.end())
        return;

    files.erase(it, files.end());
    SaveRecentFiles();
}

static void LimitRecentFiles()
{
    std::vector<std::string>& files = GetRecentFiles();
    const size_t max = static_cast<size_t>(GetStartupSettings().recentFileCount);

    if (files.size() > max)
        files.resize(max);
}

void LoadRecentFiles()
{
    json::Value section;
    LoadConfigSection(cStateFile, cFilesSection, &section);

    ReadPathList(section, cRecentKey, &GetRecentFiles());
    ReadPathList(section, cPinnedKey, &GetPinnedFiles());

    for (const std::string& path : GetPinnedFiles())
        RemovePath(&GetRecentFiles(), path);

    PruneRecentFiles();
    UpdateMissingPinnedFiles();
}

void AddRecentFile(const char* path)
{
    if (testhooks::IsEnabled())
        return;

    const std::string file = NormalizePath(path);

    if (file.empty() || ContainsPath(GetPinnedFiles(), file))
        return;

    std::vector<std::string>& files = GetRecentFiles();

    RemovePath(&files, file);
    files.insert(files.begin(), file);

    TrimRecentFiles();
}

void TrimRecentFiles()
{
    LimitRecentFiles();
    SaveRecentFiles();
}

static void* RecentSettingsReadOpen(ImGuiContext*, ImGuiSettingsHandler*, const char*)
{
    return &sRecentSort;
}

static void RecentSettingsReadLine(ImGuiContext*, ImGuiSettingsHandler*, void*, const char* line)
{
    int sort = 0;
    int descending = 0;

    if (sscanf(line, "Sort=%d", &sort) == 1 && sort >= static_cast<int>(RecentSort::Min) && sort <= static_cast<int>(RecentSort::Max))
        sRecentSort = static_cast<RecentSort>(sort);
    else if (sscanf(line, "Descending=%d", &descending) == 1)
        sRecentSortDescending = descending != 0;
}

static void RecentSettingsWriteAll(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buf)
{
    buf->appendf("[%s][View]\n", handler->TypeName);
    buf->appendf("Sort=%d\n", static_cast<int>(sRecentSort));
    buf->appendf("Descending=%d\n\n", sRecentSortDescending ? 1 : 0);
}

void RegisterRecentFilesSettings()
{
    ImGuiSettingsHandler handler;
    handler.TypeName = "RecentFiles";
    handler.TypeHash = ImHashStr("RecentFiles");
    handler.ReadOpenFn = RecentSettingsReadOpen;
    handler.ReadLineFn = RecentSettingsReadLine;
    handler.WriteAllFn = RecentSettingsWriteAll;

    ImGui::AddSettingsHandler(&handler);
}

static const char* GetPathFileName(const char* path)
{
    const char* name = path;

    for (const char* c = path; *c != '\0'; c++)
    {
        if (*c == '/' || *c == '\\')
            name = c + 1;
    }

    return name;
}

static std::string GetPathFolder(const std::string& path)
{
    const char* name = GetPathFileName(path.c_str());
    if (name == path.c_str())
        return std::string();

    return std::string(path.c_str(), name - 1);
}

static std::string GetRecentFolderName(const std::string& path)
{
    const std::string folder = GetPathFolder(path);

    return GetPathFileName(folder.c_str());
}

static void SortRecentFiles(std::vector<const std::string*>* rows)
{
    if (sRecentSort == RecentSort::LastOpened)
        return;

    const bool byFolder = sRecentSort == RecentSort::Folder;
    const bool descending = sRecentSortDescending;

    std::stable_sort(rows->begin(), rows->end(), [byFolder, descending](const std::string* a, const std::string* b)
    {
        s32 order = 0;

        if (byFolder)
            order = ComparePathText(GetRecentFolderName(*a).c_str(), GetRecentFolderName(*b).c_str());

        if (order == 0 && byFolder)
            order = ComparePathText(GetPathFolder(*a).c_str(), GetPathFolder(*b).c_str());

        if (order == 0)
            order = ComparePathText(GetPathFileName(a->c_str()), GetPathFileName(b->c_str()));

        return descending ? order > 0 : order < 0;
    });
}

static void ToggleRecentSort(RecentSort sort)
{
    if (sRecentSort != sort)
    {
        sRecentSort = sort;
        sRecentSortDescending = false;
    }
    else if (!sRecentSortDescending)
    {
        sRecentSortDescending = true;
    }
    else
    {
        sRecentSort = RecentSort::LastOpened;
        sRecentSortDescending = false;
    }

    ImGui::MarkIniSettingsDirty();
}

static f32 CalcRecentRowHeight()
{
    return ImGui::GetFontSize() + ImGui::GetStyle().ItemSpacing.y;
}

static bool DrawRecentRowButton(const char* id, const char* icon, bool lit, bool visible, const char* tooltip)
{
    const f32 size = CalcRecentRowHeight();
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    const bool pressed = ImGui::InvisibleButton(id, ImVec2(size, size));
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("%s", tooltip);

    if (!visible && !hovered && !held)
        return pressed;

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const f32 hoverRatio = ImGui::GetHoverRatio(ImGui::GetItemID() + 1, hovered || held);

    if (hoverRatio > 0.0f)
        drawList->AddRectFilled(pos, pos + ImVec2(size, size), ImGui::GetColorU32(held ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered, hoverRatio), ImGui::GetStyle().FrameRounding);

    const ImVec2 iconSize = ImGui::CalcTextSize(icon);
    const ImGuiCol color = lit ? ImGuiCol_CheckMark : hovered || held ? ImGuiCol_Text : ImGuiCol_TextDisabled;

    drawList->AddText(pos + ImFloor((ImVec2(size, size) - iconSize) * 0.5f), ImGui::GetColorU32(color), icon);

    return pressed;
}

static RecentAction DrawRecentRow(const std::string& path, bool pinned, bool missing, bool canDrag, f32 width, f32 nameWidth)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 em = ImGui::GetFontSize();
    const f32 buttonSize = CalcRecentRowHeight();
    const char* name = GetPathFileName(path.c_str());
    const std::string folder = GetRecentFolderName(path);

    RecentAction action = RecentAction::None;
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    ImGui::PushID(path.c_str());

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 min(pos.x, pos.y - ImFloor(style.ItemSpacing.y * 0.5f));
    const ImVec2 max(pos.x + width, min.y + buttonSize);

    const ImGuiID activeId = ImGui::GetActiveID();
    const bool ownActive = activeId != 0 && (activeId == ImGui::GetID("##row") || activeId == ImGui::GetID("##pin") || activeId == ImGui::GetID("##remove"));
    const bool rowHovered = ownActive || ImGui::IsPopupOpen("##menu") || (activeId == 0 && ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(min, max));

    ImDrawList* highlightList = BeginMenuHighlight();
    const bool pressed = ImGui::Selectable("##row", false, ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_DontClosePopups, ImVec2(width, 0.0f));
    const bool held = ImGui::IsItemActive();
    const bool detailHovered = IsItemHoveredForDetail();
    EndMenuHighlight(highlightList, rowHovered);

    ImGui::OpenPopupOnItemClick("##menu", ImGuiPopupFlags_MouseButtonRight);

    if (held && canDrag && sRecentDragMoved)
        drawList->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_CheckMark), style.FrameRounding);

    // Icon
    const char* icon = missing ? ICON_LC_FILE_X : canDrag && rowHovered ? ICON_LC_GRIP_VERTICAL : ICON_LC_FILE_MUSIC;
    const ImVec4 dimColor = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    const f32 textY = pos.y;

    drawList->AddText(ImVec2(pos.x, textY), ImGui::GetColorU32(missing ? dimColor : GetItemValueColor()), icon);

    // Name and folder
    const f32 buttonsLeft = max.x - buttonSize * 2.0f;
    const f32 textRight = buttonsLeft + (pinned || !rowHovered ? buttonSize : 0.0f) - style.ItemSpacing.x;
    const f32 nameLeft = pos.x + ImGui::CalcTextSize(ICON_LC_FILE_MUSIC " ").x;
    const f32 folderLeft = nameLeft + nameWidth + em * cRecentFolderGapEm;
    const f32 nameRight = sead::Mathf::min(folderLeft - style.ItemSpacing.x, textRight);

    ImGui::PushStyleColor(ImGuiCol_Text, missing ? dimColor : ImGui::GetStyleColorVec4(ImGuiCol_Text));
    ImGui::RenderTextEllipsis(drawList, ImVec2(nameLeft, textY), ImVec2(nameRight, textY + em), nameRight, nameRight, name, nullptr, nullptr);
    ImGui::PopStyleColor();

    if (folderLeft < textRight)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, dimColor);
        ImGui::RenderTextEllipsis(drawList, ImVec2(folderLeft, textY), ImVec2(textRight, textY + em), textRight, textRight, folder.c_str(), nullptr, nullptr);
        ImGui::PopStyleColor();
    }

    if (detailHovered)
        ImGui::SetTooltip(missing ? messages::recent::cNotFoundFormat : "%s", path.c_str());

    // Reorder
    if (held && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        sRecentDragMoved = true;

        if (canDrag)
        {
            const f32 mouseY = ImGui::GetMousePos().y;

            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

            if (mouseY < min.y)
                action = RecentAction::MoveUp;
            else if (mouseY > max.y)
                action = RecentAction::MoveDown;
        }
    }

    if (pressed && !sRecentDragMoved && !missing)
    {
        action = RecentAction::Open;
        ImGui::CloseCurrentPopup();
    }

    // Buttons
    if (!pinned)
    {
        ImGui::SetCursorScreenPos(ImVec2(buttonsLeft, min.y));
        if (DrawRecentRowButton("##remove", ICON_LC_X, false, rowHovered, messages::recent::cRemove))
            action = RecentAction::Remove;
    }

    ImGui::SetCursorScreenPos(ImVec2(buttonsLeft + buttonSize, min.y));
    if (DrawRecentRowButton("##pin", ICON_LC_PIN, pinned, pinned || rowHovered, pinned ? messages::recent::cUnpin : messages::recent::cPin))
        action = RecentAction::TogglePin;

    if (BeginContextMenu("##menu"))
    {
        if (ContextMenuItem(ICON_LC_PIN, pinned ? messages::recent::cUnpin : messages::recent::cPin))
            action = RecentAction::TogglePin;

        if (ContextMenuItem(ICON_LC_FOLDER_OPEN, messages::recent::cShowInFileManager, !missing))
            ShowInFileManager(path);

        if (ContextMenuItem(ICON_LC_COPY, messages::mainmenu::cCopyPath))
            ImGui::SetClipboardText(path.c_str());

        ContextMenuSeparator();

        if (ContextMenuItem(ICON_LC_X, messages::recent::cRemove))
            action = RecentAction::Remove;

        EndContextMenu();
    }

    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy(ImVec2(width, em));

    ImGui::PopID();

    return action;
}

static void DrawRecentHeaderCell(const char* id, const char* title, RecentSort sort, f32 left, f32 textLeft, f32 right)
{
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool sorted = sRecentSort == sort;

    ImGui::SetCursorScreenPos(ImVec2(left, pos.y));

    ImDrawList* highlightList = BeginMenuHighlight();
    const bool pressed = ImGui::Selectable(id, false, ImGuiSelectableFlags_DontClosePopups, ImVec2(right - left, 0.0f));
    EndMenuHighlight(highlightList, false);

    if (pressed)
        ToggleRecentSort(sort);

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    drawList->AddText(ImVec2(textLeft, pos.y), ImGui::GetColorU32(sorted ? ImGuiCol_Text : ImGuiCol_TextDisabled), title);

    if (sorted)
    {
        const char* mark = sRecentSortDescending ? ICON_LC_CHEVRON_DOWN : ICON_LC_CHEVRON_UP;
        const f32 markLeft = textLeft + ImGui::CalcTextSize(title).x + ImGui::GetStyle().ItemInnerSpacing.x;

        drawList->AddText(ImVec2(markLeft, pos.y), ImGui::GetColorU32(GetItemValueColor()), mark);
    }

    ImGui::SetCursorScreenPos(pos);
}

static void DrawRecentHeader(f32 width, f32 nameWidth)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 em = ImGui::GetFontSize();
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    const f32 nameLeft = pos.x + ImGui::CalcTextSize(ICON_LC_FILE_MUSIC " ").x;
    const f32 folderLeft = nameLeft + nameWidth + em * cRecentFolderGapEm;
    const f32 folderCellLeft = folderLeft - ImFloor(style.ItemSpacing.x * 0.5f);

    DrawRecentHeaderCell("##sortName", messages::recent::cSortName, RecentSort::Name, pos.x, nameLeft, folderCellLeft - style.ItemSpacing.x);
    DrawRecentHeaderCell("##sortFolder", messages::recent::cSortFolder, RecentSort::Folder, folderCellLeft, folderLeft, pos.x + width);

    ImGui::Dummy(ImVec2(width, em));
}

template <typename Fn>
static f32 CalcRecentTextWidth(Fn getText, f32 maxWidth)
{
    f32 width = 0.0f;

    for (const std::vector<std::string>* files : { &GetPinnedFiles(), &GetRecentFiles() })
    {
        for (const std::string& path : *files)
            width = sead::Mathf::max(width, ImGui::CalcTextSize(getText(path).c_str()).x);
    }

    return sead::Mathf::min(width, maxWidth);
}

static void ApplyRecentAction(RecentAction action, const std::string& path)
{
    std::vector<std::string>& pinned = GetPinnedFiles();
    std::vector<std::string>& recent = GetRecentFiles();
    const auto it = std::find(pinned.begin(), pinned.end(), path);
    const bool isPinned = it != pinned.end();

    switch (action)
    {
    case RecentAction::TogglePin:
        if (isPinned)
        {
            pinned.erase(it);
            recent.insert(recent.begin(), path);
            LimitRecentFiles();
        }
        else
        {
            RemovePath(&recent, path);
            pinned.push_back(path);
        }
        break;

    case RecentAction::Remove:
        RemovePath(&pinned, path);
        RemovePath(&recent, path);
        break;

    case RecentAction::MoveUp:
        if (!isPinned || it == pinned.begin())
            return;
        std::iter_swap(it, it - 1);
        break;

    case RecentAction::MoveDown:
        if (!isPinned || it + 1 == pinned.end())
            return;
        std::iter_swap(it, it + 1);
        break;

    default:
        return;
    }

    UpdateMissingPinnedFiles();
    SaveRecentFiles();
}

bool DrawRecentFilesList(std::string* outPath)
{
    SEAD_ASSERT(outPath);

    std::vector<std::string>& pinned = GetPinnedFiles();
    std::vector<std::string>& recent = GetRecentFiles();

    if (ImGui::IsWindowAppearing())
    {
        PruneRecentFiles();
        UpdateMissingPinnedFiles();
    }

    if (pinned.empty() && recent.empty())
    {
        MenuBarItem(messages::recent::cEmpty, nullptr, false, false);
        return false;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 em = ImGui::GetFontSize();
    const f32 nameWidth = CalcRecentTextWidth([](const std::string& path) { return std::string(GetPathFileName(path.c_str())); }, em * cRecentNameMaxEm);
    const f32 folderWidth = CalcRecentTextWidth(GetRecentFolderName, em * cRecentFolderMaxEm);
    const f32 width = ImFloor(ImGui::CalcTextSize(ICON_LC_FILE_MUSIC " ").x + nameWidth + em * cRecentFolderGapEm + folderWidth + style.ItemSpacing.x + CalcRecentRowHeight());

    RecentAction action = RecentAction::None;
    std::string actionPath;

    auto drawRow = [&](const std::string& path, bool isPinned)
    {
        const bool missing = isPinned && ContainsPath(GetMissingPinnedFiles(), path);
        const RecentAction rowAction = DrawRecentRow(path, isPinned, missing, isPinned && pinned.size() > 1, width, nameWidth);

        if (rowAction == RecentAction::None)
            return;

        action = rowAction;
        actionPath = path;
    };

    for (const std::string& path : pinned)
        drawRow(path, true);

    if (!pinned.empty() && !recent.empty())
        ImGui::Separator();

    if (!recent.empty())
    {
        std::vector<const std::string*> rows;
        for (const std::string& path : recent)
            rows.push_back(&path);

        SortRecentFiles(&rows);

        DrawRecentHeader(width, nameWidth);

        const bool scroll = static_cast<s32>(rows.size()) > cRecentVisibleRows;
        const f32 inset = ImFloor(style.ItemSpacing.x * 0.5f);

        if (scroll)
        {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() - inset);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::BeginChild("##recent", ImVec2(width + inset * 2.0f + style.ScrollbarSize, cRecentVisibleRows * CalcRecentRowHeight()));
            ImGui::SetCursorPos(ImVec2(inset, ImFloor(style.ItemSpacing.y * 0.5f)));
            ImGui::Indent(inset);
        }

        for (const std::string* path : rows)
            drawRow(*path, false);

        if (scroll)
        {
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }
    }

    ImGui::Separator();

    const sead::FormatFixedSafeString<64> clearLabel(ICON_LC_BRUSH_CLEANING " %s", messages::recent::cClear);

    if (MenuBarItem(clearLabel.cstr(), nullptr, false, !recent.empty()))
    {
        recent.clear();
        SaveRecentFiles();
    }

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        sRecentDragMoved = false;

    if (action == RecentAction::Open)
    {
        *outPath = actionPath;
        return true;
    }

    ApplyRecentAction(action, actionPath);

    return false;
}
