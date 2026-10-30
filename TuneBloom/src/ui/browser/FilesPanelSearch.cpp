#include <ui/browser/FilesPanelInternal.h>

#include <ui/widgets/ListFilter.h>
#include <ui/file/PathText.h>
#include <ui/app/Shortcuts.h>
#include <messages/Messages.h>

#include <imgui/imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

static bool ContainsIgnoreCase(const std::string& text, const char* query)
{
    const size_t queryLength = strlen(query);

    if (queryLength == 0)
        return true;

    const auto found = std::search(text.begin(), text.end(), query, query + queryLength, [](char a, char b)
    {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });

    return found != text.end();
}

bool FilesPanel::isSearching() const
{
    return mSearch[0] != '\0' || !mScope.rootPath.empty();
}

std::string FilesPanel::getSearchKey(const FilesRow& row) const
{
    if (row.kind == FilesRowKind::Root)
        return mRoots[row.root].path;

    if (row.kind == FilesRowKind::Folder)
        return getFolderPath(row.root, row.index);

    return getFile(row).path;
}

bool FilesPanel::isSearchRowOpen(const FilesRow& row) const
{
    if (!isExpandable(row))
        return false;

    const bool isToggled = std::find(mSearchToggled.begin(), mSearchToggled.end(), getSearchKey(row)) != mSearchToggled.end();

    return row.isClosedAtFirst == isToggled;
}

void FilesPanel::setSearchRowOpen(const FilesRow& row, bool isOpen)
{
    const std::string key = getSearchKey(row);

    std::erase(mSearchToggled, key);

    if (isOpen == row.isClosedAtFirst)
        mSearchToggled.push_back(key);
}

void FilesPanel::setSearchScope(s32 root, const std::string& folderKey)
{
    const s32 folder = folderKey.empty() ? getRootFolder(root) : findFolder(root, folderKey);

    if (folder < 0)
        return;

    mScope = SearchScope();
    mScope.rootPath = mRoots[root].path;
    mScope.folderKey = folderKey;
    mScope.name = folderKey.empty() ? mRoots[root].name : getSnapshot(root).folders[folder].name;
    mScope.path = folderKey.empty() ? mRoots[root].path : getFolderPath(root, folder);
    mFocusSearch = true;
    mScrollToSelected = true;

    invalidateRows();
}

void FilesPanel::clearSearchScope()
{
    mScope = SearchScope();

    invalidateRows();
}

void FilesPanel::resolveSearchScope()
{
    mScope.root = -1;
    mScope.folder = -1;

    if (mScope.rootPath.empty())
        return;

    const s32 root = findRoot(mScope.rootPath);

    if (root < 0)
    {
        clearSearchScope();
        return;
    }

    mScope.root = root;

    if (!mRoots[root].hasSnapshot || mRoots[root].isMissing)
        return;

    mScope.folder = mScope.folderKey.empty() ? getRootFolder(root) : findFolder(root, mScope.folderKey);

    if (mScope.folder < 0)
        clearSearchScope();
}

void FilesPanel::handleSearchKeys()
{
    if (!isSearching() || !mSelectedPaths.empty() || ImGui::GetIO().WantTextInput || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
        return;

    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || !shortcuts::Pressed(shortcuts::Action::CloseFilter))
        return;

    if (mSearch[0] != '\0')
        mSearch[0] = '\0';
    else
        clearSearchScope();

    mScrollToSelected = true;
}

bool FilesPanel::drawSearchField()
{
    bool menuPressed = false;
    bool scopeCleared = false;
    bool scopeHovered = false;

    SearchFieldExtra searchExtra;
    searchExtra.hint = messages::files::cSearchHint;
    searchExtra.menuPressed = &menuPressed;
    searchExtra.menuActive = isAnyKindHidden();

    if (!mScope.rootPath.empty())
    {
        searchExtra.scope = mScope.name.c_str();
        searchExtra.scopeClearTooltip = messages::files::cSearchAllFolders;
        searchExtra.scopeCleared = &scopeCleared;
        searchExtra.scopeHovered = &scopeHovered;
    }

    const f32 addWidth = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x;

    SearchField("##FilesSearch", mSearch, sizeof(mSearch), ImGui::GetContentRegionAvail().x - addWidth, nullptr, std::exchange(mFocusSearch, false), &searchExtra);

    if (scopeHovered && ImGui::BeginTooltip())
    {
        ImGui::TextUnformatted(mScope.name.c_str());
        pathtext::DrawTooltipFolder(mScope.path, ImGui::CalcTextSize(mScope.name.c_str()).x);
        ImGui::EndTooltip();
    }

    if (scopeCleared)
    {
        clearSearchScope();
        mFocusSearch = true;
    }

    handleSearchKeys();

    return menuPressed;
}

u32 FilesPanel::getSoundMatchCount(s32 root, s32 file) const
{
    if (root >= static_cast<s32>(mSoundMatches.starts.size()))
        return 0;

    const std::vector<u32>& starts = mSoundMatches.starts[root];

    return file + 1 < static_cast<s32>(starts.size()) ? starts[file + 1] - starts[file] : 0;
}

void FilesPanel::updateSoundMatches()
{
    if (mSoundMatches.generation == mSnapshotGeneration && mSoundMatches.query == mSearch)
        return;

    mSoundMatches.query = mSearch;
    mSoundMatches.generation = mSnapshotGeneration;
    mSoundMatches.rows.assign(mRoots.size(), {});
    mSoundMatches.starts.assign(mRoots.size(), {});

    for (s32 root = 0; root < static_cast<s32>(mRoots.size()); root++)
    {
        const std::vector<BrowserFile>& files = getSnapshot(root).files;
        std::vector<FilesRow>& rows = mSoundMatches.rows[root];
        std::vector<u32>& starts = mSoundMatches.starts[root];

        starts.assign(files.size() + 1, 0);

        for (s32 file = 0; file < static_cast<s32>(files.size()); file++)
        {
            const std::vector<ArchiveSound>& sounds = files[file].probe.contents.sounds;

            starts[file] = static_cast<u32>(rows.size());

            for (s32 sound = 0; sound < static_cast<s32>(sounds.size()) && mSearch[0] != '\0'; sound++)
            {
                if (!sounds[sound].name.empty() && ContainsIgnoreCase(sounds[sound].name, mSearch))
                    rows.push_back({ .kind = FilesRowKind::Sound, .root = root, .index = file, .sound = sound });
            }
        }

        starts[files.size()] = static_cast<u32>(rows.size());
    }
}

void FilesPanel::collectFileRootSearchRows(FrameContext& ctx, s32 root, bool isAllOpen)
{
    const BrowserFile* file = getRootFile(root);

    if (!file || !isFileShown(*file))
        return;

    const bool isNameMatch = ContainsIgnoreCase(file->name, mSearch);
    const std::vector<FilesRow>& soundRows = mSoundMatches.rows[root];

    if (!isNameMatch && soundRows.empty())
        return;

    const FilesRow rootRow = { .kind = FilesRowKind::Root, .root = root, .childCount = static_cast<u32>(soundRows.size()) };

    ctx.rows.push_back(rootRow);
    ctx.shownFileCount += isNameMatch ? 1 : 0;
    ctx.shownSoundCount += static_cast<u32>(soundRows.size());

    if (!isAllOpen && !isRowOpen(rootRow))
        return;

    for (FilesRow row : soundRows)
    {
        row.depth = 1;
        ctx.rows.push_back(row);
    }
}

void FilesPanel::countSearchHits(SearchCounts& counts, s32 root, s32 folder, bool isInherited) const
{
    const FileIndexSnapshot& snapshot = getSnapshot(root);
    const BrowserFolder& entry = snapshot.folders[folder];
    u32 listed = 0;
    u32 named = 0;
    u32 sounds = 0;

    counts.isInherited[folder] = isInherited ? 1 : 0;

    for (s32 child : entry.folders)
    {
        countSearchHits(counts, root, child, isInherited || ContainsIgnoreCase(snapshot.folders[child].name, mSearch));

        listed += counts.listed[child];
        named += counts.named[child];
        sounds += counts.sounds[child];
    }

    for (s32 file : entry.files)
    {
        const BrowserFile& entryFile = snapshot.files[file];

        if (!isFileShown(entryFile))
            continue;

        const bool isNamed = isInherited || ContainsIgnoreCase(entryFile.name, mSearch);
        const u32 soundCount = getSoundMatchCount(root, file);

        listed += isNamed || soundCount > 0 ? 1 : 0;
        named += isNamed ? 1 : 0;
        sounds += soundCount;
    }

    counts.listed[folder] = listed;
    counts.named[folder] = named;
    counts.sounds[folder] = sounds;
}

void FilesPanel::collectSearchFolderRows(FrameContext& ctx, const SearchCounts& counts, s32 root, s32 folder, s32 depth, bool isAllOpen)
{
    for (s32 child : getSnapshot(root).folders[folder].folders)
    {
        if (counts.listed[child] == 0)
            continue;

        const FilesRow row = { .kind = FilesRowKind::Folder, .root = root, .index = child, .depth = depth, .childCount = counts.listed[child], .isClosedAtFirst = counts.isInherited[child] != 0 };

        ctx.rows.push_back(row);

        if (isAllOpen || isRowOpen(row))
            collectSearchFolderRows(ctx, counts, root, child, depth + 1, isAllOpen);
    }

    collectSearchFileRows(ctx, counts, root, folder, depth, isAllOpen);
}

void FilesPanel::collectSearchFileRows(FrameContext& ctx, const SearchCounts& counts, s32 root, s32 folder, s32 depth, bool isAllOpen)
{
    const FileIndexSnapshot& snapshot = getSnapshot(root);
    const std::vector<FilesRow>& soundRows = mSoundMatches.rows[root];
    const bool isInherited = counts.isInherited[folder] != 0;
    const size_t firstFile = ctx.rows.size();

    for (s32 file : snapshot.folders[folder].files)
    {
        const BrowserFile& entry = snapshot.files[file];
        const u32 soundCount = getSoundMatchCount(root, file);

        if (!isFileShown(entry) || (!isInherited && soundCount == 0 && !ContainsIgnoreCase(entry.name, mSearch)))
            continue;

        const FilesRow row = { .kind = FilesRowKind::File, .root = root, .index = file, .depth = depth, .childCount = soundCount };

        ctx.rows.push_back(row);

        if (soundCount == 0 || (!isAllOpen && !isRowOpen(row)))
            continue;

        const u32 start = mSoundMatches.starts[root][file];

        for (u32 i = start; i < start + soundCount; i++)
        {
            FilesRow sound = soundRows[i];
            sound.depth = depth + 1;
            ctx.rows.push_back(sound);
        }
    }

    markSharedPrefixes(ctx.rows, firstFile);
}

void FilesPanel::collectSearchRows(FrameContext& ctx, s32 root, bool isAllOpen)
{
    if (!mScope.rootPath.empty() && root != mScope.root)
        return;

    if (mRoots[root].isFile && getRootFolder(root) < 0)
    {
        collectFileRootSearchRows(ctx, root, isAllOpen);
        return;
    }

    const s32 start = mScope.root == root ? mScope.folder : getRootFolder(root);

    if (start < 0 || !mRoots[root].hasSnapshot || mRoots[root].isMissing)
        return;

    const size_t folderCount = getSnapshot(root).folders.size();

    SearchCounts counts;
    counts.listed.assign(folderCount, 0);
    counts.named.assign(folderCount, 0);
    counts.sounds.assign(folderCount, 0);
    counts.isInherited.assign(folderCount, 0);

    countSearchHits(counts, root, start, false);

    if (counts.listed[start] == 0)
        return;

    const bool isScopeFolder = start != getRootFolder(root);
    const FilesRow topRow = { .kind = isScopeFolder ? FilesRowKind::Folder : FilesRowKind::Root, .root = root, .index = isScopeFolder ? start : -1, .childCount = counts.listed[start] };

    ctx.rows.push_back(topRow);
    ctx.shownFileCount += counts.named[start];
    ctx.shownSoundCount += counts.sounds[start];

    if (isAllOpen || isRowOpen(topRow))
        collectSearchFolderRows(ctx, counts, root, start, 1, isAllOpen);
}

void FilesPanel::countScopeFiles(FrameContext& ctx) const
{
    if (mScope.folder < 0)
        return;

    const FileIndexSnapshot& snapshot = getSnapshot(mScope.root);
    std::vector<s32> folders = { mScope.folder };

    ctx.totalFileCount = 0;
    ctx.totalSize = 0;

    while (!folders.empty())
    {
        const BrowserFolder& entry = snapshot.folders[folders.back()];
        folders.pop_back();
        folders.insert(folders.end(), entry.folders.begin(), entry.folders.end());

        for (s32 file : entry.files)
        {
            ctx.totalFileCount++;
            ctx.totalSize += snapshot.files[file].size;
        }
    }
}
