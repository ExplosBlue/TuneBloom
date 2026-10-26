#include <ui/browser/FilesPanel.h>

#include <ui/settings/ConfigFile.h>
#include <ui/widgets/ContextMenu.h>
#include <ui/file/Document.h>
#include <ui/file/DocumentMgr.h>
#include <ui/file/DocumentState.h>
#include <ui/file/File.h>
#include <ui/file/FileDialogs.h>
#include <ui/file/FileFlow.h>
#include <ui/browser/FilesPanelInternal.h>
#include <ui/items/ItemColumns.h>
#include <ui/widgets/ListFilter.h>
#include <ui/Messages.h>
#include <ui/items/Selection.h>
#include <ui/app/Shortcuts.h>
#include <ui/settings/Theme.h>
#include <ui/widgets/Widgets.h>

#include <bfsar/io/FileIo.h>
#include <libs/IconsLucide.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static constexpr f32 cEmptyGapEm    = 0.6f;
static constexpr f32 cSectionGapEm  = 0.35f;
static constexpr u32 cAutoExpandMax = 3;

static constexpr const char* cConfigSection  = "browser";
static constexpr const char* cRootKey        = "root";
static constexpr const char* cAutoPlayKey    = "autoPlay";
static constexpr const char* cCacheFolder    = "browser";
static constexpr const char* cCacheExtension = ".cache";
static constexpr const char* cKindMenuId     = "##FilesKinds";

struct KindFilterInfo
{
    const char* label;
    BrowserFileKind kinds[2];
    s32 kindCount;
};

static const KindFilterInfo cKindFilters[cKindFilterCount] = {
    { messages::files::cKindArchives,  { BrowserFileKind::SoundArchive,  BrowserFileKind::SoundArchive },   1 },
    { messages::files::cKindResources, { BrowserFileKind::AudioResource, BrowserFileKind::AudioResource },  1 },
    { messages::files::cKindWaves,     { BrowserFileKind::Wave,          BrowserFileKind::Wave },           1 },
    { messages::files::cKindStreams,   { BrowserFileKind::Stream,        BrowserFileKind::StreamPrefetch }, 2 },
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

static std::string MakeCachePath(const std::string& root)
{
    const sead::FormatFixedSafeString<32> name("%016llx%s", static_cast<unsigned long long>(std::hash<std::string>()(root)), cCacheExtension);

    return (fs::path(GetConfigDir()) / cCacheFolder / name.cstr()).string();
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

static std::string sRequestedRoot;

static bool TakeRequestedRoot(std::string* out)
{
    const std::lock_guard<std::mutex> lock(sRequestedRootMutex);

    if (sRequestedRoot.empty())
        return false;

    *out = std::move(sRequestedRoot);
    sRequestedRoot.clear();

    return true;
}

void FilesPanel::loadConfig()
{
    json::Value section;
    LoadConfigSection(cStateFile, cConfigSection, &section);

    mAutoPlay = section.getBool(cAutoPlayKey, mAutoPlay);
    mColumnWidth.load(section);

    if (const std::string* root = section.findString(cRootKey))
        setRoot(*root);
}

void FilesPanel::saveConfig() const
{
    json::Value section;
    section.setString(cRootKey, mRoot.c_str());
    section.setBool(cAutoPlayKey, mAutoPlay);
    mColumnWidth.save(&section);

    SaveConfigSection(cStateFile, cConfigSection, section);
}

void FilesPanel::setRoot(const std::string& root)
{
    mRoot = fileio::CanonicalPath(root.c_str());
    mSelected = FilesRow();
    mSearch[0] = '\0';

    std::error_code error;
    mRootMissing = mRoot.empty() || !fs::is_directory(mRoot, error);

    if (mRootMissing)
    {
        mIndex.cancel();
        return;
    }

    mIndex.scan(mRoot, MakeCachePath(mRoot));
}

void FilesPanel::chooseFolder()
{
    sead::FixedSafeString<1024> path;

    if (!SelectFolderDialog(&path, messages::files::cChooseFolderTitle))
        return;

    openFolder(path.cstr());
}

void FilesPanel::updateColumnWidth()
{
    mIsColumnWidthDirty |= mColumnWidth.update(ImGui::FindWindowByName(cFilesWindowId));

    if (!mIsColumnWidthDirty || ImGui::IsMouseDown(ImGuiMouseButton_Left))
        return;

    mIsColumnWidthDirty = false;
    saveConfig();
}

void FilesPanel::openFolder(const std::string& root)
{
    setRoot(root);
    saveConfig();
}

void FilesPanel::syncSnapshot()
{
    if (!mIndex.update())
        return;

    mSelected = FilesRow();
    mSnapshotGeneration++;
    resetExpanded();
    updateSizeLabelLengths();
}

void FilesPanel::resetExpanded()
{
    const FileIndexSnapshot& snapshot = mIndex.getSnapshot();

    mExpanded.assign(snapshot.folders.size(), 0);

    if (snapshot.folders.empty())
        return;

    mExpanded[0] = 1;

    s32 folder = 0;

    while (snapshot.folders[folder].folders.size() == 1 && snapshot.folders[folder].files.empty())
    {
        folder = snapshot.folders[folder].folders[0];
        mExpanded[folder] = 1;
    }

    if (snapshot.folders[folder].folders.size() > cAutoExpandMax)
        return;

    for (s32 child : snapshot.folders[folder].folders)
        mExpanded[child] = 1;
}

bool FilesPanel::isFileShown(const BrowserFile& file) const
{
    return mKindShown[static_cast<s32>(getKindFilter(file.probe.kind))];
}

void FilesPanel::drawRootRow(FrameContext& ctx)
{
    const f32 frame = ImGui::GetFrameHeight();
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 buttonsWidth = frame * 2.0f + style.ItemSpacing.x * 2.0f;
    const f32 rootWidth = sead::Mathf::max(ImGui::GetContentRegionAvail().x - buttonsWidth, frame);
    const std::string rootName = fs::path(mRoot).filename().string();

    if (ImGui::Button("##FilesRoot", ImVec2(rootWidth, frame)))
        chooseFolder();

    ImGui::SetItemTooltip("%s", mRoot.c_str());

    const ImVec2 rootMin = ImGui::GetItemRectMin();
    const ImVec2 labelPos(rootMin.x + style.FramePadding.x, rootMin.y + style.FramePadding.y);

    drawRowLabel(labelPos, rootMin.x + rootWidth - style.FramePadding.x, ICON_LC_FOLDER_OPEN, ImGui::GetColorU32(GetItemValueColor()), rootName.c_str(), ImGui::GetColorU32(ImGuiCol_Text), nullptr, 0);

    ImGui::SameLine();
    ImGui::BeginDisabled(mIndex.isScanning() || mRootMissing);

    if (DrawIconButton("##FilesRescan", ICON_LC_REFRESH_CW, ImVec2(frame, frame), frame, ctx.em, false))
        setRoot(mRoot);

    ImGui::EndDisabled();

    ImGui::SetItemTooltip("%s", messages::files::cRescan);

    ImGui::SameLine();
    if (DrawIconButton("##FilesChoose", ICON_LC_FOLDER_SEARCH, ImVec2(frame, frame), frame, ctx.em, false))
        chooseFolder();

    ImGui::SetItemTooltip("%s", messages::files::cChooseFolder);
}

void FilesPanel::drawKindMenu(FrameContext& ctx)
{
    if (!BeginContextMenu(cKindMenuId))
        return;

    u32 counts[cKindFilterCount] = {};

    for (s32 kind = 0; kind < static_cast<s32>(BrowserFileKind::Count); kind++)
        counts[static_cast<s32>(getKindFilter(static_cast<BrowserFileKind>(kind)))] += ctx.snapshot->kindCounts[kind];

    for (s32 filter = 0; filter < cKindFilterCount; filter++)
    {
        sead::FormatFixedSafeString<64> label(messages::files::cKindFilterFormat, cKindFilters[filter].label, counts[filter]);

        if (ContextMenuCheckItem(label.cstr(), mKindShown[filter], counts[filter] > 0))
            mKindShown[filter] = !mKindShown[filter];
    }

    ContextMenuSeparator();

    if (ContextMenuCheckItem(messages::browser::cAutoPlay, mAutoPlay))
        setAutoPlay(!mAutoPlay);

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

void FilesPanel::drawEmptyState(const char* text, const char* detail, bool showButton)
{
    const f32 em = ImGui::GetFontSize();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const f32 gap = ImFloor(em * cEmptyGapEm);
    const f32 contentHeight = em * 2.0f + gap + (showButton ? ImGui::GetFrameHeight() + gap : 0.0f);
    const f32 startX = ImGui::GetCursorPosX();

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + sead::Mathf::max(ImFloor((avail.y - contentHeight) * 0.5f), 0.0f));

    CenteredTextWrapped(text, avail.x);

    if (detail)
    {
        ImGui::SetCursorPosX(startX);
        ImGui::PushStyleColor(ImGuiCol_Text, GetItemValueColor());
        CenteredTextWrapped(detail, avail.x);
        ImGui::PopStyleColor();
    }

    if (!showButton)
        return;

    ImGui::Dummy(ImVec2(0.0f, gap));

    sead::FormatFixedSafeString<128> label(ICON_LC_FOLDER_SEARCH "  %s", messages::files::cChooseFolder);
    const f32 buttonWidth = ImGui::CalcTextSize(label.cstr()).x + ImGui::GetStyle().FramePadding.x * 2.0f;

    ImGui::SetCursorPosX(startX + sead::Mathf::max(ImFloor((avail.x - buttonWidth) * 0.5f), 0.0f));

    if (ImGui::Button(label.cstr()))
        chooseFolder();
}

void FilesPanel::drawTable(FrameContext& ctx)
{
    const f32 footerHeight = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings;

    PushItemTablePadding();
    setupColumns(ctx);

    if (!ImGui::BeginTable("##FilesTable", cFilesColumnCount, flags, ImVec2(0.0f, -footerHeight)))
    {
        ImGui::PopStyleVar();
        return;
    }

    ImGui::TableSetupColumn(messages::files::cColumnName, ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn(messages::files::cColumnFormat, ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize | (ctx.showFormat ? 0 : ImGuiTableColumnFlags_Disabled), ctx.formatWidth);
    ImGui::TableSetupColumn(messages::files::cColumnSize, ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize | (ctx.showSize ? 0 : ImGuiTableColumnFlags_Disabled), ctx.sizeWidth);

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

            if (row.kind == FilesRowKind::Folder)
                drawFolderRow(ctx, row);
            else if (row.kind == FilesRowKind::File)
                drawFileRow(ctx, row);
            else
                drawSoundRow(ctx, row);

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

void FilesPanel::drawFooter(FrameContext& ctx)
{
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetStyle().CellPadding.x);

    if (mIndex.isScanning())
    {
        ImGui::TextDisabled(messages::files::cIndexingFormat, mIndex.getScannedCount());
        return;
    }

    const u32 total = static_cast<u32>(ctx.snapshot->files.size());

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
    FormatItemSize(&size, ctx.snapshot->totalSize);

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

    const sead::FormatFixedSafeString<32> files(ctx.shownFileCount == 1 ? messages::files::cFileCountOne : messages::files::cFileCountFormat, ctx.shownFileCount);

    ImGui::TextDisabled(messages::files::cMatchSummaryFormat, files.cstr(), sounds.cstr());
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

    if (shortcuts::Pressed(shortcuts::Action::SelectNext))
        next = current + 1;
    else if (shortcuts::Pressed(shortcuts::Action::SelectPrevious))
        next = current - 1;
    else if (shortcuts::Pressed(shortcuts::Action::SelectFirst))
        next = 0;
    else if (shortcuts::Pressed(shortcuts::Action::SelectLast))
        next = static_cast<s32>(ctx.rows.size()) - 1;
    else if (current >= 0 && isCollapsePressed)
        next = collapseRow(ctx, current);
    else if (current >= 0 && isExpandPressed)
        next = expandRow(ctx, current);

    next = std::clamp(next, 0, static_cast<s32>(ctx.rows.size()) - 1);

    if (next != current)
    {
        select(ctx, ctx.rows[next]);
        mScrollToSelected = true;
    }

    if (current >= 0 && shortcuts::Pressed(shortcuts::Action::ConfirmSelection))
        confirmRow(ctx, mSelected);
}

s32 FilesPanel::collapseRow(const FrameContext& ctx, s32 current)
{
    const FilesRow& row = ctx.rows[current];

    if (row.kind == FilesRowKind::Folder && mExpanded[row.index])
    {
        mExpanded[row.index] = 0;
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

    if (row.kind != FilesRowKind::Folder)
        return current;

    if (!mExpanded[row.index])
    {
        mExpanded[row.index] = 1;
        return current;
    }

    const bool hasChild = current + 1 < static_cast<s32>(ctx.rows.size()) && ctx.rows[current + 1].depth > row.depth;

    return hasChild ? current + 1 : current;
}

void FilesPanel::confirmRow(const FrameContext& ctx, const FilesRow& row)
{
    if (row.kind == FilesRowKind::Folder)
    {
        mExpanded[row.index] = mExpanded[row.index] ? 0 : 1;
        return;
    }

    const BrowserFile& file = ctx.snapshot->files[row.index];

    if (row.kind == FilesRowKind::File)
        openFile(file);
    else
        openSound(ctx, file, file.probe.contents.sounds[row.sound]);
}

void FilesPanel::select(const FrameContext& ctx, const FilesRow& row)
{
    mSelected = row;

    if (!mAutoPlay)
        return;

    if (row.kind == FilesRowKind::File)
        previewFile(ctx.snapshot->files[row.index]);
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
    RequestOpenFile(file.path.c_str());
}

void FilesPanel::showFile(const BrowserFile& file)
{
    RequestPreviewFile(file.path.c_str());
}

void FilesPanel::openSound(const FrameContext& ctx, const BrowserFile& file, const ArchiveSound& sound)
{
    mPendingSound = { file.path, sound.name, ctx.openPath };

    if (ctx.openPath != file.path)
        RequestOpenFile(file.path.c_str());
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
    syncSnapshot();
    mPreview.update();

    FrameContext ctx;
    ctx.snapshot = &mIndex.getSnapshot();
    ctx.em = ImGui::GetFontSize();
    ctx.chevronWidth = ImGui::CalcTextSize(ICON_LC_CHEVRON_RIGHT).x + ImFloor(ctx.em * cIconGapEm * 0.5f);

    collectOpenFiles(ctx);

    selectPendingSound(ctx);

    if (mRoot.empty())
    {
        drawEmptyState(messages::files::cEmpty, messages::files::cEmptyDetail, true);
        return;
    }

    drawRootRow(ctx);

    if (mRootMissing)
    {
        drawEmptyState(messages::files::cRootMissing, nullptr, true);
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

    drawKindMenu(ctx);
    ImGui::Dummy(ImVec2(0.0f, ImFloor(ctx.em * cSectionGapEm)));

    collectRows(ctx);

    if (ctx.rows.empty() && !mIndex.isScanning())
        drawEmptyState(ctx.isSearching ? messages::files::cNoMatch : messages::files::cNoAudio, nullptr, false);
    else
        drawTable(ctx);

    drawFooter(ctx);
}

void LoadFilesPanelConfig()
{
    sFilesPanel.loadConfig();
}

void SetFilesPanelRoot(const char* path)
{
    sFilesPanel.openFolder(path);
}

void RequestFilesPanelRoot(const char* path)
{
    const std::lock_guard<std::mutex> lock(sRequestedRootMutex);

    sRequestedRoot = path;
}

void DrawFilesPanel()
{
    std::string requestedRoot;

    if (TakeRequestedRoot(&requestedRoot))
    {
        sFilesPanel.openFolder(requestedRoot);
        ImGui::SetNextWindowFocus();
    }

    sFilesPanel.updateColumnWidth();

    if (ImGuiWindow* project = ImGui::FindWindowByName("###ProjectWindow"); project && project->DockId != 0)
        ImGui::SetNextWindowDockID(project->DockId, ImGuiCond_FirstUseEver);

    sead::FormatFixedSafeString<64> title(ICON_LC_FOLDER_TREE " %s%s", messages::files::cWindowTitle, cFilesWindowId);

    if (ImGui::Begin(title.cstr()))
        sFilesPanel.draw();

    ImGui::End();
}
