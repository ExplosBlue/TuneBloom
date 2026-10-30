#include <ui/file/RecentFiles.h>

#include <bfsar/io/FileIo.h>
#include <bfsar/io/PackFile.h>

#include <ui/settings/Config.h>
#include <ui/settings/ConfigFile.h>
#include <ui/widgets/ContextMenu.h>
#include <ui/file/FileDrag.h>
#include <ui/items/ItemColumns.h>
#include <ui/app/MenuBar.h>
#include <messages/Messages.h>
#include <ui/file/PathText.h>
#include <core/TestHooks.h>
#include <ui/file/SystemShell.h>
#include <ui/widgets/TimeUtil.h>
#include <ui/widgets/Widgets.h>
#include <ui/settings/UiColors.h>

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
    Remove
};

static constexpr const char* cRecentTableId = "##RecentFiles";

static constexpr s32   cRecentVisibleRows  = 12;
static constexpr RecentSort cRecentMenuSortMax = RecentSort::Folder;

static constexpr f32    cRecentTableFolderMinEm  = 6.0f;
static constexpr f32    cRecentTableNameMinEm    = 8.0f;
static constexpr f32    cRecentTableNameMaxShare = 0.5f;
static constexpr f32    cRecentGroupGapEm        = 0.5f;
static constexpr f32    cRecentEmptyGapEm        = 0.3f;

static constexpr f32    cRecentNameMaxEm    = 16.0f;
static constexpr f32    cRecentFolderMaxEm  = 7.0f;
static constexpr f32    cRecentFolderGapEm  = 1.0f;

static constexpr const char* cFilesSection = "files";
static constexpr const char* cRecentKey    = "recent";
static constexpr const char* cPinnedKey    = "pinned";

static RecentSort sRecentSort = RecentSort::LastOpened;
static bool sRecentSortDescending = false;
static bool sRecentInfoDirty = true;
static s32 sRecentTableFrame = -1;

std::vector<std::string>& GetRecentFiles()
{
    static std::vector<std::string>* files = new std::vector<std::string>();
    return *files;
}

static std::vector<std::string>& GetLegacyPinnedFiles()
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

static bool IsFileMissing(const std::string& path)
{
    return !packfile::IsOnDisk(path);
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

        const std::string file = fileio::NormalizePath(item.string.c_str());

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

    std::vector<std::string>& recent = GetRecentFiles();
    std::vector<std::string>& pinned = GetLegacyPinnedFiles();

    ReadPathList(section, cRecentKey, &recent);
    ReadPathList(section, cPinnedKey, &pinned);

    for (auto it = pinned.rbegin(); it != pinned.rend(); ++it)
    {
        if (packfile::IsPackPath(*it) && !ContainsPath(recent, *it))
            recent.insert(recent.begin(), *it);
    }

    std::erase_if(pinned, [](const std::string& path) { return packfile::IsPackPath(path); });

    RefreshRecentFiles();
}

std::vector<std::string> TakeLegacyPinnedFiles()
{
    std::vector<std::string> files;
    files.swap(GetLegacyPinnedFiles());

    if (!files.empty())
        SaveRecentFiles();

    return files;
}

void AddRecentFile(const char* path)
{
    if (testhooks::IsEnabled())
        return;

    const std::string file = fileio::NormalizePath(path);

    if (file.empty())
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

static std::string GetRecentFolderName(const std::string& path)
{
    const std::string folder = pathtext::Folder(path);

    return pathtext::FileName(folder.c_str());
}

static std::vector<RecentFileInfo>& GetRecentFileInfos()
{
    static std::vector<RecentFileInfo>* infos = new std::vector<RecentFileInfo>();
    return *infos;
}

static RecentFileInfo ReadRecentFileInfo(const std::string& path)
{
    namespace fs = std::filesystem;

    RecentFileInfo info = { path, pathtext::Folder(path), std::string(), 0, 0 };

    std::error_code ec;
    const fs::path file(packfile::GetDiskPath(path));
    const fs::file_time_type writeTime = fs::last_write_time(file, ec);

    if (!ec)
    {
        const auto systemTime = std::chrono::system_clock::now() + std::chrono::duration_cast<std::chrono::system_clock::duration>(writeTime - fs::file_time_type::clock::now());
        info.time = static_cast<s64>(std::chrono::system_clock::to_time_t(systemTime));
    }

    u64 bytes = 0;

    if (packfile::GetFileSize(path, &bytes))
    {
        info.bytes = bytes;

        sead::FixedSafeString<32> size;
        FormatItemSize(&size, info.bytes);
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
        return order != 0 ? order : ComparePathText(pathtext::Folder(a).c_str(), pathtext::Folder(b).c_str());
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
            order = ComparePathText(pathtext::FileName(a->c_str()), pathtext::FileName(b->c_str()));

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

static bool IsRecentRowHeld()
{
    const ImGuiID activeId = ImGui::GetActiveID();

    return activeId != 0 && (activeId == ImGui::GetID("##row") || activeId == ImGui::GetID("##remove"));
}

static f32 CalcRecentNameOffset()
{
    return ImGui::CalcTextSize(ICON_LC_FILE_MUSIC " ").x;
}

static void DrawRecentFileName(const std::string& path, const ImVec2& pos, f32 right)
{
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const f32 nameLeft = pos.x + CalcRecentNameOffset();

    drawList->AddText(pos, ImGui::GetColorU32(GetItemValueColor()), ICON_LC_FILE_MUSIC);
    ImGui::RenderTextEllipsis(drawList, ImVec2(nameLeft, pos.y), ImVec2(right, pos.y + ImGui::GetFontSize()), right, right, pathtext::FileName(path.c_str()), nullptr, nullptr);
}

static void DrawRecentDragSource(const std::string& path)
{
    FileDragSource([&path]()
    {
        FileDrag drag;
        drag.paths.push_back(path);
        drag.name = pathtext::FileName(path.c_str());
        drag.icon = ICON_LC_FILE_MUSIC;
        drag.iconColor = ImGui::GetColorU32(GetItemValueColor());
        drag.origin = FileDragOrigin::Recent;

        return drag;
    });
}

static void SetRecentFileTooltip(const std::string& path)
{
    if (!ImGui::BeginTooltip())
        return;

    const char* name = pathtext::FileName(path.c_str());

    ImGui::TextUnformatted(name);
    pathtext::DrawTooltipFolder(path, ImGui::CalcTextSize(name).x);

    ImGui::EndTooltip();
}

static void SetRecentDateTooltip(const RecentFileInfo& info)
{
    char date[48];
    timeutil::FormatFullDate(date, sizeof(date), info.time);

    ImGui::SetTooltip("%s", date);
}

static RecentAction DrawRecentRowMenu(const std::string& path)
{
    RecentAction action = RecentAction::None;

    if (!BeginContextMenu("##menu"))
        return action;

    if (ContextMenuItem(ICON_LC_FOLDER_OPEN, messages::recent::cShowInFileManager))
        ShowInFileManager(path);

    if (ContextMenuItem(ICON_LC_COPY, messages::mainmenu::cCopyPath))
        ImGui::SetClipboardText(path.c_str());

    ContextMenuSeparator();

    if (ContextMenuItem(ICON_LC_X, messages::common::cRemoveFromList))
        action = RecentAction::Remove;

    EndContextMenu();

    return action;
}

static RecentAction DrawRecentRow(const std::string& path, f32 width, f32 nameWidth)
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
    const bool detailHovered = IsItemHoveredForDetail();
    EndMenuHighlight(highlightList, rowHovered);

    ImGui::OpenPopupOnItemClick("##menu", ImGuiPopupFlags_MouseButtonRight);

    // Name and folder
    const f32 textY = pos.y;
    const f32 buttonLeft = max.x - buttonSize;
    const f32 textRight = buttonLeft - style.ItemSpacing.x;
    const f32 folderLeft = pos.x + CalcRecentNameOffset() + nameWidth + em * cRecentFolderGapEm;

    DrawRecentFileName(path, pos, sead::Mathf::min(folderLeft - style.ItemSpacing.x, textRight));

    if (folderLeft < textRight)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::RenderTextEllipsis(drawList, ImVec2(folderLeft, textY), ImVec2(textRight, textY + em), textRight, textRight, folder.c_str(), nullptr, nullptr);
        ImGui::PopStyleColor();
    }

    if (detailHovered)
        SetRecentFileTooltip(path);

    if (pressed)
    {
        action = RecentAction::Open;
        ImGui::CloseCurrentPopup();
    }

    // Buttons
    ImGui::SetCursorScreenPos(ImVec2(buttonLeft, min.y));
    if (DrawRowHoverButton("##remove", ICON_LC_X, rowHovered, messages::common::cRemoveFromList, ImVec2(buttonSize, buttonSize), buttonSize))
        action = RecentAction::Remove;

    const RecentAction menuAction = DrawRecentRowMenu(path);

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
        DrawItemSortMark(drawList, ImVec2(textLeft + ImGui::CalcTextSize(title).x + GetItemSortMarkGap(), pos.y), sRecentSortDescending, ImGui::GetColorU32(GetItemValueColor()));

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

    for (const std::string& path : GetRecentFiles())
        width = sead::Mathf::max(width, ImGui::CalcTextSize(getText(path).c_str()).x);

    return sead::Mathf::min(width, maxWidth);
}

static bool FinishRecentAction(RecentAction action, const std::string& path, std::string* outPath)
{
    if (action == RecentAction::Open)
    {
        *outPath = path;
        return true;
    }

    if (action == RecentAction::Remove)
    {
        RemovePath(&GetRecentFiles(), path);
        SaveRecentFiles();
    }

    return false;
}

static bool DrawRecentEmptyItem(const std::vector<std::string>& recent)
{
    if (!recent.empty())
        return false;

    MenuBarItem(messages::recent::cEmpty, std::nullopt, false, false);
    return true;
}

static void DrawRecentClearItem(std::vector<std::string>* recent)
{
    MenuSeparator();

    if (!MenuBarIconItem(ICON_LC_BRUSH_CLEANING, messages::recent::cClear, std::nullopt, !recent->empty()))
        return;

    recent->clear();
    SaveRecentFiles();
}

bool DrawRecentFilesList(std::string* outPath)
{
    SEAD_ASSERT(outPath);

    std::vector<std::string>& recent = GetRecentFiles();

    if (ImGui::IsWindowAppearing())
        RefreshRecentFiles();

    if (DrawRecentEmptyItem(recent))
        return false;

    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 em = ImGui::GetFontSize();
    const f32 nameWidth = CalcRecentTextWidth([](const std::string& path) { return std::string(pathtext::FileName(path.c_str())); }, em * cRecentNameMaxEm);
    const f32 folderWidth = CalcRecentTextWidth(GetRecentFolderName, em * cRecentFolderMaxEm);
    const f32 width = ImFloor(CalcRecentNameOffset() + nameWidth + em * cRecentFolderGapEm + folderWidth + style.ItemSpacing.x + CalcRecentRowHeight());

    RecentAction action = RecentAction::None;
    std::string actionPath;

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
        ImGui::PushStyleColor(ImGuiCol_ChildBg, uicolors::cTransparent);
        ImGui::BeginChild("##recent", ImVec2(width + inset * 2.0f + style.ScrollbarSize, cRecentVisibleRows * CalcRecentRowHeight()));
        ImGui::SetCursorPos(ImVec2(inset, ImFloor(style.ItemSpacing.y * 0.5f)));
        ImGui::Indent(inset);
    }

    for (const std::string* path : rows)
    {
        const RecentAction rowAction = DrawRecentRow(*path, width, nameWidth);

        if (rowAction == RecentAction::None)
            continue;

        action = rowAction;
        actionPath = *path;
    }

    if (scroll)
    {
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    DrawRecentClearItem(&recent);

    return FinishRecentAction(action, actionPath, outPath);
}

bool VisitRecentFilesList(std::string* outPath)
{
    SEAD_ASSERT(outPath);

    std::vector<std::string>& recent = GetRecentFiles();

    if (DrawRecentEmptyItem(recent))
        return false;

    bool chosen = false;

    for (const std::string& path : recent)
    {
        if (!VisitMenuFileItem(path.c_str()))
            continue;

        *outPath = path;
        chosen = true;
    }

    DrawRecentClearItem(&recent);

    return chosen;
}

static void DrawRecentTableEmpty()
{
    const f32 em = ImGui::GetFontSize();
    const ImVec2 start = ImGui::GetCursorPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 boxMin = ImGui::GetCursorScreenPos();
    const f32 gap = ImFloor(em * cRecentEmptyGapEm);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(boxMin, boxMin + avail, ImGui::GetColorU32(ImGuiCol_ChildBg));
    drawList->AddRect(boxMin, boxMin + avail + ImVec2(1.0f, 0.0f), ImGui::GetColorU32(ImGuiCol_TableBorderStrong));
    ImGui::SetCursorPosY(start.y + ImFloor((avail.y - em * 2.0f - gap) * 0.5f));

    CenteredTextWrapped(messages::recent::cEmpty, avail.x);
    ImGui::Dummy(ImVec2(0.0f, sead::Mathf::max(gap - ImGui::GetStyle().ItemSpacing.y * 2.0f, 0.0f)));

    ImGui::SetCursorPosX(start.x);
    ImGui::PushStyleColor(ImGuiCol_Text, GetItemValueColor());
    CenteredTextWrapped(messages::start::cEmptyDetail, avail.x);
    ImGui::PopStyleColor();
}

static RecentAction DrawRecentTableRow(const std::string& path)
{
    const RecentFileInfo& info = GetRecentFileInfo(path);

    ImGui::PushID(path.c_str());
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(static_cast<s32>(RecentColumn::Name));
    ImGui::AlignTextToFramePadding();

    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const ImVec2 textPos(cursor.x, cursor.y + ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset);
    const f32 nameRight = cursor.x + ImGui::GetContentRegionAvail().x;

    bool selectableHovered = false;
    const bool pressed = DrawTableRowSelectable("##row", false, &selectableHovered);
    const bool detailHovered = IsItemHoveredForDetail();

    DrawRecentDragSource(path);

    ImGui::OpenPopupOnItemClick("##menu", ImGuiPopupFlags_MouseButtonRight);

    DrawRecentFileName(path, textPos, nameRight);

    if (detailHovered)
    {
        if (ImGui::TableGetHoveredColumn() == static_cast<s32>(RecentColumn::Modified) && info.time != 0)
            SetRecentDateTooltip(info);
        else
            SetRecentFileTooltip(path);
    }

    RecentAction action = pressed ? RecentAction::Open : RecentAction::None;

    if (ImGui::TableSetColumnIndex(static_cast<s32>(RecentColumn::Folder)))
        DrawItemValueCell(info.folder.c_str(), cItemColumnFlag_Text | cItemColumnFlag_TextEnd);

    if (ImGui::TableSetColumnIndex(static_cast<s32>(RecentColumn::Modified)))
        DrawItemValueCell(FormatRecentDate(info).c_str(), cItemColumnFlag_Text);

    if (ImGui::TableSetColumnIndex(static_cast<s32>(RecentColumn::Size)))
        DrawItemValueCell(info.size.c_str(), cItemColumnFlag_None);

    const RecentAction menuAction = DrawRecentRowMenu(path);

    if (menuAction != RecentAction::None)
        action = menuAction;

    ImGui::PopID();

    return action;
}

static const char* GetRecentDateGroup(const RecentFileInfo& info)
{
    if (info.time == 0)
        return messages::recent::cGroupOlder;

    const s32 days = timeutil::CalcDaysAgo(info.time);

    if (days <= 0)
        return messages::recent::cGroupToday;

    if (days < 7)
        return messages::recent::cGroupWeek;

    if (days < 30)
        return messages::recent::cGroupMonth;

    return messages::recent::cGroupOlder;
}

static void DrawRecentGroupRow(const char* title)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(static_cast<s32>(RecentColumn::Name));
    DrawTableGroupRow(title, ImFloor(ImGui::GetFontSize() * cRecentGroupGapEm));
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

static f32 CalcRecentScrollbarWidth()
{
    const ImGuiTable* table = ImGui::TableFindByID(ImGui::GetID(cRecentTableId));

    return table && table->InnerWindow && !table->InnerWindow->ScrollbarY ? 0.0f : ImGui::GetStyle().ScrollbarSize;
}

static void CalcRecentTableWidths(const ItemColumn* columns, f32* widths, bool* shown)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 em = ImGui::GetFontSize();
    const f32 cell = style.CellPadding.x * 2.0f + 1.0f;
    const f32 avail = ImGui::GetContentRegionAvail().x - CalcRecentScrollbarWidth();
    const f32 folderMin = em * cRecentTableFolderMinEm;

    const f32 nameMin = em * cRecentTableNameMinEm;
    const f32 nameText = CalcRecentTextWidth([](const std::string& path) { return std::string(pathtext::FileName(path.c_str())); }, avail * cRecentTableNameMaxShare);
    const f32 nameNeed = CalcRecentNameOffset() + style.ItemSpacing.x + nameText;

    widths[1] = GetItemColumnWidth(columns[1]);
    widths[2] = GetItemColumnWidth(columns[2]);

    f32 rest = avail - cell - nameMin;

    for (s32 i : { 1, 2 })
    {
        shown[i] = rest >= widths[i] + cell;

        if (shown[i])
            rest -= widths[i] + cell;
    }

    const f32 folderRoom = rest - cell - sead::Mathf::max(nameNeed - nameMin, 0.0f);

    shown[0] = folderRoom >= folderMin;
    widths[0] = sead::Mathf::max(folderMin, folderRoom);
}

static bool BeginRecentTable(ItemColumn* columns, std::string* samples, f32* widths)
{
    for (const std::string& path : GetRecentFiles())
        GetRecentFileInfo(path);

    for (const RecentFileInfo& info : GetRecentFileInfos())
    {
        const std::string texts[2] = { FormatRecentDate(info), info.size };

        for (s32 i = 0; i < 2; i++)
        {
            if (ImGui::CalcTextSize(texts[i].c_str()).x > ImGui::CalcTextSize(samples[i].c_str()).x)
                samples[i] = texts[i];
        }
    }

    columns[0] = { "Folder", messages::common::cFolder, "", nullptr, nullptr, cItemColumnFlag_Text | cItemColumnFlag_TextEnd, nullptr };
    columns[1] = { "Modified", messages::common::cModified, samples[0].c_str(), nullptr, nullptr, cItemColumnFlag_Text, nullptr };
    columns[2] = { "Size", messages::common::cSize, samples[1].c_str(), nullptr, nullptr, cItemColumnFlag_None, nullptr };

    bool shown[3];
    CalcRecentTableWidths(columns, widths, shown);

    auto columnFlags = [&shown](s32 index, ImGuiTableColumnFlags flags)
    {
        return shown[index] ? flags : flags | ImGuiTableColumnFlags_Disabled;
    };

    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_SizingFixedFit |
                                  ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate;

    if (!ImGui::BeginTable(cRecentTableId, static_cast<s32>(RecentColumn::Num), flags))
        return false;

    ImGui::TableSetupColumn(messages::common::cName, ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoHide, 0.0f, static_cast<ImGuiID>(ItemColumnId::Name));
    ImGui::TableSetupColumn(columns[0].title, columnFlags(0, ImGuiTableColumnFlags_WidthFixed), widths[0]);
    ImGui::TableSetupColumn(columns[1].title, columnFlags(1, ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_DefaultSort), widths[1]);
    ImGui::TableSetupColumn(columns[2].title, columnFlags(2, ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending), widths[2]);
    ImGui::TableSetupScrollFreeze(0, 1);

    return true;
}

static void RefreshRecentTables()
{
    if (ImGui::GetFrameCount() > sRecentTableFrame + 1)
        RefreshRecentFiles();

    sRecentTableFrame = ImGui::GetFrameCount();
}

bool DrawRecentFilesTable(std::string* outPath)
{
    SEAD_ASSERT(outPath);

    RefreshRecentTables();

    if (GetRecentFiles().empty())
    {
        DrawRecentTableEmpty();
        return false;
    }

    ItemColumn columns[3];
    std::string samples[2];
    f32 widths[3];

    PushItemTablePadding();

    if (!BeginRecentTable(columns, samples, widths))
    {
        ImGui::PopStyleVar();
        return false;
    }

    DrawItemHeadersRow(columns, 3, widths);

    bool descending = false;
    const RecentSort sort = GetRecentTableSort(ImGui::TableGetSortSpecs(), &descending);

    std::vector<const std::string*> rows;
    for (const std::string& path : GetRecentFiles())
        rows.push_back(&path);

    SortRecentFiles(&rows, sort, descending);

    RecentAction action = RecentAction::None;
    std::string actionPath;
    const char* group = nullptr;

    for (const std::string* path : rows)
    {
        if (sort == RecentSort::Modified)
        {
            const char* rowGroup = GetRecentDateGroup(GetRecentFileInfo(*path));

            if (rowGroup != group)
                DrawRecentGroupRow(rowGroup);

            group = rowGroup;
        }

        const RecentAction rowAction = DrawRecentTableRow(*path);

        if (rowAction == RecentAction::None)
            continue;

        action = rowAction;
        actionPath = *path;
    }

    ImGui::EndTable();
    ImGui::PopStyleVar();

    return FinishRecentAction(action, actionPath, outPath);
}
