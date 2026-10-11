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
#include <ui/widgets/TimeUtil.h>
#include <ui/widgets/Widgets.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_custom.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>

enum class RecentSort
{
    LastOpened,
    Name,
    Folder,
    Modified,
    Size
};

enum class RecentColumn
{
    Name,
    Folder,
    Modified,
    Size,
    Pin,
    Num
};

struct RecentFileInfo
{
    std::string path;
    std::string folder;
    std::string size;
    s64 time;
    u64 bytes;
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
static constexpr RecentSort cRecentMenuSortMax = RecentSort::Folder;

static constexpr f32    cRecentTableFolderMinEm  = 6.0f;
static constexpr f32    cRecentTableNameMinEm    = 8.0f;
static constexpr f32    cRecentEmptyGapEm        = 0.3f;

static constexpr f32    cRecentNameMaxEm    = 16.0f;
static constexpr f32    cRecentFolderMaxEm  = 7.0f;
static constexpr f32    cRecentFolderGapEm  = 1.0f;

static constexpr const char* cFilesSection = "files";
static constexpr const char* cRecentKey    = "recent";
static constexpr const char* cPinnedKey    = "pinned";

static RecentSort sRecentSort = RecentSort::LastOpened;
static bool sRecentSortDescending = false;
static bool sRecentDragMoved = false;
static bool sRecentInfoDirty = true;
static s32 sRecentTableFrame = -1;

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
    sRecentInfoDirty = true;

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

static void RefreshRecentFiles()
{
    PruneRecentFiles();
    UpdateMissingPinnedFiles();
    sRecentInfoDirty = true;
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

    RefreshRecentFiles();
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

    if (sscanf(line, "Sort=%d", &sort) == 1 && sort >= static_cast<int>(RecentSort::LastOpened) && sort <= static_cast<int>(cRecentMenuSortMax))
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

static std::vector<RecentFileInfo>& GetRecentFileInfos()
{
    static std::vector<RecentFileInfo>* infos = new std::vector<RecentFileInfo>();
    return *infos;
}

static RecentFileInfo ReadRecentFileInfo(const std::string& path)
{
    namespace fs = std::filesystem;

    RecentFileInfo info = { path, GetPathFolder(path), std::string(), 0, 0 };

    std::error_code ec;
    const fs::path file(path);
    const fs::file_time_type writeTime = fs::last_write_time(file, ec);

    if (!ec)
    {
        const auto systemTime = std::chrono::system_clock::now() + std::chrono::duration_cast<std::chrono::system_clock::duration>(writeTime - fs::file_time_type::clock::now());
        info.time = static_cast<s64>(std::chrono::system_clock::to_time_t(systemTime));
    }

    const std::uintmax_t bytes = fs::file_size(file, ec);

    if (!ec)
    {
        info.bytes = static_cast<u64>(bytes);

        sead::FixedSafeString<32> size;
        FormatItemSize(&size, static_cast<u32>(sead::Mathf::min(bytes, static_cast<std::uintmax_t>(0xFFFFFFFFu))));
        info.size = size.cstr();
    }

    return info;
}

static std::string FormatRecentDate(const RecentFileInfo& info)
{
    if (info.time == 0)
        return std::string();

    char date[48];
    timeutil::FormatRelativeDate(date, sizeof(date), info.time);

    return date;
}

static const RecentFileInfo& GetRecentFileInfo(const std::string& path)
{
    std::vector<RecentFileInfo>& infos = GetRecentFileInfos();

    if (sRecentInfoDirty)
    {
        infos.clear();
        sRecentInfoDirty = false;
    }

    for (const RecentFileInfo& info : infos)
    {
        if (info.path == path)
            return info;
    }

    infos.push_back(ReadRecentFileInfo(path));
    return infos.back();
}

static s32 CompareRecentFiles(const std::string& a, const std::string& b, RecentSort sort)
{
    switch (sort)
    {
    case RecentSort::Folder:
    {
        const s32 order = ComparePathText(GetRecentFolderName(a).c_str(), GetRecentFolderName(b).c_str());
        return order != 0 ? order : ComparePathText(GetPathFolder(a).c_str(), GetPathFolder(b).c_str());
    }

    case RecentSort::Modified:
    {
        const s64 timeA = GetRecentFileInfo(a).time;
        const s64 timeB = GetRecentFileInfo(b).time;
        return timeA < timeB ? -1 : timeA > timeB ? 1 : 0;
    }

    case RecentSort::Size:
    {
        const u64 bytesA = GetRecentFileInfo(a).bytes;
        const u64 bytesB = GetRecentFileInfo(b).bytes;
        return bytesA < bytesB ? -1 : bytesA > bytesB ? 1 : 0;
    }

    default:
        return 0;
    }
}

static void SortRecentFiles(std::vector<const std::string*>* rows, RecentSort sort, bool descending)
{
    if (sort == RecentSort::LastOpened)
        return;

    std::stable_sort(rows->begin(), rows->end(), [sort, descending](const std::string* a, const std::string* b)
    {
        s32 order = CompareRecentFiles(*a, *b, sort);

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

static bool DrawRecentRowButton(const char* id, const char* icon, bool lit, bool visible, const char* tooltip, const ImVec2& hitSize, f32 size)
{
    const ImVec2 hitPos = ImGui::GetCursorScreenPos();
    const ImVec2 pos = hitPos + ImFloor((hitSize - ImVec2(size, size)) * 0.5f);

    const bool pressed = ImGui::InvisibleButton(id, hitSize);
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

static bool IsRecentRowHeld()
{
    const ImGuiID activeId = ImGui::GetActiveID();

    return activeId != 0 && (activeId == ImGui::GetID("##row") || activeId == ImGui::GetID("##pin") || activeId == ImGui::GetID("##remove"));
}

static f32 CalcRecentNameOffset()
{
    return ImGui::CalcTextSize(ICON_LC_FILE_MUSIC " ").x;
}

static void DrawRecentFileName(const std::string& path, bool missing, bool grip, const ImVec2& pos, f32 right)
{
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec4 dimColor = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    const char* icon = missing ? ICON_LC_FILE_X : grip ? ICON_LC_GRIP_VERTICAL : ICON_LC_FILE_MUSIC;
    const f32 nameLeft = pos.x + CalcRecentNameOffset();

    drawList->AddText(pos, ImGui::GetColorU32(missing ? dimColor : GetItemValueColor()), icon);

    ImGui::PushStyleColor(ImGuiCol_Text, missing ? dimColor : ImGui::GetStyleColorVec4(ImGuiCol_Text));
    ImGui::RenderTextEllipsis(drawList, ImVec2(nameLeft, pos.y), ImVec2(right, pos.y + ImGui::GetFontSize()), right, right, GetPathFileName(path.c_str()), nullptr, nullptr);
    ImGui::PopStyleColor();
}

static void SetRecentFileTooltip(const std::string& path, bool missing)
{
    ImGui::SetTooltip(missing ? messages::recent::cNotFoundFormat : "%s", path.c_str());
}

static void SetRecentDateTooltip(const RecentFileInfo& info)
{
    char date[48];
    timeutil::FormatFullDate(date, sizeof(date), info.time);

    ImGui::SetTooltip("%s", date);
}

static RecentAction DragRecentRow(bool held, bool canDrag, f32 top, f32 bottom)
{
    if (!held || !ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        return RecentAction::None;

    sRecentDragMoved = true;

    if (!canDrag)
        return RecentAction::None;

    const f32 mouseY = ImGui::GetMousePos().y;

    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

    if (mouseY < top)
        return RecentAction::MoveUp;

    if (mouseY > bottom)
        return RecentAction::MoveDown;

    return RecentAction::None;
}

static RecentAction DrawRecentRowMenu(const std::string& path, bool pinned, bool missing)
{
    RecentAction action = RecentAction::None;

    if (!BeginContextMenu("##menu"))
        return action;

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

    return action;
}

static RecentAction DrawRecentRow(const std::string& path, bool pinned, bool missing, bool canDrag, f32 width, f32 nameWidth)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 em = ImGui::GetFontSize();
    const f32 buttonSize = CalcRecentRowHeight();
    const std::string folder = GetRecentFolderName(path);

    RecentAction action = RecentAction::None;
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    ImGui::PushID(path.c_str());

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 min(pos.x, pos.y - ImFloor(style.ItemSpacing.y * 0.5f));
    const ImVec2 max(pos.x + width, min.y + buttonSize);

    const bool rowHovered = IsRecentRowHeld() || ImGui::IsPopupOpen("##menu") || (ImGui::GetActiveID() == 0 && ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(min, max));

    ImDrawList* highlightList = BeginMenuHighlight();
    const bool pressed = ImGui::Selectable("##row", false, ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_DontClosePopups, ImVec2(width, 0.0f));
    const bool held = ImGui::IsItemActive();
    const bool detailHovered = IsItemHoveredForDetail();
    EndMenuHighlight(highlightList, rowHovered);

    ImGui::OpenPopupOnItemClick("##menu", ImGuiPopupFlags_MouseButtonRight);

    if (held && canDrag && sRecentDragMoved)
        drawList->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_CheckMark), style.FrameRounding);

    // Name and folder
    const f32 textY = pos.y;
    const f32 buttonsLeft = max.x - buttonSize * 2.0f;
    const f32 textRight = buttonsLeft + (pinned || !rowHovered ? buttonSize : 0.0f) - style.ItemSpacing.x;
    const f32 folderLeft = pos.x + CalcRecentNameOffset() + nameWidth + em * cRecentFolderGapEm;

    DrawRecentFileName(path, missing, canDrag && rowHovered, pos, sead::Mathf::min(folderLeft - style.ItemSpacing.x, textRight));

    if (folderLeft < textRight)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::RenderTextEllipsis(drawList, ImVec2(folderLeft, textY), ImVec2(textRight, textY + em), textRight, textRight, folder.c_str(), nullptr, nullptr);
        ImGui::PopStyleColor();
    }

    if (detailHovered)
        SetRecentFileTooltip(path, missing);

    action = DragRecentRow(held, canDrag, min.y, max.y);

    if (pressed && !sRecentDragMoved && !missing)
    {
        action = RecentAction::Open;
        ImGui::CloseCurrentPopup();
    }

    // Buttons
    if (!pinned)
    {
        ImGui::SetCursorScreenPos(ImVec2(buttonsLeft, min.y));
        if (DrawRecentRowButton("##remove", ICON_LC_X, false, rowHovered, messages::recent::cRemove, ImVec2(buttonSize, buttonSize), buttonSize))
            action = RecentAction::Remove;
    }

    ImGui::SetCursorScreenPos(ImVec2(buttonsLeft + buttonSize, min.y));
    if (DrawRecentRowButton("##pin", ICON_LC_PIN, pinned, pinned || rowHovered, pinned ? messages::recent::cUnpin : messages::recent::cPin, ImVec2(buttonSize, buttonSize), buttonSize))
        action = RecentAction::TogglePin;

    const RecentAction menuAction = DrawRecentRowMenu(path, pinned, missing);

    if (menuAction != RecentAction::None)
        action = menuAction;

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

    const f32 nameLeft = pos.x + CalcRecentNameOffset();
    const f32 folderLeft = nameLeft + nameWidth + em * cRecentFolderGapEm;
    const f32 folderCellLeft = folderLeft - ImFloor(style.ItemSpacing.x * 0.5f);

    DrawRecentHeaderCell("##sortName", messages::common::cName, RecentSort::Name, pos.x, nameLeft, folderCellLeft - style.ItemSpacing.x);
    DrawRecentHeaderCell("##sortFolder", messages::common::cFolder, RecentSort::Folder, folderCellLeft, folderLeft, pos.x + width);

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

static bool FinishRecentAction(RecentAction action, const std::string& path, std::string* outPath)
{
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        sRecentDragMoved = false;

    if (action == RecentAction::Open)
    {
        *outPath = path;
        return true;
    }

    ApplyRecentAction(action, path);

    return false;
}

bool DrawRecentFilesList(std::string* outPath)
{
    SEAD_ASSERT(outPath);

    std::vector<std::string>& pinned = GetPinnedFiles();
    std::vector<std::string>& recent = GetRecentFiles();

    if (ImGui::IsWindowAppearing())
        RefreshRecentFiles();

    if (pinned.empty() && recent.empty())
    {
        MenuBarItem(messages::recent::cEmpty, nullptr, false, false);
        return false;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 em = ImGui::GetFontSize();
    const f32 nameWidth = CalcRecentTextWidth([](const std::string& path) { return std::string(GetPathFileName(path.c_str())); }, em * cRecentNameMaxEm);
    const f32 folderWidth = CalcRecentTextWidth(GetRecentFolderName, em * cRecentFolderMaxEm);
    const f32 width = ImFloor(CalcRecentNameOffset() + nameWidth + em * cRecentFolderGapEm + folderWidth + style.ItemSpacing.x + CalcRecentRowHeight());

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

    if (!pinned.empty())
        ImGui::SeparatorText(messages::recent::cPinnedTitle);

    for (const std::string& path : pinned)
        drawRow(path, true);

    if (!pinned.empty() && !recent.empty())
        ImGui::SeparatorText(messages::common::cRecentFiles);

    if (!recent.empty())
    {
        std::vector<const std::string*> rows;
        for (const std::string& path : recent)
            rows.push_back(&path);

        SortRecentFiles(&rows, sRecentSort, sRecentSortDescending);

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

    return FinishRecentAction(action, actionPath, outPath);
}

static void DrawRecentTableEmpty()
{
    const f32 em = ImGui::GetFontSize();
    const ImVec2 start = ImGui::GetCursorPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 boxMin = ImGui::GetCursorScreenPos();
    const f32 gap = ImFloor(em * cRecentEmptyGapEm);

    ImGui::GetWindowDrawList()->AddRect(boxMin, boxMin + avail + ImVec2(1.0f, 0.0f), ImGui::GetColorU32(ImGuiCol_TableBorderStrong));
    ImGui::SetCursorPosY(start.y + ImFloor((avail.y - em * 2.0f - gap) * 0.5f));

    CenteredTextWrapped(messages::recent::cEmpty, avail.x);
    ImGui::Dummy(ImVec2(0.0f, sead::Mathf::max(gap - ImGui::GetStyle().ItemSpacing.y * 2.0f, 0.0f)));

    ImGui::SetCursorPosX(start.x);
    ImGui::PushStyleColor(ImGuiCol_Text, GetItemValueColor());
    CenteredTextWrapped(messages::start::cEmptyDetail, avail.x);
    ImGui::PopStyleColor();
}

static void DrawRecentTablePin(bool pinned, bool rowHovered, RecentAction* action)
{
    if (!ImGui::TableSetColumnIndex(static_cast<s32>(RecentColumn::Pin)))
        return;

    BeginItemActionCell();

    if (DrawRecentRowButton("##pin", ICON_LC_PIN, pinned, pinned || rowHovered, pinned ? messages::recent::cUnpin : messages::recent::cPin, GetItemActionCellSize(), GetRowButtonSize()))
        *action = RecentAction::TogglePin;

    EndItemActionCell();
}

static RecentAction DrawRecentTableRow(const std::string& path, bool pinned, bool missing, bool canDrag)
{
    const RecentFileInfo& info = GetRecentFileInfo(path);

    ImGui::PushID(path.c_str());
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(static_cast<s32>(RecentColumn::Name));
    ImGui::AlignTextToFramePadding();

    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const ImVec2 textPos(cursor.x, cursor.y + ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset);
    const f32 nameRight = cursor.x + ImGui::GetContentRegionAvail().x;
    const bool rowHovered = IsRecentRowHeld() || ImGui::IsPopupOpen("##menu") || (ImGui::GetActiveID() == 0 && ImGui::TableGetHoveredRow() == ImGui::TableGetRowIndex());

    bool selectableHovered = false;
    ImGui::SetNextItemAllowOverlap();
    const bool pressed = DrawTableRowSelectable("##row", false, &selectableHovered);
    const bool held = ImGui::IsItemActive();
    const bool detailHovered = IsItemHoveredForDetail();
    const ImVec2 rowMin = ImGui::GetItemRectMin();
    const ImVec2 rowMax = ImGui::GetItemRectMax();

    ImGui::OpenPopupOnItemClick("##menu", ImGuiPopupFlags_MouseButtonRight);

    DrawRecentFileName(path, missing, canDrag && rowHovered, textPos, nameRight);

    if (detailHovered)
    {
        if (ImGui::TableGetHoveredColumn() == static_cast<s32>(RecentColumn::Modified) && info.time != 0)
            SetRecentDateTooltip(info);
        else
            SetRecentFileTooltip(path, missing);
    }

    RecentAction action = DragRecentRow(held, canDrag, rowMin.y, rowMax.y);

    if (pressed && !sRecentDragMoved && !missing)
        action = RecentAction::Open;

    if (ImGui::TableSetColumnIndex(static_cast<s32>(RecentColumn::Folder)))
        DrawItemValueCell(info.folder.c_str(), cItemColumnFlag_Text | cItemColumnFlag_TextEnd);

    if (ImGui::TableSetColumnIndex(static_cast<s32>(RecentColumn::Modified)))
        DrawItemValueCell(FormatRecentDate(info).c_str(), cItemColumnFlag_Text);

    if (ImGui::TableSetColumnIndex(static_cast<s32>(RecentColumn::Size)))
        DrawItemValueCell(info.size.c_str(), cItemColumnFlag_None);

    DrawRecentTablePin(pinned, rowHovered, &action);

    const RecentAction menuAction = DrawRecentRowMenu(path, pinned, missing);

    if (menuAction != RecentAction::None)
        action = menuAction;

    ImGui::PopID();

    return action;
}

static RecentSort GetRecentTableSort(const ImGuiTableSortSpecs* specs, bool* outDescending)
{
    *outDescending = false;

    if (!specs || specs->SpecsCount == 0)
        return RecentSort::LastOpened;

    const ImGuiTableColumnSortSpecs& spec = specs->Specs[0];
    *outDescending = spec.SortDirection == ImGuiSortDirection_Descending;

    switch (static_cast<RecentColumn>(spec.ColumnIndex))
    {
    case RecentColumn::Name:
        return RecentSort::Name;

    case RecentColumn::Folder:
        return RecentSort::Folder;

    case RecentColumn::Modified:
        return RecentSort::Modified;

    case RecentColumn::Size:
        return RecentSort::Size;

    default:
        return RecentSort::LastOpened;
    }
}

static void CalcRecentTableWidths(const ItemColumn* columns, f32* widths, bool* shown)
{
    const f32 em = ImGui::GetFontSize();
    const f32 cell = ImGui::GetStyle().CellPadding.x * 2.0f + 1.0f;
    const f32 folderMin = em * cRecentTableFolderMinEm;

    const f32 nameMin = em * cRecentTableNameMinEm;
    const f32 nameNeed = CalcRecentNameOffset() + cell + em + CalcRecentTextWidth([](const std::string& path) { return std::string(GetPathFileName(path.c_str())); }, em * cRecentNameMaxEm);

    widths[1] = GetItemColumnWidth(columns[1]);
    widths[2] = GetItemColumnWidth(columns[2]);
    widths[3] = GetItemActionColumnWidth();

    f32 rest = ImGui::GetContentRegionAvail().x - cell - nameMin - (widths[3] + cell);

    shown[3] = true;

    for (s32 i : { 1, 2 })
    {
        shown[i] = rest >= widths[i] + cell;

        if (shown[i])
            rest -= widths[i] + cell;
    }

    const f32 folderRoom = rest - cell - sead::Mathf::max(nameNeed - nameMin, 0.0f);

    shown[0] = folderRoom >= folderMin;
    widths[0] = sead::Mathf::max(folderMin, sead::Mathf::min(GetItemColumnWidth(columns[0]), folderRoom));
}

static bool BeginRecentTable(const char* id, bool pinned, ItemColumn* columns, std::string* samples, f32* widths)
{
    for (const std::vector<std::string>* files : { &GetPinnedFiles(), &GetRecentFiles() })
    {
        for (const std::string& path : *files)
            GetRecentFileInfo(path);
    }

    for (const RecentFileInfo& info : GetRecentFileInfos())
    {
        const std::string texts[3] = { info.folder, FormatRecentDate(info), info.size };

        for (s32 i = 0; i < 3; i++)
        {
            if (ImGui::CalcTextSize(texts[i].c_str()).x > ImGui::CalcTextSize(samples[i].c_str()).x)
                samples[i] = texts[i];
        }
    }

    columns[0] = { "Folder", messages::common::cFolder, samples[0].c_str(), nullptr, nullptr, cItemColumnFlag_Text | cItemColumnFlag_TextEnd, nullptr };
    columns[1] = { "Modified", messages::common::cModified, samples[1].c_str(), nullptr, nullptr, cItemColumnFlag_Text, nullptr };
    columns[2] = { "Size", messages::common::cSize, samples[2].c_str(), nullptr, nullptr, cItemColumnFlag_None, nullptr };
    columns[3] = { "Pin", "", "", nullptr, nullptr, cItemColumnFlag_None, nullptr };

    bool shown[4];
    CalcRecentTableWidths(columns, widths, shown);

    auto columnFlags = [&shown](s32 index, ImGuiTableColumnFlags flags)
    {
        return shown[index] ? flags : flags | ImGuiTableColumnFlags_Disabled;
    };

    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_SizingFixedFit |
                                  (pinned ? ImGuiTableFlags_None : ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32_BLACK_TRANS);
    const bool open = ImGui::BeginTable(id, static_cast<s32>(RecentColumn::Num), flags);
    ImGui::PopStyleColor();

    if (!open)
        return false;

    ImGui::TableSetupColumn(messages::common::cName, ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoHide, 0.0f, static_cast<ImGuiID>(ItemColumnId::Name));
    ImGui::TableSetupColumn(columns[0].title, columnFlags(0, ImGuiTableColumnFlags_WidthFixed), widths[0]);
    ImGui::TableSetupColumn(columns[1].title, columnFlags(1, ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending), widths[1]);
    ImGui::TableSetupColumn(columns[2].title, columnFlags(2, ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending), widths[2]);
    SetupItemActionColumn();

    if (!pinned)
        ImGui::TableSetupScrollFreeze(0, 1);

    return true;
}

static void RefreshRecentTables()
{
    if (ImGui::GetFrameCount() > sRecentTableFrame + 1)
        RefreshRecentFiles();

    sRecentTableFrame = ImGui::GetFrameCount();
}

static bool DrawRecentFileTable(const char* id, bool pinned, std::string* outPath)
{
    SEAD_ASSERT(outPath);

    const std::vector<std::string>& files = pinned ? GetPinnedFiles() : GetRecentFiles();
    ItemColumn columns[4];
    std::string samples[3];
    f32 widths[4];

    PushItemTablePadding();

    if (!BeginRecentTable(id, pinned, columns, samples, widths))
    {
        ImGui::PopStyleVar();
        return false;
    }

    DrawItemHeadersRow(columns, 4, widths);

    bool descending = false;
    const RecentSort sort = pinned ? RecentSort::LastOpened : GetRecentTableSort(ImGui::TableGetSortSpecs(), &descending);

    std::vector<const std::string*> rows;
    for (const std::string& path : files)
        rows.push_back(&path);

    SortRecentFiles(&rows, sort, descending);

    RecentAction action = RecentAction::None;
    std::string actionPath;

    for (const std::string* path : rows)
    {
        const bool missing = pinned && ContainsPath(GetMissingPinnedFiles(), *path);
        const RecentAction rowAction = DrawRecentTableRow(*path, pinned, missing, pinned && files.size() > 1);

        if (rowAction == RecentAction::None)
            continue;

        action = rowAction;
        actionPath = *path;
    }

    ImGui::EndTable();
    ImGui::PopStyleVar();

    return FinishRecentAction(action, actionPath, outPath);
}

bool HasPinnedFiles()
{
    RefreshRecentTables();

    return !GetPinnedFiles().empty();
}

bool DrawPinnedFilesTable(std::string* outPath)
{
    return DrawRecentFileTable("##PinnedFiles", true, outPath);
}

bool DrawRecentFilesTable(std::string* outPath)
{
    RefreshRecentTables();

    if (GetRecentFiles().empty())
    {
        DrawRecentTableEmpty();
        return false;
    }

    return DrawRecentFileTable("##RecentFiles", false, outPath);
}
