#include <ui/browser/FilesPanelInternal.h>

#include <ui/app/Notifications.h>
#include <ui/browser/FilesPanel.h>
#include <ui/file/File.h>
#include <ui/file/FileDialogs.h>
#include <ui/file/RecentFiles.h>
#include <ui/file/SystemShell.h>
#include <ui/items/ItemColumns.h>
#include <ui/settings/ConfigFile.h>
#include <ui/settings/Theme.h>
#include <ui/widgets/ContextMenu.h>
#include <ui/widgets/Widgets.h>
#include <ui/Messages.h>

#include <bfsar/io/FileIo.h>
#include <bfsar/model/ArchiveFormat.h>
#include <core/Json.h>
#include <libs/IconsLucide.h>
#include <imgui/imgui_custom.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static constexpr f32 cSpinnerRadiusEm  = 0.3f;
static constexpr f32 cProgressHeightEm = 0.3f;
static constexpr f32 cProgressTopEm    = 0.15f;

static constexpr const char* cConfigSection  = "browser";
static constexpr const char* cRootsKey       = "roots";
static constexpr const char* cOldRootKey     = "root";
static constexpr const char* cPathKey        = "path";
static constexpr const char* cPinnedKey      = "pinned";
static constexpr const char* cFileKey        = "file";
static constexpr const char* cSectionsKey    = "sections";
static constexpr const char* cSectionKey     = "section";
static constexpr const char* cNameKey        = "name";
static constexpr const char* cFoldedKey      = "folded";
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
    return root.isFile ? 0 : 1;
}

static std::string MakeRootName(const std::string& path)
{
    const std::string name = fs::path(path).filename().string();

    return name.empty() ? path : name;
}

static FilesRoot MakeRoot(const std::string& path, bool isFile, u32 section)
{
    FilesRoot root;
    root.path = path;
    root.name = MakeRootName(path);
    root.isFile = isFile;
    root.section = section;

    return root;
}

static bool IsInsideFolder(const std::string& path, const std::string& folder)
{
    return path.size() > folder.size() && path.compare(0, folder.size(), folder) == 0 && (path[folder.size()] == '/' || path[folder.size()] == '\\');
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

    const json::Value* sections = section.findArray(cSectionsKey);

    if (sections)
    {
        for (const json::Value& entry : sections->items)
        {
            const std::string* name = entry.findString(cNameKey);
            mSections.push_back({ mNextSectionId++, name ? *name : makeSectionName(), entry.getBool(cFoldedKey, false) });
        }
    }

    std::vector<u8> isOldPin;

    if (const json::Value* roots = section.findArray(cRootsKey))
    {
        for (const json::Value& entry : roots->items)
        {
            const std::string* path = entry.findString(cPathKey);

            if (!path || path->empty() || findRoot(*path) >= 0)
                continue;

            const s32 sectionIndex = entry.getInt(cSectionKey, 0) - 1;
            const bool isFile = entry.getBool(cFileKey, false);
            FilesRoot root = MakeRoot(*path, isFile, sectionIndex >= 0 && sectionIndex < static_cast<s32>(mSections.size()) ? mSections[sectionIndex].id : 0);
            root.isOpen = entry.getBool(cOpenKey, true);
            isOldPin.push_back(isFile || entry.getBool(cPinnedKey, false));

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
        mRoots.push_back(MakeRoot(*oldRoot, false, 0));
    }

    if (!sections && !isOldPin.empty())
    {
        std::vector<FilesRoot> pinnedFirst;

        for (const bool isPin : { true, false })
        {
            for (size_t i = 0; i < isOldPin.size(); i++)
            {
                if ((isOldPin[i] != 0) == isPin)
                    pinnedFirst.push_back(std::move(mRoots[i]));
            }
        }

        mRoots = std::move(pinnedFirst);
    }

    sortRoots();
    addLegacyPins();

    for (s32 i = static_cast<s32>(mRoots.size()) - 1; i >= 0; i--)
        requestScan(i);

    onRootsChanged();
}

void FilesPanel::addLegacyPins()
{
    const std::vector<std::string> paths = TakeLegacyPinnedFiles();

    if (paths.empty())
        return;

    for (const std::string& path : paths)
    {
        const std::string canonical = fileio::CanonicalPath(path.c_str());

        if (!canonical.empty() && findRoot(canonical) < 0)
            insertRoot(MakeRoot(canonical, true, 0));
    }

    saveConfig();
}

void FilesPanel::saveConfig() const
{
    json::Value section;
    json::Value& sections = section.setArray(cSectionsKey);

    for (const FilesSection& entry : mSections)
    {
        json::Value& value = sections.pushObject();
        value.setString(cNameKey, entry.name.c_str());
        value.setBool(cFoldedKey, entry.isFolded);
    }

    json::Value& roots = section.setArray(cRootsKey);

    for (const FilesRoot& root : mRoots)
    {
        json::Value& entry = roots.pushObject();
        entry.setString(cPathKey, root.path.c_str());
        entry.setBool(cOpenKey, root.isOpen);

        if (root.section != 0)
            entry.setInt(cSectionKey, getSectionRank(root.section));

        if (root.isFile)
            entry.setBool(cFileKey, true);

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

s32 FilesPanel::insertRoot(FilesRoot root, s32 slot)
{
    if (findSection(root.section) < 0)
        root.section = 0;

    const s32 start = getSectionStart(root.section);
    const s32 end = getSectionEnd(root.section);
    const s32 index = slot < 0 ? end : std::clamp(slot, start, end);

    mRoots.insert(mRoots.begin() + index, std::move(root));

    return index;
}

s32 FilesPanel::addRoot(const std::string& path, bool isFile, u32 section, s32 slot)
{
    const s32 index = insertRoot(MakeRoot(path, isFile, section), slot);
    requestScan(index);

    return index;
}

void FilesPanel::removeRoot(s32 index)
{
    mRoots.erase(mRoots.begin() + index);
    onRootsChanged();
    invalidateRows();
    saveConfig();
}

void FilesPanel::onRootsChanged()
{
    mSelected = FilesRow();
    mSnapshotGeneration++;

    for (FilesRoot& root : mRoots)
    {
        root.note.clear();
        root.isNested = false;

        for (const FilesRoot& other : mRoots)
        {
            if (&other == &root)
                continue;

            if (EqualsIgnoreCase(other.name.c_str(), root.name.c_str()))
                root.note = MakeRootName(fs::path(root.path).parent_path().string());

            root.isNested |= !other.isFile && IsInsideFolder(root.path, other.path);
        }
    }
}

void FilesPanel::selectRoot(s32 root)
{
    mSelected = FilesRow { .kind = FilesRowKind::Root, .root = root };
    mScrollToSelected = true;
    clearMultiSelection();
}

s32 FilesPanel::addPath(const std::string& path, u32 section, s32 slot)
{
    const std::string canonical = fileio::CanonicalPath(path.c_str());

    if (canonical.empty())
        return -1;

    std::error_code error;
    s32 index = findRoot(canonical);

    if (index < 0)
    {
        index = addRoot(canonical, !fs::is_directory(canonical, error), section, slot);
        onRootsChanged();
    }

    setSectionFolded(mRoots[index].section, false);
    mRoots[index].isOpen = true;
    invalidateRows();
    selectRoot(index);
    saveConfig();

    return index;
}

void FilesPanel::addPaths(const std::vector<std::string>& paths, s32 slot, u32 section)
{
    std::string names;
    u32 count = 0;

    for (const std::string& path : paths)
    {
        const std::string canonical = fileio::CanonicalPath(path.c_str());
        const bool wasRoot = isRootPath(canonical);
        s32 index = wasRoot ? findRoot(canonical) : addPath(path, section, slot);

        if (index < 0)
            continue;

        if (wasRoot && slot >= 0)
            index = placeRoot(index, slot, section);

        if (slot >= 0)
            slot = index + 1;

        if (wasRoot)
            continue;

        names += count++ == 0 ? "" : ", ";
        names += mRoots[index].name;
    }

    if (count == 1)
        notify::Add(notify::Kind::Success, messages::files::cAddedOneTitle, names.c_str());
    else if (count > 1)
        notify::Add(notify::Kind::Success, sead::FormatFixedSafeString<64>(messages::files::cAddedTitleFormat, FormatCountText(count).cstr()).cstr(), names.c_str());
}

void FilesPanel::removeRootWithUndo(s32 index)
{
    const u32 token = mNextUndoToken++;
    const std::string name = mRoots[index].name;

    PushRemovedEntry(&mRemovedRoots, RemovedRoot { token, index, std::move(mRoots[index]) });
    removeRoot(index);

    notify::Add(notify::Kind::Info, messages::files::cRemovedTitle, name.c_str(), { messages::common::cUndo, &UndoFilesPanelRemove, token });
}

void FilesPanel::requestUndo(u32 token)
{
    mUndoToken = token;
}

void FilesPanel::applyUndo()
{
    const u32 token = mUndoToken;
    mUndoToken = 0;

    if (token == 0)
        return;

    const auto section = std::find_if(mRemovedSections.begin(), mRemovedSections.end(), [token](const RemovedSection& removed) { return removed.token == token; });

    if (section != mRemovedSections.end())
    {
        RemovedSection removed = std::move(*section);
        mRemovedSections.erase(section);
        mSections.insert(mSections.begin() + std::min(removed.index, static_cast<s32>(mSections.size())), removed.section);

        for (FilesRoot& root : mRoots)
        {
            if (std::find(removed.rootPaths.begin(), removed.rootPaths.end(), root.path) != removed.rootPaths.end())
                root.section = removed.section.id;
        }

        sortRoots();
        onRootsChanged();
        invalidateRows();
        saveConfig();
        return;
    }

    const auto found = std::find_if(mRemovedRoots.begin(), mRemovedRoots.end(), [token](const RemovedRoot& removed) { return removed.token == token; });

    if (found == mRemovedRoots.end())
        return;

    RemovedRoot removed = std::move(*found);
    mRemovedRoots.erase(found);

    if (findRoot(removed.root.path) >= 0)
        return;

    const s32 index = insertRoot(std::move(removed.root), removed.index);

    setSectionFolded(mRoots[index].section, false);
    onRootsChanged();
    invalidateRows();
    selectRoot(index);
    saveConfig();
}

void FilesPanel::moveRoot(s32 index, s32 offset)
{
    const s32 target = index + offset;

    if (target < 0 || target >= static_cast<s32>(mRoots.size()) || mRoots[target].section != mRoots[index].section)
        return;

    std::swap(mRoots[index], mRoots[target]);

    onRootsChanged();
    invalidateRows();
    selectRoot(target);
    saveConfig();
}

s32 FilesPanel::placeRoot(s32 index, s32 slot, u32 section)
{
    if (findSection(section) < 0)
        section = 0;

    if (mRoots[index].section == section && (slot == index || slot == index + 1))
        return index;

    FilesRoot root = std::move(mRoots[index]);
    mRoots.erase(mRoots.begin() + index);
    root.section = section;

    const s32 target = insertRoot(std::move(root), index < slot ? slot - 1 : slot);

    onRootsChanged();
    invalidateRows();
    selectRoot(target);
    saveConfig();

    return target;
}

bool FilesPanel::chooseFolder(u32 section)
{
    sead::FixedSafeString<1024> path;

    if (!SelectFolderDialog(&path, messages::files::cChooseFolderTitle))
        return false;

    requestAction(RootAction::Open, -1, path.cstr(), section);

    return true;
}

bool FilesPanel::chooseFiles(u32 section)
{
    std::vector<std::string> paths;

    if (!ChooseDocumentFiles(&paths, messages::files::cChooseFilesTitle))
        return false;

    addPaths(paths, -1, section);

    return true;
}

void FilesPanel::requestAction(RootAction action, s32 root, const std::string& path, u32 section)
{
    mPendingAction = { action, root >= 0 ? mRoots[root].path : std::string(), path, section };
}

void FilesPanel::applyPendingAction()
{
    const PendingAction pending = std::move(mPendingAction);
    mPendingAction = PendingAction();

    const s32 root = findRoot(pending.rootPath);

    switch (pending.action)
    {
        case RootAction::Open:
            addPath(pending.path, pending.section);
            return;

        case RootAction::ChooseFolder:
            chooseFolder(pending.section);
            return;

        case RootAction::ChooseFiles:
            chooseFiles(pending.section);
            return;

        case RootAction::NewSection:
            addSection(pending.section != 0 ? findSection(pending.section) + 1 : -1);
            return;

        case RootAction::RemoveSection:
            removeSection(pending.section);
            return;

        default:
            break;
    }

    if (root < 0)
        return;

    switch (pending.action)
    {
        case RootAction::MoveUp:
            moveRoot(root, -1);
            break;

        case RootAction::MoveDown:
            moveRoot(root, 1);
            break;

        case RootAction::MoveToSection:
            placeRoot(root, getSectionEnd(pending.section), pending.section);
            break;

        case RootAction::MoveToNewSection:
        {
            const u32 section = addSection();
            placeRoot(findRoot(pending.rootPath), getSectionEnd(section), section);
            break;
        }

        case RootAction::Rescan:
            requestScan(root, true);
            break;

        case RootAction::Remove:
            removeRootWithUndo(root);
            break;

        case RootAction::SearchInFolder:
            setSearchScope(root, pending.path);
            break;

        default:
            break;
    }
}

void FilesPanel::requestScan(s32 index, bool isFullScan)
{
    FilesRoot& root = mRoots[index];
    std::error_code error;

    root.isMissing = root.isFile ? !fs::is_regular_file(root.path, error) : !fs::is_directory(root.path, error);
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

s32 FilesPanel::getRootFolder(s32 root) const
{
    const FilesRoot& entry = mRoots[root];

    if (!entry.isFile)
        return 0;

    if (!entry.hasSnapshot || entry.isMissing)
        return -1;

    const std::vector<BrowserFolder>& folders = getSnapshot(root).folders;

    for (s32 child : folders.front().folders)
    {
        if (folders[child].isPack)
            return child;
    }

    return -1;
}

const BrowserFile* FilesPanel::getRootFile(s32 root) const
{
    if (getRootFolder(root) >= 0 || !mRoots[root].hasSnapshot || mRoots[root].isMissing)
        return nullptr;

    const std::vector<BrowserFile>& files = getSnapshot(root).files;

    return files.size() == 1 ? &files.front() : nullptr;
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
    const fs::path base = mRoots[root].isFile ? fs::path(mRoots[root].path).parent_path() : fs::path(mRoots[root].path);
    fs::path path = base / MakeFolderKey(getSnapshot(root), folder);

    return path.make_preferred().string();
}

std::string FilesPanel::getFolderKey(s32 root, s32 folder) const
{
    return MakeFolderKey(getSnapshot(root), folder);
}

s32 FilesPanel::findFolder(s32 root, const std::string& key) const
{
    const FileIndexSnapshot& snapshot = getSnapshot(root);

    for (s32 folder = 0; folder < static_cast<s32>(snapshot.folders.size()); folder++)
    {
        if (MakeFolderKey(snapshot, folder) == key)
            return folder;
    }

    return -1;
}

std::string FilesPanel::makePrefixKey(const FilesRow& row) const
{
    const FileIndexSnapshot& snapshot = getSnapshot(row.root);

    return MakeFolderKey(snapshot, row.index) + '/' + snapshot.files[row.sound].stem.substr(0, row.nameStart - 1) + '_';
}

void FilesPanel::drawRootRow(FrameContext& ctx, const FilesRow& row)
{
    const FilesRoot& root = mRoots[row.root];
    const BrowserFile* file = getRootFile(row.root);

    ImGui::SetNextItemAllowOverlap();

    const RowFrame frame = beginRow(ctx, row, 0.0f);

    if (isExpandable(row) && (file ? isChevronClicked(ctx, frame) : frame.isPressed))
        setRowOpen(row, !isRowOpen(row));
    else if (file && frame.isDoubleClicked)
        openFile(*file);
    else if (file && frame.isPressed)
        showFile(*file);

    ImGui::OpenPopupOnItemClick(cRootMenuId, ImGuiPopupFlags_MouseButtonRight);

    if (IsItemHoveredForDetail())
    {
        if (file)
            drawFileTooltip(*file, true);
        else
            ImGui::SetTooltip(root.isMissing ? messages::recent::cNotFoundFormat : "%s", root.path.c_str());
    }

    const bool isRowHovered = ImGui::IsPopupOpen(cRootMenuId) || (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(frame.rowMin, frame.rowMax, false));
    const f32 buttonHit = frame.rowMax.y - frame.rowMin.y;
    const f32 labelRight = frame.rowMax.x - ImGui::GetStyle().CellPadding.x - buttonHit * static_cast<f32>(GetRootButtonCount(root));

    PushTableRowClipRect(frame.rowMin, frame.rowMax);

    const f32 labelEnd = drawRootLabel(ctx, row, frame, labelRight);

    if (isRootScanning(row.root))
    {
        const f32 radius = ImFloor(ctx.em * cSpinnerRadiusEm);
        const f32 centerX = labelEnd + ImFloor(ctx.em * cIconGapEm) + radius;

        if (centerX + radius <= labelRight)
            DrawSpinner(ImVec2(centerX, frame.textPos.y + ImFloor(ctx.em * 0.5f)), radius, ImGui::GetColorU32(ImGuiCol_TextDisabled));
    }

    drawRootButtons(root, row.root, frame, isRowHovered);

    ImGui::PopClipRect();

    drawRootMenu(root, row.root);
}

f32 FilesPanel::drawRootLabel(const FrameContext& ctx, const FilesRow& row, const RowFrame& frame, f32 right)
{
    const FilesRoot& root = mRoots[row.root];
    const BrowserFile* file = getRootFile(row.root);
    const s32 folder = getRootFolder(row.root);
    const bool canOpen = isExpandable(row);
    const bool isOpen = canOpen && isRowOpen(row);
    const bool showCount = folder >= 0 && root.hasSnapshot && !root.isMissing && !isRootScanning(row.root) && folder < static_cast<s32>(ctx.shownCounts[row.root].size());
    const OpenFileMark* openMark = file ? findOpenFile(ctx, file->path) : nullptr;
    const ImU32 dimColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);

    const char* icon = isOpen ? ICON_LC_FOLDER_OPEN : ICON_LC_FOLDER_CLOSED;
    ImU32 iconColor = ImGui::GetColorU32(GetItemValueColor());

    if (root.isMissing)
    {
        icon = root.isFile ? ICON_LC_FILE_X : ICON_LC_FOLDER_X;
        iconColor = dimColor;
    }
    else if (file)
    {
        icon = getFileIcon(*file, &iconColor);
    }
    else if (root.isFile && folder >= 0)
    {
        icon = ICON_LC_FOLDER_ARCHIVE;
        iconColor = ImGui::GetColorU32(GetFileKindColor(BrowserFileKind::AudioResource));
    }
    else if (root.isFile)
    {
        icon = ICON_LC_FILE;
    }

    const sead::FixedSafeString<32> count = FormatCountText(!showCount ? 0 : ctx.isSearching ? row.childCount : ctx.shownCounts[row.root][folder]);
    const sead::FormatFixedSafeString<288> note("%s%s%s", root.note.c_str(), !root.note.empty() && showCount ? "  " : "", showCount ? count.cstr() : "");
    const char* noteText = openMark ? cOpenFileMark : note.isEmpty() ? nullptr : note.cstr();
    const ImU32 noteColor = openMark ? getOpenMarkColor(openMark) : dimColor;

    if (canOpen)
        ImGui::GetWindowDrawList()->AddText(frame.textPos, dimColor, isOpen ? ICON_LC_CHEVRON_DOWN : ICON_LC_CHEVRON_RIGHT);

    return drawRowLabel(ImVec2(frame.textPos.x + ctx.chevronWidth, frame.textPos.y), right, icon, iconColor, root.name.c_str(), root.isMissing ? dimColor : getOpenNameColor(openMark), noteText, noteColor);
}

void FilesPanel::drawRootButtons(const FilesRoot& root, s32 index, const RowFrame& frame, bool isRowHovered)
{
    if (root.isFile || isRootScanning(index))
        return;

    const f32 hit = frame.rowMax.y - frame.rowMin.y;
    const f32 size = sead::Mathf::min(GetRowButtonSize(), hit);

    ImGui::SetCursorScreenPos(ImVec2(frame.rowMax.x - ImGui::GetStyle().CellPadding.x - hit, frame.rowMin.y));

    if (DrawRowHoverButton("##rescan", ICON_LC_REFRESH_CW, isRowHovered, messages::files::cRescan, ImVec2(hit, hit), size))
        requestAction(RootAction::Rescan, index);
}

void FilesPanel::drawRootMenu(const FilesRoot& root, s32 index)
{
    if (!BeginContextMenu(cRootMenuId))
        return;

    const s32 count = static_cast<s32>(mRoots.size());
    const bool canMoveUp = index > 0 && mRoots[index - 1].section == root.section;
    const bool canMoveDown = index + 1 < count && mRoots[index + 1].section == root.section;

    if (const BrowserFile* file = getRootFile(index))
    {
        if (drawSelectionMenu(*file))
        {
            EndContextMenu();
            return;
        }

        drawFileCommands(*file);
        ContextMenuSeparator();
    }

    drawMoveToSectionMenu(index);

    if (canMoveUp || canMoveDown)
    {
        if (ContextMenuItem(ICON_LC_ARROW_UP, messages::files::cMoveUp, canMoveUp))
            requestAction(RootAction::MoveUp, index);

        if (ContextMenuItem(ICON_LC_ARROW_DOWN, messages::files::cMoveDown, canMoveDown))
            requestAction(RootAction::MoveDown, index);
    }

    ContextMenuSeparator();

    const bool hasFolder = getRootFolder(index) >= 0;

    if (hasFolder)
    {
        if (ContextMenuItem(ICON_LC_FOLDER_SEARCH, messages::files::cSearchInFolder))
            requestAction(RootAction::SearchInFolder, index);

        ContextMenuSeparator();
    }

    if (hasFolder && ContextMenuItem(ICON_LC_REFRESH_CW, messages::files::cRescan, !isRootScanning(index)))
        requestAction(RootAction::Rescan, index);

    if (ContextMenuItem(ICON_LC_FOLDER_OPEN, messages::recent::cShowInFileManager, !root.isMissing))
    {
        if (root.isFile)
            ShowInFileManager(root.path);
        else
            OpenFolder(root.path);
    }

    if (ContextMenuItem(ICON_LC_COPY, messages::mainmenu::cCopyPath))
        ImGui::SetClipboardText(root.path.c_str());

    ContextMenuSeparator();

    if (ContextMenuItem(ICON_LC_X, messages::files::cRemoveFromBrowser))
        requestAction(RootAction::Remove, index);

    EndContextMenu();
}

void FilesPanel::drawFolderMenu(s32 root, s32 folder)
{
    if (!BeginContextMenu(cFolderMenuId))
        return;

    const std::string path = getFolderPath(root, folder);
    const s32 pack = findPackAncestor(root, folder);

    if (pack < 0)
    {
        drawBrowserItem(path, mRoots[root].section);
        ContextMenuSeparator();
    }

    if (ContextMenuItem(ICON_LC_FOLDER_SEARCH, messages::files::cSearchInFolder))
        requestAction(RootAction::SearchInFolder, root, getFolderKey(root, folder));

    ContextMenuSeparator();

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

void FilesPanel::drawBrowserItem(const std::string& path, u32 section)
{
    const s32 root = findRoot(fileio::CanonicalPath(path.c_str()));

    if (root >= 0)
    {
        if (ContextMenuItem(ICON_LC_X, messages::files::cRemoveFromBrowser))
            requestAction(RootAction::Remove, root);

        return;
    }

    if (ContextMenuItem(ICON_LC_PLUS, messages::files::cAddToBrowser))
        requestAction(RootAction::Open, -1, path, section);
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
        text.format(messages::files::cIndexingFormat, FormatCountText(index.getFoundCount()).cstr());
    else
        text.format(messages::files::cReadingFormat, FormatCountText(index.getScannedCount()).cstr(), FormatCountText(index.getFoundCount()).cstr());

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
        requestAction(RootAction::ChooseFolder, -1);

    ImGui::SetItemTooltip("%s", messages::files::cDropAddTip);

    const bool isHovered = ImGui::IsItemHovered();

    if (isHovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    const ImU32 color = ImGui::GetHoverColorU32(ImGui::GetItemID(), isHovered, ImGui::GetColorU32(ImGuiCol_TextDisabled), ImGui::GetColorU32(ImGuiCol_Text));

    drawRowLabel(labelPos, labelPos.x + labelWidth, ICON_LC_FOLDER_PLUS, color, messages::files::cAddFolder, color, nullptr, 0);
}
