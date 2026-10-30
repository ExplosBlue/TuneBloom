#include <ui/browser/FilesPanel.h>

#include <ui/widgets/ContextMenu.h>
#include <ui/file/Document.h>
#include <ui/file/DocumentMgr.h>
#include <ui/file/DocumentState.h>
#include <ui/file/File.h>
#include <ui/file/FileFlow.h>
#include <ui/browser/FilesPanelInternal.h>
#include <ui/items/ItemColumns.h>
#include <ui/widgets/RowSelection.h>
#include <messages/Messages.h>
#include <ui/items/Selection.h>
#include <ui/app/Shortcuts.h>
#include <ui/settings/Theme.h>
#include <ui/widgets/Widgets.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

static constexpr f32 cSectionGapEm = 0.35f;

static constexpr const char* cKindMenuId = "##FilesKinds";

struct KindFilterInfo
{
    const char* label;
    BrowserFileKind kinds[2];
    s32 kindCount;
};

static const KindFilterInfo cKindFilters[cKindFilterCount] = {
    { messages::common::cArchives,     { BrowserFileKind::SoundArchive,  BrowserFileKind::SoundArchive },   1 },
    { messages::files::cKindResources, { BrowserFileKind::AudioResource, BrowserFileKind::ResourceList },   2 },
    { messages::common::cWaves,        { BrowserFileKind::Wave,          BrowserFileKind::Wave },           1 },
    { messages::common::cStreams,      { BrowserFileKind::Stream,        BrowserFileKind::StreamPrefetch }, 2 },
    { messages::common::cWav,          { BrowserFileKind::Wav,           BrowserFileKind::Wav },            1 },
};

KindFilter FilesPanel::getKindFilter(BrowserFileKind kind)
{
    for (s32 filter = 0; filter < cKindFilterCount; filter++)
    {
        for (s32 i = 0; i < cKindFilters[filter].kindCount; i++)
        {
            if (cKindFilters[filter].kinds[i] == kind)
                return static_cast<KindFilter>(filter);
        }
    }

    return KindFilter::Archives;
}

const char* GetFileKindIcon(BrowserFileKind kind)
{
    switch (kind)
    {
        case BrowserFileKind::SoundArchive:
            return ICON_LC_PACKAGE;

        case BrowserFileKind::AudioResource:
            return ICON_LC_FILE_ARCHIVE;

        case BrowserFileKind::ResourceList:
            return ICON_LC_LIST_MUSIC;

        case BrowserFileKind::Wave:
            return ICON_LC_FILE_AUDIO;

        case BrowserFileKind::Wav:
            return ICON_LC_AUDIO_WAVEFORM;

        default:
            break;
    }

    return ICON_LC_DISC_3;
}

ImVec4 GetFileKindColor(BrowserFileKind kind)
{
    return GetSeriesColor(static_cast<s32>(FilesPanel::getKindFilter(kind)));
}

bool FilesPanel::isPreviewable(const FileProbe& probe)
{
    return probe.kind == BrowserFileKind::Wave || probe.kind == BrowserFileKind::Stream || probe.kind == BrowserFileKind::Wav;
}

bool FilesPanel::isOpenable(const FileProbe& probe)
{
    return probe.kind != BrowserFileKind::Wav;
}

static void SelectOpenedSound(const std::string& name)
{
    Bfsar* archive = FindActiveSoundArchive();

    if (!archive)
        return;

    for (Item* sound : archive->getSoundList())
    {
        if (std::strcmp(sound->getName().cstr(), name.c_str()) == 0)
        {
            SelectItem(sound);
            return;
        }
    }
}

static FilesPanel sFilesPanel;

static std::mutex sRequestedRootMutex;

struct RootRequest
{
    std::string path;
    bool isAnnounced = false;
};

struct DropTarget
{
    ImVec2 min;
    ImVec2 max;
    s32 frame = -1;
};

static std::vector<RootRequest> sRequestedRoots;

static DropTarget sDropTarget;

static bool sIsFolderDialogRequested = false;

static std::vector<RootRequest> TakeRequestedRoots()
{
    const std::lock_guard<std::mutex> lock(sRequestedRootMutex);

    std::vector<RootRequest> roots;
    roots.swap(sRequestedRoots);

    return roots;
}

static void UpdateDropTarget()
{
    const std::lock_guard<std::mutex> lock(sRequestedRootMutex);

    if (ImGui::GetWindowViewport() != ImGui::GetMainViewport())
    {
        sDropTarget.frame = -1;
        return;
    }

    const ImVec2 origin = ImGui::GetMainViewport()->Pos;

    sDropTarget.min = ImGui::GetWindowPos() - origin;
    sDropTarget.max = sDropTarget.min + ImGui::GetWindowSize();
    sDropTarget.frame = ImGui::GetFrameCount();
}

bool FilesPanel::isFileShown(const BrowserFile& file) const
{
    return mKindShown[static_cast<s32>(getKindFilter(file.probe.kind))];
}

void FilesPanel::drawKindMenu()
{
    if (!BeginContextMenu(cKindMenuId))
        return;

    u32 counts[cKindFilterCount] = {};

    for (s32 root = 0; root < static_cast<s32>(mRoots.size()); root++)
    {
        for (s32 kind = 0; kind < static_cast<s32>(BrowserFileKind::Count); kind++)
            counts[static_cast<s32>(getKindFilter(static_cast<BrowserFileKind>(kind)))] += getSnapshot(root).kindCounts[kind];
    }

    for (s32 filter = 0; filter < cKindFilterCount; filter++)
    {
        sead::FormatFixedSafeString<64> label(messages::files::cKindFilterFormat, cKindFilters[filter].label, FormatCountText(counts[filter]).cstr());

        if (!ContextMenuCheckItem(label.cstr(), mKindShown[filter], counts[filter] > 0))
            continue;

        mKindShown[filter] = !mKindShown[filter];
        invalidateRows();
    }

    ContextMenuSeparator();

    if (ContextMenuCheckItem(messages::browser::cAutoPlay, mAutoPlay))
        setAutoPlay(!mAutoPlay);

    if (ContextMenuCheckItem(messages::browser::cGroupPrefix, mGroupByPrefix))
    {
        mGroupByPrefix = !mGroupByPrefix;
        invalidateRows();
        saveConfig();
    }

    if (!mSections.empty())
    {
        const bool isAnyOpen = std::any_of(mSections.begin(), mSections.end(), [](const FilesSection& section) { return !section.isFolded; });

        ContextMenuSeparator();

        if (ContextMenuItem(isAnyOpen ? ICON_LC_CHEVRONS_DOWN_UP : ICON_LC_CHEVRONS_UP_DOWN, isAnyOpen ? messages::files::cFoldAll : messages::files::cUnfoldAll))
            foldAllSections(isAnyOpen);
    }

    EndContextMenu();
}

bool FilesPanel::isAnyKindHidden() const
{
    for (bool isShown : mKindShown)
    {
        if (!isShown)
            return true;
    }

    return false;
}

static f32 CalcFooterHeight(bool hasRoots)
{
    return hasRoots ? ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y : 0.0f;
}

void FilesPanel::drawEmptyState(const char* text)
{
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const f32 areaHeight = avail.y - CalcFooterHeight(!mRoots.empty());
    const f32 areaEndY = ImGui::GetCursorPosY() + areaHeight + ImGui::GetStyle().ItemSpacing.y;

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + sead::Mathf::max(ImFloor((areaHeight - ImGui::GetFontSize()) * 0.5f), 0.0f));
    CenteredTextWrapped(text, avail.x);
    ImGui::SetCursorPosY(sead::Mathf::max(areaEndY, ImGui::GetCursorPosY()));
}

void FilesPanel::drawTable(FrameContext& ctx)
{
    const f32 footerHeight = CalcFooterHeight(!mRoots.empty());
    const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings;

    ctx.tableWidth = sead::Mathf::max(ImGui::GetContentRegionAvail().x, 1.0f);

    PushItemTablePadding();
    setupColumns(ctx);

    if (!ImGui::BeginTable("##FilesTable", cFilesColumnCount, flags, ImVec2(ctx.tableWidth, -footerHeight)))
    {
        ImGui::PopStyleVar();
        return;
    }

    ImGui::TableSetupColumn(messages::common::cName, ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn(messages::common::cFormat, ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize | (ctx.showFormat ? 0 : ImGuiTableColumnFlags_Disabled), ctx.formatWidth);
    ImGui::TableSetupColumn(messages::common::cSize, ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize | (ctx.showSize ? 0 : ImGuiTableColumnFlags_Disabled), ctx.sizeWidth);

    handleKeys(ctx);

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<s32>(ctx.rows.size()));

    if (mScrollToSelected)
    {
        for (s32 i = 0; i < static_cast<s32>(ctx.rows.size()); i++)
        {
            if (ctx.rows[i].isSame(mSelected))
                clipper.IncludeItemByIndex(i);
        }
    }

    while (clipper.Step())
    {
        for (s32 i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
        {
            const FilesRow& row = ctx.rows[i];

            const FilesRowKey key = row.key();

            ImGui::PushID(static_cast<s32>(ImHashData(&key, sizeof(key))));
            ImGui::TableNextRow();
            drawRow(ctx, row);

            if (mScrollToSelected && row.isSame(mSelected))
            {
                ImGui::SetScrollHereY();
                mScrollToSelected = false;
            }

            ImGui::PopID();
        }
    }

    mScrollToSelected = false;

    drawDropLine(ctx);
    drawAreaMenu();

    ImGui::EndTable();
    ImGui::PopStyleVar();
}

void FilesPanel::drawRow(FrameContext& ctx, const FilesRow& row)
{
    recordDropRow(ctx, row);

    switch (row.kind)
    {
        case FilesRowKind::Root:
            drawRootRow(ctx, row);
            break;

        case FilesRowKind::Folder:
            drawFolderRow(ctx, row);
            break;

        case FilesRowKind::Prefix:
            drawPrefixRow(ctx, row);
            break;

        case FilesRowKind::File:
            drawFileRow(ctx, row);
            break;

        case FilesRowKind::Sound:
            drawSoundRow(ctx, row);
            break;

        case FilesRowKind::Status:
            drawStatusRow(ctx, row);
            break;

        case FilesRowKind::Section:
            drawSectionRow(ctx, row);
            break;

        case FilesRowKind::AddFolder:
            drawAddFolderRow(ctx);
            break;
    }
}

void FilesPanel::drawFooter(FrameContext& ctx)
{
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetStyle().CellPadding.x);

    if (drawSelectionSummary())
        return;

    const u32 total = ctx.totalFileCount;

    if (ctx.shownSoundCount > 0)
    {
        drawMatchSummary(ctx);
        return;
    }

    if (ctx.shownFileCount != total)
    {
        ImGui::TextDisabled(messages::files::cShownFormat, FormatCountText(ctx.shownFileCount).cstr(), FormatCountText(total).cstr());
        return;
    }

    sead::FixedSafeString<32> size;
    FormatItemSize(&size, ctx.totalSize);

    if (total == 1)
        ImGui::TextDisabled(messages::files::cSummaryOneFormat, size.cstr());
    else
        ImGui::TextDisabled(messages::files::cSummaryFormat, FormatCountText(total).cstr(), size.cstr());
}

void FilesPanel::drawMatchSummary(const FrameContext& ctx)
{
    const sead::FormatFixedSafeString<32> sounds(ctx.shownSoundCount == 1 ? messages::files::cSoundCountOne : messages::files::cSoundCountFormat, FormatCountText(ctx.shownSoundCount).cstr());

    if (ctx.shownFileCount == 0)
    {
        ImGui::TextDisabled("%s", sounds.cstr());
        return;
    }

    const sead::FormatFixedSafeString<32> files(ctx.shownFileCount == 1 ? messages::files::cFileCountOne : messages::common::cFileCountFormat, FormatCountText(ctx.shownFileCount).cstr());

    ImGui::TextDisabled(messages::common::cPairFormat, files.cstr(), sounds.cstr());
}

bool FilesPanel::drawSelectionSummary()
{
    if (mSelectedSorted.size() < 2)
        return false;

    sead::FixedSafeString<32> size;
    FormatItemSize(&size, mSelectedSize);

    ImGui::TextDisabled(messages::files::cSelectedFormat, FormatCountText(mSelectedSorted.size()).cstr(), size.cstr());

    return true;
}

static bool IsSelectableRow(const FilesRow& row)
{
    return row.kind != FilesRowKind::Status && row.kind != FilesRowKind::AddFolder;
}

static s32 SkipInertRows(const std::vector<FilesRow>& rows, s32 index, s32 step)
{
    while (index >= 0 && index < static_cast<s32>(rows.size()) && !IsSelectableRow(rows[index]))
        index += step;

    return index < static_cast<s32>(rows.size()) ? index : -1;
}

void FilesPanel::handleKeys(FrameContext& ctx)
{
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::GetIO().WantTextInput)
        return;

    const bool isCollapsePressed = shortcuts::Pressed(shortcuts::Action::CollapseFolder);
    const bool isExpandPressed = shortcuts::Pressed(shortcuts::Action::ExpandFolder);

    if (ctx.rows.empty())
        return;

    s32 current = -1;

    for (s32 i = 0; i < static_cast<s32>(ctx.rows.size()); i++)
    {
        if (ctx.rows[i].isSame(mSelected))
            current = i;
    }

    s32 next = current;
    s32 step = 1;

    if (shortcuts::Pressed(shortcuts::Action::SelectNext))
    {
        next = current + 1;
    }
    else if (shortcuts::Pressed(shortcuts::Action::SelectPrevious))
    {
        next = current - 1;
        step = -1;
    }
    else if (shortcuts::Pressed(shortcuts::Action::SelectPageDown))
    {
        next = current + cRowSelectionPageStep;
    }
    else if (shortcuts::Pressed(shortcuts::Action::SelectPageUp))
    {
        next = current - cRowSelectionPageStep;
        step = -1;
    }
    else if (shortcuts::Pressed(shortcuts::Action::SelectFirst))
    {
        next = 0;
    }
    else if (shortcuts::Pressed(shortcuts::Action::SelectLast))
    {
        next = static_cast<s32>(ctx.rows.size()) - 1;
        step = -1;
    }
    else if (current >= 0 && isCollapsePressed)
    {
        next = collapseRow(ctx, current);
        step = -1;
    }
    else if (current >= 0 && isExpandPressed)
    {
        next = expandRow(ctx, current);
    }

    if (next != current)
    {
        const s32 target = std::clamp(next, 0, static_cast<s32>(ctx.rows.size()) - 1);
        next = SkipInertRows(ctx.rows, target, step);

        if (next < 0)
            next = SkipInertRows(ctx.rows, target, -step);
    }

    if (next >= 0 && next != current)
    {
        select(ctx.rows[next]);
        mScrollToSelected = true;
    }

    if (current >= 0 && shortcuts::Pressed(shortcuts::Action::ScrollToSelection))
        mScrollToSelected = true;

    if (shortcuts::Pressed(shortcuts::Action::SelectAll))
        selectAll(ctx);

    if (!mSelectedPaths.empty() && shortcuts::Pressed(shortcuts::Action::ClearSelection))
        clearMultiSelection();

    if (current >= 0 && shortcuts::Pressed(shortcuts::Action::ConfirmSelection))
        confirmSelection(ctx);

    if (current < 0 || mSelectedSorted.size() > 1)
        return;

    const FilesRow& row = ctx.rows[current];
    const bool isSection = row.kind == FilesRowKind::Section;

    if (shortcuts::Pressed(shortcuts::Action::DeleteSelection))
    {
        if (isSection)
            requestAction(RootAction::RemoveSection, -1, std::string(), mSections[row.index].id);
        else if (row.kind == FilesRowKind::Root)
            requestAction(RootAction::Remove, row.root);
    }

    if (isSection && shortcuts::Pressed(shortcuts::Action::RenameSelection))
        mRenameRequest = mSections[row.index].id;
}

void FilesPanel::confirmSelection(const FrameContext& ctx)
{
    const BrowserFile* file = getRowFile(mSelected);

    if (file && mSelectedSorted.size() >= 2 && isPathSelected(file->path))
        openSelectedFiles();
    else
        confirmRow(ctx, mSelected);
}

void FilesPanel::openSelectedFiles()
{
    for (const BrowserFile* file : collectSelectedFiles())
    {
        if (isOpenable(file->probe))
            openFile(*file);
    }
}

bool FilesPanel::isExpandable(const FilesRow& row) const
{
    if (row.kind == FilesRowKind::Section)
        return !isSearching();

    if (isSearching())
        return row.childCount > 0;

    if (row.kind == FilesRowKind::Root)
        return getRootFolder(row.root) >= 0;

    return row.kind == FilesRowKind::Folder || row.kind == FilesRowKind::Prefix;
}

const BrowserFile* FilesPanel::getRowFile(const FilesRow& row) const
{
    if (row.root < 0)
        return nullptr;

    if (row.kind == FilesRowKind::File)
        return &getFile(row);

    return row.kind == FilesRowKind::Root ? getRootFile(row.root) : nullptr;
}

bool FilesPanel::isRowOpen(const FilesRow& row) const
{
    if (row.kind == FilesRowKind::Section)
        return isSearching() || !mSections[row.index].isFolded;

    if (isSearching())
        return isSearchRowOpen(row);

    if (row.kind == FilesRowKind::Root)
        return mRoots[row.root].isOpen && isExpandable(row);

    if (row.kind == FilesRowKind::Prefix)
    {
        const std::vector<std::string>& openPrefixes = mRoots[row.root].openPrefixes;

        return std::find(openPrefixes.begin(), openPrefixes.end(), makePrefixKey(row)) != openPrefixes.end();
    }

    return row.kind == FilesRowKind::Folder && mRoots[row.root].expanded[row.index] != 0;
}

void FilesPanel::setRowOpen(const FilesRow& row, bool isOpen)
{
    invalidateRows();

    if (row.kind == FilesRowKind::Section)
    {
        if (!isSearching())
            setSectionFolded(mSections[row.index].id, !isOpen);

        return;
    }

    if (isSearching())
    {
        setSearchRowOpen(row, isOpen);
        return;
    }

    if (row.kind == FilesRowKind::Folder)
    {
        mRoots[row.root].expanded[row.index] = isOpen ? 1 : 0;
        saveConfig();
        return;
    }

    if (row.kind == FilesRowKind::Prefix)
    {
        std::vector<std::string>& openPrefixes = mRoots[row.root].openPrefixes;
        const std::string key = makePrefixKey(row);

        std::erase(openPrefixes, key);

        if (isOpen)
            openPrefixes.push_back(key);

        saveConfig();
        return;
    }

    if (row.kind != FilesRowKind::Root || mRoots[row.root].isOpen == isOpen)
        return;

    mRoots[row.root].isOpen = isOpen;
    saveConfig();
}

s32 FilesPanel::collapseRow(const FrameContext& ctx, s32 current)
{
    const FilesRow& row = ctx.rows[current];

    if (isRowOpen(row))
    {
        setRowOpen(row, false);
        return current;
    }

    for (s32 i = current - 1; i >= 0; i--)
    {
        if (ctx.rows[i].depth < row.depth)
            return i;
    }

    return current;
}

s32 FilesPanel::expandRow(const FrameContext& ctx, s32 current)
{
    const FilesRow& row = ctx.rows[current];

    if (!isExpandable(row))
        return current;

    if (!isRowOpen(row))
    {
        setRowOpen(row, true);
        return current;
    }

    const bool hasChild = current + 1 < static_cast<s32>(ctx.rows.size()) && ctx.rows[current + 1].depth > row.depth;

    return hasChild ? current + 1 : current;
}

void FilesPanel::confirmRow(const FrameContext& ctx, const FilesRow& row)
{
    if (const BrowserFile* file = getRowFile(row))
    {
        openFile(*file);
        return;
    }

    switch (row.kind)
    {
        case FilesRowKind::Section:
        case FilesRowKind::Root:
        case FilesRowKind::Folder:
        case FilesRowKind::Prefix:
            if (isExpandable(row))
                setRowOpen(row, !isRowOpen(row));
            break;

        case FilesRowKind::Sound:
            openSound(ctx, getFile(row), getFile(row).probe.contents.sounds[row.sound]);
            break;

        default:
            break;
    }
}

void FilesPanel::select(const FilesRow& row, bool reportsReadFailure)
{
    const BrowserFile* file = getRowFile(row);

    mSelected = row;
    mSelectAnchor = file ? file->path : std::string();
    clearMultiSelection();

    if (!mAutoPlay)
        return;

    if (file)
        previewFile(*file, reportsReadFailure);
    else
        mPreview.stop();
}

void FilesPanel::clickSelection(const FrameContext& ctx, const FilesRow& row, const BrowserFile& file)
{
    const BrowserFile* focusFile = getRowFile(mSelected);
    std::string focus = focusFile ? focusFile->path : std::string();

    const RowSelection<std::string> selection { &mSelectedPaths, &mSelectAnchor, &focus };

    ClickRowSelection(selection, collectSelectablePaths(ctx), file.path, shortcuts::Held(shortcuts::Modifier::ToggleSelection), shortcuts::Held(shortcuts::Modifier::ExtendSelection), true);

    if (focus == file.path)
        mSelected = row;
    else if (focus.empty())
        mSelected = FilesRow();

    onSelectionChanged();
}

void FilesPanel::selectAll(const FrameContext& ctx)
{
    mSelectedPaths = collectSelectablePaths(ctx);

    if (!mSelectedPaths.empty())
        mSelectAnchor = mSelectedPaths.front();

    onSelectionChanged();
}

void FilesPanel::clearMultiSelection()
{
    mSelectedPaths.clear();
    mSelectedSorted.clear();
    mSelectedSize = 0;
    mCanExportSelection = false;
}

void FilesPanel::onSelectionChanged()
{
    std::vector<std::string> shown = collectSelectablePaths(mFrame);
    std::sort(shown.begin(), shown.end());

    std::erase_if(mSelectedPaths, [&shown](const std::string& path) { return !std::binary_search(shown.begin(), shown.end(), path); });

    mSelectedSorted = mSelectedPaths;
    std::sort(mSelectedSorted.begin(), mSelectedSorted.end());
    mSelectedSorted.erase(std::unique(mSelectedSorted.begin(), mSelectedSorted.end()), mSelectedSorted.end());

    mSelectedSize = 0;
    mCanExportSelection = false;

    for (const BrowserFile* file : collectSelectedFiles())
    {
        mSelectedSize += file->size;
        mCanExportSelection |= CanExportFileContents(file->probe);
    }
}

bool FilesPanel::isPathSelected(const std::string& path) const
{
    return std::binary_search(mSelectedSorted.begin(), mSelectedSorted.end(), path);
}

bool FilesPanel::isRowSelected(const FilesRow& row) const
{
    if (mSelectedSorted.empty())
        return mSelected.isSame(row);

    const BrowserFile* file = getRowFile(row);

    return file && isPathSelected(file->path);
}

std::vector<std::string> FilesPanel::collectSelectablePaths(const FrameContext& ctx) const
{
    std::vector<std::string> paths;

    for (const FilesRow& row : ctx.rows)
    {
        if (const BrowserFile* file = getRowFile(row))
            paths.push_back(file->path);
    }

    return paths;
}

std::vector<const BrowserFile*> FilesPanel::collectSelectedFiles() const
{
    std::vector<const BrowserFile*> files;
    std::vector<u8> isCollected(mSelectedSorted.size(), 0);

    for (const FilesRow& row : mFrame.rows)
    {
        const BrowserFile* file = getRowFile(row);

        if (!file)
            continue;

        const auto it = std::lower_bound(mSelectedSorted.begin(), mSelectedSorted.end(), file->path);

        if (it == mSelectedSorted.end() || *it != file->path || isCollected[it - mSelectedSorted.begin()])
            continue;

        isCollected[it - mSelectedSorted.begin()] = 1;
        files.push_back(file);
    }

    return files;
}

void FilesPanel::setAutoPlay(bool isOn)
{
    mAutoPlay = isOn;
    saveConfig();

    if (!mAutoPlay)
        mPreview.stop();
}

void FilesPanel::previewFile(const BrowserFile& file, bool reportsReadFailure)
{
    if (isPreviewable(file.probe))
        mPreview.play(file.path, file.stem, reportsReadFailure);
    else
        mPreview.stop();
}

void FilesPanel::openFile(const BrowserFile& file)
{
    if (isOpenable(file.probe))
        RequestOpenFile(file.path.c_str(), openqueue::Origin::Browser);
    else
        previewFile(file);
}

void FilesPanel::showFile(const BrowserFile& file)
{
    if (isOpenable(file.probe))
        RequestPreviewFile(file.path.c_str());
}

void FilesPanel::openSound(const FrameContext& ctx, const BrowserFile& file, const ArchiveSound& sound)
{
    mPendingSound = { file.path, sound.name, ctx.openPath };

    if (ctx.openPath != file.path)
        RequestOpenFile(file.path.c_str(), openqueue::Origin::Browser);
}

void FilesPanel::selectPendingSound(const FrameContext& ctx)
{
    if (mPendingSound.path.empty())
        return;

    if (ctx.openPath == mPendingSound.path)
        SelectOpenedSound(mPendingSound.name);
    else if (ctx.openPath == mPendingSound.openPathAtRequest)
        return;

    mPendingSound = PendingSound();
}

void FilesPanel::collectOpenFiles(FrameContext& ctx)
{
    ctx.openFiles.clear();
    ctx.openPath.clear();

    for (const Document* document : GetOpenDocuments())
    {
        const DocumentState& state = document->state();

        if (state.canonicalPath.empty())
            continue;

        const bool isActive = document == ActiveDocument();

        ctx.openFiles.push_back({ state.canonicalPath, isActive, state.unsaved });

        if (isActive)
            ctx.openPath = state.canonicalPath;
    }
}

const OpenFileMark* FilesPanel::findOpenFile(const FrameContext& ctx, const std::string& path)
{
    for (const OpenFileMark& mark : ctx.openFiles)
    {
        if (mark.path == path)
            return &mark;
    }

    return nullptr;
}

void FilesPanel::draw()
{
    applyUndo();
    applyPendingAction();
    applyPendingDrop();
    updateScans();
    mPreview.update();

    UpdateDropTarget();

    FrameContext& ctx = mFrame;
    ctx.em = ImGui::GetFontSize();
    ctx.chevronWidth = ImGui::CalcTextSize(ICON_LC_CHEVRON_RIGHT).x + ImFloor(ctx.em * cIconGapEm * 0.5f);

    collectOpenFiles(ctx);
    beginDrop(ctx);

    selectPendingSound(ctx);

    if (mRoots.empty() && mSections.empty())
    {
        updateRows(ctx);
        drawTable(ctx);
        drawDropTarget(ctx);
        return;
    }

    const bool menuPressed = drawSearchField();

    drawAddButton();

    if (menuPressed)
        ImGui::OpenPopup(cKindMenuId);

    drawKindMenu();
    ImGui::Dummy(ImVec2(0.0f, ImFloor(ctx.em * cSectionGapEm)));

    updateRows(ctx);

    if (ctx.rows.empty())
        drawEmptyState(messages::files::cNoMatch);
    else
        drawTable(ctx);

    drawFooter(ctx);
    drawDropTarget(ctx);
}

void LoadFilesPanelConfig()
{
    sFilesPanel.loadConfig();
}

void RequestFilesPanelRoot(const char* path, bool isAnnounced)
{
    const std::lock_guard<std::mutex> lock(sRequestedRootMutex);

    sRequestedRoots.push_back({ path, isAnnounced });
}

bool IsFilesPanelDropTarget(f32 x, f32 y)
{
    const std::lock_guard<std::mutex> lock(sRequestedRootMutex);

    return sDropTarget.frame >= ImGui::GetFrameCount() - 1 && x >= sDropTarget.min.x && y >= sDropTarget.min.y && x < sDropTarget.max.x && y < sDropTarget.max.y;
}

void UndoFilesPanelRemove(u32 token)
{
    sFilesPanel.requestUndo(token);
}

void RequestFilesPanelFolderDialog()
{
    sIsFolderDialogRequested = true;
}

bool TakeFilesPanelRequests()
{
    bool isShown = false;

    std::vector<std::string> announcedPaths;

    for (const RootRequest& request : TakeRequestedRoots())
    {
        if (request.isAnnounced)
            announcedPaths.push_back(request.path);
        else
            sFilesPanel.addPath(request.path);

        isShown = true;
    }

    sFilesPanel.addPaths(announcedPaths);

    if (sIsFolderDialogRequested)
    {
        sIsFolderDialogRequested = false;
        isShown |= sFilesPanel.chooseFolder();
    }

    return isShown;
}

void DrawFilesPanel()
{
    sFilesPanel.draw();
}
