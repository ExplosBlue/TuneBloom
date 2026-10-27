#include <ui/browser/FilesPanelInternal.h>

#include <ui/file/FileDialogs.h>
#include <ui/file/SystemShell.h>
#include <ui/items/ItemColumns.h>
#include <ui/settings/ConfigFile.h>
#include <ui/settings/Theme.h>
#include <ui/widgets/ContextMenu.h>
#include <ui/widgets/Widgets.h>
#include <ui/Messages.h>

#include <bfsar/io/FileIo.h>
#include <core/Json.h>
#include <libs/IconsLucide.h>
#include <imgui/imgui_custom.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static constexpr s32 cRecentRootMax    = 8;
static constexpr f32 cSpinnerRadiusEm  = 0.3f;
static constexpr f32 cProgressHeightEm = 0.3f;
static constexpr f32 cProgressTopEm    = 0.15f;

static constexpr const char* cConfigSection  = "browser";
static constexpr const char* cRootsKey       = "roots";
static constexpr const char* cOldRootKey     = "root";
static constexpr const char* cPathKey        = "path";
static constexpr const char* cPinnedKey      = "pinned";
static constexpr const char* cOpenKey        = "open";
static constexpr const char* cOpenFoldersKey = "openFolders";
static constexpr const char* cOpenPrefixKey  = "openPrefixes";
static constexpr const char* cAutoPlayKey    = "autoPlay";
static constexpr const char* cGroupPrefixKey = "groupByPrefix";
static constexpr const char* cCacheFolder    = "browser";
static constexpr const char* cCacheExtension = ".cache";
static constexpr const char* cRootMenuId     = "##rootmenu";

static std::string MakeCachePath(const std::string& root)
{
    const sead::FormatFixedSafeString<32> name("%016llx%s", static_cast<unsigned long long>(std::hash<std::string>()(root)), cCacheExtension);

    return (fs::path(GetConfigDir()) / cCacheFolder / name.cstr()).string();
}

static s32 GetRootButtonCount(const FilesRoot& root)
{
    return root.isPinned ? 2 : 3;
}

static std::string MakeRootName(const std::string& path)
{
    const std::string name = fs::path(path).filename().string();

    return name.empty() ? path : name;
}

static bool IsSameName(const std::string& left, const std::string& right)
{
    return std::equal(left.begin(), left.end(), right.begin(), right.end(), [](char a, char b)
    {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
}

static bool IsScanFirst(const FilesRoot& left, const FilesRoot& right)
{
    if (left.isOpen != right.isOpen)
        return left.isOpen;

    return left.scanRequest > right.scanRequest;
}

static std::string MakeFolderKey(const FileIndexSnapshot& snapshot, s32 folder)
{
    std::string key;

    for (s32 entry = folder; entry > 0; entry = snapshot.folders[entry].parent)
        key.insert(0, key.empty() ? snapshot.folders[entry].name : snapshot.folders[entry].name + '/');

    return key;
}

static std::vector<std::string> CollectOpenFolders(const FileIndexSnapshot& snapshot, const std::vector<u8>& expanded)
{
    std::vector<std::string> openFolders;

    for (s32 folder = 1; folder < static_cast<s32>(expanded.size()) && folder < static_cast<s32>(snapshot.folders.size()); folder++)
    {
        if (expanded[folder])
            openFolders.push_back(MakeFolderKey(snapshot, folder));
    }

    return openFolders;
}

void FilesPanel::loadConfig()
{
    json::Value section;
    LoadConfigSection(cStateFile, cConfigSection, &section);

    mAutoPlay = section.getBool(cAutoPlayKey, mAutoPlay);
    mGroupByPrefix = section.getBool(cGroupPrefixKey, mGroupByPrefix);

    if (const json::Value* roots = section.findArray(cRootsKey))
    {
        for (const json::Value& entry : roots->items)
        {
            const std::string* path = entry.findString(cPathKey);

            if (!path || path->empty() || findRoot(*path) >= 0)
                continue;

            FilesRoot root;
            root.path = *path;
            root.name = MakeRootName(*path);
            root.isPinned = entry.getBool(cPinnedKey, false);
            root.isOpen = entry.getBool(cOpenKey, true);

            if (const json::Value* openFolders = entry.findArray(cOpenFoldersKey))
            {
                root.savedOpenFolders.emplace();

                for (const json::Value& folder : openFolders->items)
                {
                    if (folder.isString())
                        root.savedOpenFolders->push_back(folder.string);
                }
            }

            if (const json::Value* openPrefixes = entry.findArray(cOpenPrefixKey))
            {
                for (const json::Value& prefix : openPrefixes->items)
                {
                    if (prefix.isString())
                        root.openPrefixes.push_back(prefix.string);
                }
            }

            mRoots.push_back(std::move(root));
        }
    }
    else if (const std::string* oldRoot = section.findString(cOldRootKey); oldRoot && !oldRoot->empty())
    {
        FilesRoot root;
        root.path = *oldRoot;
        root.name = MakeRootName(*oldRoot);
        mRoots.push_back(std::move(root));
    }

    std::stable_partition(mRoots.begin(), mRoots.end(), [](const FilesRoot& root) { return root.isPinned; });

    for (s32 i = static_cast<s32>(mRoots.size()) - 1; i >= 0; i--)
        requestScan(i);

    onRootsChanged();
}

void FilesPanel::saveConfig() const
{
    json::Value section;
    json::Value& roots = section.setArray(cRootsKey);

    for (const FilesRoot& root : mRoots)
    {
        json::Value& entry = roots.pushObject();
        entry.setString(cPathKey, root.path.c_str());
        entry.setBool(cPinnedKey, root.isPinned);
        entry.setBool(cOpenKey, root.isOpen);

        if (!root.openPrefixes.empty())
        {
            json::Value& openPrefixes = entry.setArray(cOpenPrefixKey);

            for (const std::string& prefix : root.openPrefixes)
                openPrefixes.pushString(prefix.c_str());
        }

        if (!root.hasSnapshot && !root.savedOpenFolders)
            continue;

        json::Value& openFolders = entry.setArray(cOpenFoldersKey);

        for (const std::string& folder : root.hasSnapshot ? CollectOpenFolders(root.index->getSnapshot(), root.expanded) : *root.savedOpenFolders)
            openFolders.pushString(folder.c_str());
    }

    section.setBool(cAutoPlayKey, mAutoPlay);
    section.setBool(cGroupPrefixKey, mGroupByPrefix);

    SaveConfigSection(cStateFile, cConfigSection, section);
}

s32 FilesPanel::findRoot(const std::string& path) const
{
    for (s32 i = 0; i < static_cast<s32>(mRoots.size()); i++)
    {
        if (mRoots[i].path == path)
            return i;
    }

    return -1;
}

s32 FilesPanel::insertRoot(FilesRoot root)
{
    const auto firstRecent = std::find_if(mRoots.begin(), mRoots.end(), [](const FilesRoot& entry) { return !entry.isPinned; });
    const s32 index = static_cast<s32>(firstRecent - mRoots.begin());

    mRoots.insert(firstRecent, std::move(root));

    return index;
}

s32 FilesPanel::addRoot(const std::string& path, bool isPinned)
{
    FilesRoot root;
    root.path = path;
    root.name = MakeRootName(path);
    root.isPinned = isPinned;

    const s32 index = insertRoot(std::move(root));
    requestScan(index);

    return index;
}

void FilesPanel::trimRecentRoots()
{
    s32 recentCount = static_cast<s32>(std::count_if(mRoots.begin(), mRoots.end(), [](const FilesRoot& root) { return !root.isPinned; }));

    while (recentCount > cRecentRootMax)
    {
        auto oldest = std::find_if(mRoots.rbegin(), mRoots.rend(), [](const FilesRoot& root) { return !root.isPinned && !root.isOpen; });

        if (oldest == mRoots.rend())
            oldest = mRoots.rbegin();

        mRoots.erase(std::next(oldest).base());
        recentCount--;
    }
}

void FilesPanel::onRootsChanged()
{
    mSelected = FilesRow();
    mSnapshotGeneration++;

    for (FilesRoot& root : mRoots)
    {
        root.note.clear();

        for (const FilesRoot& other : mRoots)
        {
            if (&other != &root && IsSameName(other.name, root.name))
                root.note = MakeRootName(fs::path(root.path).parent_path().string());
        }
    }
}

void FilesPanel::selectRoot(s32 root)
{
    mSelected = FilesRow { .kind = FilesRowKind::Root, .root = root };
    mScrollToSelected = true;
}

void FilesPanel::openFolder(const std::string& path, bool isPinned)
{
    const std::string canonical = fileio::CanonicalPath(path.c_str());

    if (canonical.empty())
        return;

    s32 index = findRoot(canonical);

    if (index < 0)
    {
        index = addRoot(canonical, isPinned);
        trimRecentRoots();
        onRootsChanged();
    }
    else if (isPinned && !mRoots[index].isPinned)
        index = togglePin(index);

    mRoots[index].isOpen = true;
    invalidateRows();
    selectRoot(index);
    saveConfig();
}

s32 FilesPanel::togglePin(s32 index)
{
    FilesRoot root = std::move(mRoots[index]);
    mRoots.erase(mRoots.begin() + index);
    root.isPinned = !root.isPinned;

    const s32 moved = insertRoot(std::move(root));

    onRootsChanged();
    selectRoot(moved);
    saveConfig();

    return moved;
}

void FilesPanel::moveRoot(s32 index, s32 offset)
{
    const s32 target = index + offset;

    if (target < 0 || target >= static_cast<s32>(mRoots.size()) || mRoots[target].isPinned != mRoots[index].isPinned)
        return;

    std::swap(mRoots[index], mRoots[target]);

    onRootsChanged();
    selectRoot(target);
    saveConfig();
}

bool FilesPanel::chooseFolder()
{
    sead::FixedSafeString<1024> path;

    if (!SelectFolderDialog(&path, messages::files::cChooseFolderTitle))
        return false;

    requestAction(RootAction::Open, -1, path.cstr());

    return true;
}

void FilesPanel::requestAction(RootAction action, s32 root, const std::string& path, bool isPinned)
{
    mPendingAction = { action, root >= 0 ? mRoots[root].path : std::string(), path, isPinned };
}

void FilesPanel::applyPendingAction()
{
    const PendingAction pending = std::move(mPendingAction);
    mPendingAction = PendingAction();

    const s32 root = findRoot(pending.rootPath);

    if (pending.action != RootAction::Open && root < 0)
        return;

    switch (pending.action)
    {
        case RootAction::TogglePin:
            togglePin(root);
            break;

        case RootAction::MoveUp:
            moveRoot(root, -1);
            break;

        case RootAction::MoveDown:
            moveRoot(root, 1);
            break;

        case RootAction::Rescan:
            requestScan(root, true);
            break;

        case RootAction::Remove:
            mRoots.erase(mRoots.begin() + root);
            onRootsChanged();
            saveConfig();
            break;

        case RootAction::Open:
            openFolder(pending.path, pending.isPinned);
            break;

        default:
            break;
    }
}

void FilesPanel::requestScan(s32 index, bool isFullScan)
{
    FilesRoot& root = mRoots[index];
    std::error_code error;

    root.isMissing = !fs::is_directory(root.path, error);
    root.needsScan = !root.isMissing;
    root.isFullScan = root.isFullScan || isFullScan;
    root.scanRequest = ++mScanRequestCount;
    invalidateRows();
}

void FilesPanel::updateScans()
{
    s32 next = -1;
    bool isAnyScanning = false;

    for (s32 i = 0; i < static_cast<s32>(mRoots.size()); i++)
    {
        FilesRoot& root = mRoots[i];

        FileIndexSnapshot previous;

        if (root.index->update(&previous))
        {
            if (root.hasSnapshot)
                root.savedOpenFolders = CollectOpenFolders(previous, root.expanded);

            root.hasSnapshot = true;
            restoreExpanded(root, std::move(root.savedOpenFolders).value_or(std::vector<std::string>()));
            root.savedOpenFolders.reset();
            updateSizeLabelLengths(root);
            mSnapshotGeneration++;

            if (mSelected.root == i && mSelected.kind != FilesRowKind::Root)
                mSelected = FilesRow();
        }

        isAnyScanning |= root.index->isScanning();

        if (root.needsScan && (next < 0 || IsScanFirst(root, mRoots[next])))
            next = i;
    }

    if (!isAnyScanning && next >= 0)
        startScan(next);
}

void FilesPanel::startScan(s32 index)
{
    FilesRoot& root = mRoots[index];

    root.index->scan(root.path, MakeCachePath(root.path), !root.isFullScan);
    root.needsScan = false;
    root.isFullScan = false;
}

bool FilesPanel::isRootScanning(s32 root) const
{
    return mRoots[root].needsScan || mRoots[root].index->isScanning();
}

const FileIndexSnapshot& FilesPanel::getSnapshot(s32 root) const
{
    return mRoots[root].index->getSnapshot();
}

const BrowserFile& FilesPanel::getFile(const FilesRow& row) const
{
    return getSnapshot(row.root).files[row.index];
}

const BrowserFolder* FilesPanel::getPackFolder(const FilesRow& row) const
{
    if (row.kind != FilesRowKind::Folder)
        return nullptr;

    const BrowserFolder& folder = getSnapshot(row.root).folders[row.index];

    return folder.isPack ? &folder : nullptr;
}

s32 FilesPanel::findPackAncestor(s32 root, s32 folder) const
{
    const std::vector<BrowserFolder>& folders = getSnapshot(root).folders;

    for (s32 ancestor = folder; ancestor >= 0; ancestor = folders[ancestor].parent)
    {
        if (folders[ancestor].isPack)
            return ancestor;
    }

    return -1;
}

void FilesPanel::restoreExpanded(FilesRoot& root, std::vector<std::string> openFolders)
{
    const FileIndexSnapshot& snapshot = root.index->getSnapshot();

    root.expanded.assign(snapshot.folders.size(), 0);

    if (snapshot.folders.empty())
        return;

    root.expanded[0] = 1;

    if (openFolders.empty())
        return;

    std::sort(openFolders.begin(), openFolders.end());

    for (s32 folder = 1; folder < static_cast<s32>(snapshot.folders.size()); folder++)
    {
        if (std::binary_search(openFolders.begin(), openFolders.end(), MakeFolderKey(snapshot, folder)))
            root.expanded[folder] = 1;
    }
}

void FilesPanel::updateSizeLabelLengths(FilesRoot& root)
{
    const std::vector<BrowserFile>& files = root.index->getSnapshot().files;

    root.sizeLabelLengths.resize(files.size());

    for (size_t i = 0; i < files.size(); i++)
    {
        sead::FixedSafeString<32> size;
        FormatItemSize(&size, files[i].size);

        root.sizeLabelLengths[i] = static_cast<u8>(size.calcLength());
    }
}

std::string FilesPanel::getFolderPath(s32 root, s32 folder) const
{
    fs::path path = fs::path(mRoots[root].path) / MakeFolderKey(getSnapshot(root), folder);

    return path.make_preferred().string();
}

std::string FilesPanel::makePrefixKey(const FilesRow& row) const
{
    const FileIndexSnapshot& snapshot = getSnapshot(row.root);

    return MakeFolderKey(snapshot, row.index) + '/' + snapshot.files[row.sound].stem.substr(0, row.nameStart - 1) + '_';
}

void FilesPanel::drawRootRow(FrameContext& ctx, const FilesRow& row)
{
    FilesRoot& root = mRoots[row.root];
    const bool isOpen = ctx.isSearching || root.isOpen;

    ImGui::SetNextItemAllowOverlap();

    const RowFrame frame = beginRow(ctx, row, 0.0f);

    if (frame.isPressed && !ctx.isSearching)
        setRowOpen(row, !root.isOpen);

    ImGui::OpenPopupOnItemClick(cRootMenuId, ImGuiPopupFlags_MouseButtonRight);

    if (IsItemHoveredForDetail())
        ImGui::SetTooltip(root.isMissing ? messages::recent::cNotFoundFormat : "%s", root.path.c_str());

    const bool isRowHovered = ImGui::IsPopupOpen(cRootMenuId) || (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(frame.rowMin, frame.rowMax, false));
    const bool isScanning = isRootScanning(row.root);
    const bool showCount = root.hasSnapshot && !root.isMissing && !isScanning && !ctx.isSearching && !ctx.shownCounts[row.root].empty();
    const f32 buttonHit = frame.rowMax.y - frame.rowMin.y;
    const f32 labelRight = frame.rowMax.x - ImGui::GetStyle().CellPadding.x - buttonHit * static_cast<f32>(GetRootButtonCount(root));
    const ImU32 dimColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const ImU32 nameColor = root.isMissing ? dimColor : ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 iconColor = root.isMissing ? dimColor : ImGui::GetColorU32(GetItemValueColor());
    const char* icon = root.isMissing ? ICON_LC_FOLDER_X : isOpen ? ICON_LC_FOLDER_OPEN : ICON_LC_FOLDER_CLOSED;

    const sead::FormatFixedSafeString<32> count("%u", showCount ? ctx.shownCounts[row.root][0] : 0);
    const sead::FormatFixedSafeString<288> note("%s%s%s", root.note.c_str(), !root.note.empty() && showCount ? "  " : "", showCount ? count.cstr() : "");

    PushTableRowClipRect(frame.rowMin, frame.rowMax);

    ImGui::GetWindowDrawList()->AddText(frame.textPos, dimColor, isOpen ? ICON_LC_CHEVRON_DOWN : ICON_LC_CHEVRON_RIGHT);

    const f32 labelEnd = drawRowLabel(ImVec2(frame.textPos.x + ctx.chevronWidth, frame.textPos.y), labelRight, icon, iconColor, root.name.c_str(), nameColor, note.isEmpty() ? nullptr : note.cstr(), dimColor);

    if (isScanning)
    {
        const f32 radius = ImFloor(ctx.em * cSpinnerRadiusEm);
        const f32 centerX = labelEnd + ImFloor(ctx.em * cIconGapEm) + radius;

        if (centerX + radius <= labelRight)
            DrawSpinner(ImVec2(centerX, frame.textPos.y + ImFloor(ctx.em * 0.5f)), radius, dimColor);
    }

    drawRootButtons(root, row.root, frame, isRowHovered);

    ImGui::PopClipRect();

    drawRootMenu(root, row.root);
}

void FilesPanel::drawRootButtons(const FilesRoot& root, s32 index, const RowFrame& frame, bool isRowHovered)
{
    const f32 hit = frame.rowMax.y - frame.rowMin.y;
    const f32 size = sead::Mathf::min(GetRowButtonSize(), hit);
    const f32 pinX = frame.rowMax.x - ImGui::GetStyle().CellPadding.x - hit;
    const ImVec2 hitSize(hit, hit);

    if (!isRootScanning(index))
    {
        ImGui::SetCursorScreenPos(ImVec2(pinX - hit, frame.rowMin.y));

        if (DrawRowHoverButton("##rescan", ICON_LC_REFRESH_CW, false, isRowHovered, messages::files::cRescan, hitSize, size))
            requestAction(RootAction::Rescan, index);
    }

    if (!root.isPinned)
    {
        ImGui::SetCursorScreenPos(ImVec2(pinX - hit * 2.0f, frame.rowMin.y));

        if (DrawRowHoverButton("##remove", ICON_LC_X, false, isRowHovered, messages::recent::cRemove, hitSize, size))
            requestAction(RootAction::Remove, index);
    }

    ImGui::SetCursorScreenPos(ImVec2(pinX, frame.rowMin.y));

    if (DrawRowHoverButton("##pin", ICON_LC_PIN, root.isPinned, root.isPinned || isRowHovered, root.isPinned ? messages::recent::cUnpin : messages::recent::cPin, hitSize, size))
        requestAction(RootAction::TogglePin, index);
}

void FilesPanel::drawRootMenu(const FilesRoot& root, s32 index)
{
    if (!BeginContextMenu(cRootMenuId))
        return;

    const s32 count = static_cast<s32>(mRoots.size());
    const bool canMoveUp = index > 0 && mRoots[index - 1].isPinned == root.isPinned;
    const bool canMoveDown = index + 1 < count && mRoots[index + 1].isPinned == root.isPinned;

    if (ContextMenuItem(ICON_LC_PIN, root.isPinned ? messages::recent::cUnpin : messages::recent::cPin))
        requestAction(RootAction::TogglePin, index);

    if (canMoveUp || canMoveDown)
    {
        if (ContextMenuItem(ICON_LC_ARROW_UP, messages::files::cMoveUp, canMoveUp))
            requestAction(RootAction::MoveUp, index);

        if (ContextMenuItem(ICON_LC_ARROW_DOWN, messages::files::cMoveDown, canMoveDown))
            requestAction(RootAction::MoveDown, index);
    }

    ContextMenuSeparator();

    if (ContextMenuItem(ICON_LC_REFRESH_CW, messages::files::cRescan, !isRootScanning(index)))
        requestAction(RootAction::Rescan, index);

    if (ContextMenuItem(ICON_LC_FOLDER_OPEN, messages::recent::cShowInFileManager, !root.isMissing))
        OpenFolder(root.path);

    if (ContextMenuItem(ICON_LC_COPY, messages::mainmenu::cCopyPath))
        ImGui::SetClipboardText(root.path.c_str());

    ContextMenuSeparator();

    if (ContextMenuItem(ICON_LC_X, messages::recent::cRemove))
        requestAction(RootAction::Remove, index);

    EndContextMenu();
}

void FilesPanel::drawFolderMenu(s32 root, s32 folder)
{
    if (!BeginContextMenu(cFolderMenuId))
        return;

    const std::string path = getFolderPath(root, folder);
    const s32 pack = findPackAncestor(root, folder);

    if (pack < 0 && ContextMenuItem(ICON_LC_PIN, messages::recent::cPin))
        requestAction(RootAction::Open, -1, path, true);

    if (ContextMenuItem(ICON_LC_FOLDER_OPEN, messages::recent::cShowInFileManager))
    {
        if (pack >= 0)
            ShowInFileManager(getFolderPath(root, pack));
        else
            OpenFolder(path);
    }

    if (ContextMenuItem(ICON_LC_COPY, messages::mainmenu::cCopyPath))
        ImGui::SetClipboardText(path.c_str());

    EndContextMenu();
}

void FilesPanel::drawStatusRow(FrameContext& ctx, const FilesRow& row)
{
    const FilesRoot& root = mRoots[row.root];
    const ImGuiTable* table = ImGui::GetCurrentTable();
    const f32 rowHeight = ImGui::GetFrameHeight();

    ImGui::TableSetColumnIndex(static_cast<s32>(FilesColumn::Name));

    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const ImVec2 rowMin(table->WorkRect.Min.x, cursor.y);
    const ImVec2 rowMax(table->WorkRect.Max.x, cursor.y + rowHeight);
    const ImVec2 textPos(cursor.x + row.depth * ImFloor(ctx.em * cIndentEm) + ctx.chevronWidth, cursor.y + ImGui::GetStyle().FramePadding.y);
    const f32 right = rowMax.x - ImGui::GetStyle().CellPadding.x;
    const ImU32 dimColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);

    ImGui::Dummy(ImVec2(1.0f, rowHeight));
    PushTableRowClipRect(rowMin, rowMax);

    if (root.isMissing)
        drawRowLabel(textPos, right, ICON_LC_FOLDER_X, dimColor, messages::files::cRootMissing, dimColor, nullptr, 0);
    else if (!root.hasSnapshot)
        drawScanProgress(root, textPos, right);
    else
        ImGui::GetWindowDrawList()->AddText(textPos, dimColor, isAnyKindHidden() ? messages::files::cNoMatch : messages::files::cNoAudio);

    ImGui::PopClipRect();
}

void FilesPanel::drawGroupRow(const FilesRow& row)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImGuiTable* table = ImGui::GetCurrentTable();
    const f32 rowHeight = ImGui::GetFrameHeight();
    const char* title = row.index == 0 ? messages::common::cPinned : messages::files::cRecentFolders;

    ImGui::TableSetColumnIndex(static_cast<s32>(FilesColumn::Name));

    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const ImVec2 rowMin(table->WorkRect.Min.x, cursor.y);
    const ImVec2 rowMax(table->WorkRect.Max.x, cursor.y + rowHeight);
    const ImVec2 textPos(cursor.x, cursor.y + style.FramePadding.y);
    const f32 lineLeft = textPos.x + ImGui::CalcTextSize(title).x + style.ItemSpacing.x;
    const f32 lineRight = rowMax.x - style.CellPadding.x;
    const f32 lineY = ImFloor(cursor.y + rowHeight * 0.5f);

    ImGui::Dummy(ImVec2(1.0f, rowHeight));
    PushTableRowClipRect(rowMin, rowMax);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddText(textPos, ImGui::GetColorU32(ImGuiCol_Text), title);

    if (lineLeft < lineRight)
        drawList->AddLine(ImVec2(lineLeft, lineY), ImVec2(lineRight, lineY), ImGui::GetColorU32(ImGuiCol_Separator), style.SeparatorTextBorderSize);

    ImGui::PopClipRect();
}

void FilesPanel::drawScanProgress(const FilesRoot& root, const ImVec2& pos, f32 right)
{
    const FileIndex& index = *root.index;
    const f32 em = ImGui::GetFontSize();
    const f32 radius = ImFloor(em * cSpinnerRadiusEm);
    const f32 centerY = pos.y + ImFloor(em * 0.5f);
    const f32 textX = pos.x + em + ImFloor(em * cIconGapEm);
    const ImU32 dimColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const bool isScanning = index.isScanning();
    const bool hasTotal = isScanning && index.isListDone() && index.getFoundCount() > 0;

    sead::FixedSafeString<64> text;

    if (!isScanning)
        text = messages::files::cWaiting;
    else if (!index.isListDone())
        text.format(messages::files::cIndexingFormat, index.getFoundCount());
    else
        text.format(messages::files::cReadingFormat, index.getScannedCount(), index.getFoundCount());

    DrawSpinner(ImVec2(pos.x + ImFloor(em * 0.5f), centerY), radius, dimColor);
    ImGui::GetWindowDrawList()->AddText(ImVec2(textX, pos.y), dimColor, text.cstr());

    if (!hasTotal)
        return;

    const f32 barTop = pos.y + em + ImFloor(em * cProgressTopEm);
    const f32 barHeight = sead::Mathf::max(ImFloor(em * cProgressHeightEm * 0.5f), 2.0f);
    const f32 barRight = sead::Mathf::min(right, textX + ImGui::CalcTextSize(text.cstr()).x);

    DrawProgressBar(ImVec2(textX, barTop), ImVec2(barRight, barTop + barHeight), static_cast<f32>(index.getScannedCount()) / static_cast<f32>(index.getFoundCount()));
}

void FilesPanel::drawAddFolderRow(const FrameContext& ctx)
{
    ImGui::TableSetColumnIndex(static_cast<s32>(FilesColumn::Name));
    ImGui::AlignTextToFramePadding();

    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const f32 rowHeight = ImGui::GetFrameHeight();
    const f32 iconWidth = ImGui::CalcTextSize(ICON_LC_FOLDER_PLUS).x;
    const f32 labelWidth = iconWidth + ImFloor(ctx.em * cIconGapEm) + ImGui::CalcTextSize(messages::files::cAddFolder).x;
    const ImVec2 labelPos(cursor.x + ctx.chevronWidth, cursor.y + ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset);

    ImGui::SetCursorScreenPos(ImVec2(labelPos.x, cursor.y));

    if (ImGui::InvisibleButton("##addFolder", ImVec2(labelWidth, rowHeight)))
        chooseFolder();

    ImGui::SetItemTooltip("%s", messages::files::cDropFolderTip);

    const bool isHovered = ImGui::IsItemHovered();

    if (isHovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    const ImU32 color = ImGui::GetHoverColorU32(ImGui::GetItemID(), isHovered, ImGui::GetColorU32(ImGuiCol_TextDisabled), ImGui::GetColorU32(ImGuiCol_Text));

    drawRowLabel(labelPos, labelPos.x + labelWidth, ICON_LC_FOLDER_PLUS, color, messages::files::cAddFolder, color, nullptr, 0);
}
