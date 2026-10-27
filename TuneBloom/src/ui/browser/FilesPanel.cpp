#include <ui/browser/FilesPanel.h>

#include <ui/widgets/ContextMenu.h>
#include <ui/file/Document.h>
#include <ui/file/DocumentMgr.h>
#include <ui/file/DocumentState.h>
#include <ui/file/File.h>
#include <ui/file/FileFlow.h>
#include <ui/browser/FilesPanelInternal.h>
#include <ui/items/ItemColumns.h>
#include <ui/widgets/ListFilter.h>
#include <ui/Messages.h>
#include <ui/items/Selection.h>
#include <ui/app/Shortcuts.h>
#include <ui/settings/Theme.h>
#include <ui/widgets/Widgets.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
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
    { messages::files::cKindResources, { BrowserFileKind::AudioResource, BrowserFileKind::AudioResource },  1 },
    { messages::common::cWaves,        { BrowserFileKind::Wave,          BrowserFileKind::Wave },           1 },
    { messages::common::cStreams,      { BrowserFileKind::Stream,        BrowserFileKind::StreamPrefetch }, 2 },
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

        case BrowserFileKind::Wave:
            return ICON_LC_FILE_AUDIO;

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
    return probe.kind == BrowserFileKind::Wave || probe.kind == BrowserFileKind::Stream;
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

static std::vector<std::string> sRequestedRoots;

static bool sIsFolderDialogRequested = false;

static std::vector<std::string> TakeRequestedRoots()
{
    const std::lock_guard<std::mutex> lock(sRequestedRootMutex);

    std::vector<std::string> roots;
    roots.swap(sRequestedRoots);

    return roots;
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
        sead::FormatFixedSafeString<64> label(messages::files::cKindFilterFormat, cKindFilters[filter].label, counts[filter]);

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

void FilesPanel::drawEmptyState(const char* text)
{
    const ImVec2 avail = ImGui::GetContentRegionAvail();

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + sead::Mathf::max(ImFloor((avail.y - ImGui::GetFontSize()) * 0.5f), 0.0f));
    CenteredTextWrapped(text, avail.x);
}

void FilesPanel::drawTable(FrameContext& ctx)
{
    const f32 footerHeight = mRoots.empty() ? 0.0f : ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
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

    ImGui::EndTable();
    ImGui::PopStyleVar();
}

void FilesPanel::drawRow(FrameContext& ctx, const FilesRow& row)
{
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

        case FilesRowKind::Group:
            drawGroupRow(row);
            break;

        case FilesRowKind::AddFolder:
            drawAddFolderRow(ctx);
            break;
    }
}

void FilesPanel::drawFooter(FrameContext& ctx)
{
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetStyle().CellPadding.x);

    const u32 total = ctx.totalFileCount;

    if (ctx.shownSoundCount > 0)
    {
        drawMatchSummary(ctx);
        return;
    }

    if (ctx.shownFileCount != total)
    {
        ImGui::TextDisabled(messages::files::cShownFormat, ctx.shownFileCount, total);
        return;
    }

    sead::FixedSafeString<32> size;
    FormatItemSize(&size, ctx.totalSize);

    if (total == 1)
        ImGui::TextDisabled(messages::files::cSummaryOneFormat, size.cstr());
    else
        ImGui::TextDisabled(messages::files::cSummaryFormat, total, size.cstr());
}

void FilesPanel::drawMatchSummary(const FrameContext& ctx)
{
    const sead::FormatFixedSafeString<32> sounds(ctx.shownSoundCount == 1 ? messages::files::cSoundCountOne : messages::files::cSoundCountFormat, ctx.shownSoundCount);

    if (ctx.shownFileCount == 0)
    {
        ImGui::TextDisabled("%s", sounds.cstr());
        return;
    }

    const sead::FormatFixedSafeString<32> files(ctx.shownFileCount == 1 ? messages::files::cFileCountOne : messages::common::cFileCountFormat, ctx.shownFileCount);

    ImGui::TextDisabled(messages::common::cPairFormat, files.cstr(), sounds.cstr());
}

static bool IsSelectableRow(const FilesRow& row)
{
    return row.kind != FilesRowKind::Status && row.kind != FilesRowKind::Group && row.kind != FilesRowKind::AddFolder;
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
        next = SkipInertRows(ctx.rows, std::clamp(next, 0, static_cast<s32>(ctx.rows.size()) - 1), step);

    if (next >= 0 && next != current)
    {
        select(ctx, ctx.rows[next]);
        mScrollToSelected = true;
    }

    if (current >= 0 && shortcuts::Pressed(shortcuts::Action::ConfirmSelection))
        confirmRow(ctx, mSelected);
}

bool FilesPanel::isRowOpen(const FilesRow& row) const
{
    if (row.kind == FilesRowKind::Root)
        return mRoots[row.root].isOpen;

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

    if (!ctx.isSearching && isRowOpen(row))
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

    if (row.kind != FilesRowKind::Folder && row.kind != FilesRowKind::Prefix && row.kind != FilesRowKind::Root)
        return current;

    if (!ctx.isSearching && !isRowOpen(row))
    {
        setRowOpen(row, true);
        return current;
    }

    const bool hasChild = current + 1 < static_cast<s32>(ctx.rows.size()) && ctx.rows[current + 1].depth > row.depth;

    return hasChild ? current + 1 : current;
}

void FilesPanel::confirmRow(const FrameContext& ctx, const FilesRow& row)
{
    switch (row.kind)
    {
        case FilesRowKind::Root:
        case FilesRowKind::Folder:
        case FilesRowKind::Prefix:
            if (!ctx.isSearching)
                setRowOpen(row, !isRowOpen(row));
            break;

        case FilesRowKind::File:
            openFile(getFile(row));
            break;

        case FilesRowKind::Sound:
            openSound(ctx, getFile(row), getFile(row).probe.contents.sounds[row.sound]);
            break;

        default:
            break;
    }
}

void FilesPanel::select(const FrameContext& ctx, const FilesRow& row)
{
    mSelected = row;

    if (!mAutoPlay)
        return;

    if (row.kind == FilesRowKind::File)
        previewFile(getFile(row));
    else
        mPreview.stop();
}

void FilesPanel::setAutoPlay(bool isOn)
{
    mAutoPlay = isOn;
    saveConfig();

    if (!mAutoPlay)
        mPreview.stop();
}

void FilesPanel::previewFile(const BrowserFile& file)
{
    if (isPreviewable(file.probe))
        mPreview.play(file.path, file.stem);
    else
        mPreview.stop();
}

void FilesPanel::openFile(const BrowserFile& file)
{
    RequestOpenFile(file.path.c_str(), openqueue::Origin::Browser);
}

void FilesPanel::showFile(const BrowserFile& file)
{
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
    applyPendingAction();
    updateScans();
    mPreview.update();
    mWavExport.update();

    FrameContext& ctx = mFrame;
    ctx.em = ImGui::GetFontSize();
    ctx.chevronWidth = ImGui::CalcTextSize(ICON_LC_CHEVRON_RIGHT).x + ImFloor(ctx.em * cIconGapEm * 0.5f);

    collectOpenFiles(ctx);

    selectPendingSound(ctx);

    if (mRoots.empty())
    {
        updateRows(ctx);
        drawTable(ctx);
        return;
    }

    bool menuPressed = false;

    SearchFieldExtra searchExtra;
    searchExtra.hint = messages::files::cSearchHint;
    searchExtra.menuPressed = &menuPressed;
    searchExtra.menuActive = isAnyKindHidden();

    SearchField("##FilesSearch", mSearch, sizeof(mSearch), 0.0f, nullptr, false, &searchExtra);

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
}

void LoadFilesPanelConfig()
{
    sFilesPanel.loadConfig();
}

void RequestFilesPanelRoot(const char* path)
{
    const std::lock_guard<std::mutex> lock(sRequestedRootMutex);

    sRequestedRoots.emplace_back(path);
}

void RequestFilesPanelFolderDialog()
{
    sIsFolderDialogRequested = true;
}

bool TakeFilesPanelRequests()
{
    bool isShown = false;

    for (const std::string& root : TakeRequestedRoots())
    {
        sFilesPanel.openFolder(root);
        isShown = true;
    }

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
