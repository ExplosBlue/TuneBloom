#include <ui/browser/FilesPanelInternal.h>

#include <ui/widgets/ContextMenu.h>
#include <ui/browser/FilesPanel.h>
#include <ui/items/ItemColumns.h>
#include <ui/app/Shortcuts.h>
#include <ui/Messages.h>
#include <ui/file/SystemShell.h>
#include <ui/settings/Theme.h>
#include <ui/settings/UiColors.h>
#include <ui/wave/WaveFormatUI.h>
#include <ui/widgets/Widgets.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

static constexpr f32 cCountGapEm     = 0.5f;
static constexpr f32 cNameMinEm        = 8.0f;
static constexpr f32 cNameComfortEm    = 20.0f;
static constexpr f32 cFolderCountChars = 4.0f;
static constexpr u32 cPrefixMinLength  = 3;
static constexpr u32 cPrefixGroupMin   = 2;
static constexpr u32 cGroupPrefixMin   = 2;
static constexpr char cPrefixSeparator = '_';

static constexpr const char* cRowMenuId       = "##menu";
static constexpr const char* cPrefixMarker    = "..";
static constexpr const char* cWidthSampleChar = "0";

struct SoundTypeInfo
{
    const char* icon;
    const char* label;
    const char* shortLabel;
};

static const SoundTypeInfo cSoundTypes[static_cast<s32>(ArchiveSoundType::Count)] = {
    { ICON_LC_DISC_3,      messages::item::cTypeStreamSound,   messages::common::cStream },
    { ICON_LC_AUDIO_LINES, messages::common::cWaveSound,       messages::common::cWave },
    { ICON_LC_MUSIC_3,     messages::item::cTypeSequenceSound, messages::common::cSequence },
    { ICON_LC_LIST_MUSIC,  messages::common::cSound,           messages::common::cSound },
};

struct CountRow
{
    const char* icon;
    const char* label;
    u32 count;
    s32 depth;
};

static const SoundTypeInfo& GetSoundType(ArchiveSoundType type)
{
    return cSoundTypes[static_cast<s32>(type)];
}

static s32 SharedPrefixLength(std::string_view left, std::string_view right)
{
    const size_t common = std::mismatch(left.begin(), left.begin() + std::min(left.size(), right.size()), right.begin()).first - left.begin();
    const size_t separator = left.rfind(cPrefixSeparator, common == 0 ? 0 : common - 1);

    if (common == 0 || separator == std::string_view::npos || separator + 1 < cPrefixMinLength)
        return 0;

    return static_cast<s32>(separator + 1);
}

static std::string_view GetGroupName(const std::string& stem)
{
    const size_t separator = stem.find(cPrefixSeparator);

    if (separator == std::string::npos || separator + 1 < cGroupPrefixMin || separator + 1 == stem.size())
        return stem;

    return std::string_view(stem).substr(0, separator);
}

static bool IsGroupBase(const std::string& stem)
{
    return GetGroupName(stem).size() == stem.size();
}

static std::string_view GetShownStem(const FileIndexSnapshot& snapshot, const FilesRow& row)
{
    return std::string_view(snapshot.files[row.index].stem).substr(row.nameStart);
}

static bool IsSiblingFileRow(const FilesRow& row, const FilesRow& other)
{
    return other.kind == FilesRowKind::File && other.depth == row.depth;
}

static s32 CalcHiddenPrefixLength(const char* name, s32 prefixLength, f32 width)
{
    const f32 markerWidth = ImGui::CalcTextSize(cPrefixMarker).x;

    for (s32 cut = 0; cut < prefixLength; cut++)
    {
        if (name[cut] == cPrefixSeparator && markerWidth + ImGui::CalcTextSize(name + cut + 1).x <= width)
            return cut + 1;
    }

    return prefixLength;
}

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

static bool IsContentRow(const FilesRow& row)
{
    return row.kind == FilesRowKind::Folder || row.kind == FilesRowKind::Prefix || row.kind == FilesRowKind::File || row.kind == FilesRowKind::Sound;
}

void FilesPanel::countShownFiles(FrameContext& ctx, s32 root, s32 folder)
{
    const FileIndexSnapshot& snapshot = getSnapshot(root);
    const BrowserFolder& entry = snapshot.folders[folder];
    std::vector<u32>& shownCounts = ctx.shownCounts[root];
    u32 count = 0;

    for (s32 child : entry.folders)
    {
        countShownFiles(ctx, root, child);
        count += shownCounts[child];
    }

    for (s32 file : entry.files)
    {
        if (isFileShown(snapshot.files[file]))
            count++;
    }

    shownCounts[folder] = count;
}

void FilesPanel::collectFolderRows(const FrameContext& ctx, std::vector<FilesRow>& rows, s32 root, s32 folder, s32 depth, bool isAllOpen)
{
    const FileIndexSnapshot& snapshot = getSnapshot(root);
    const BrowserFolder& entry = snapshot.folders[folder];

    for (s32 child : entry.folders)
    {
        if (ctx.shownCounts[root][child] == 0)
            continue;

        rows.push_back({ .kind = FilesRowKind::Folder, .root = root, .index = child, .depth = depth });

        if (isAllOpen || mRoots[root].expanded[child])
            collectFolderRows(ctx, rows, root, child, depth + 1, isAllOpen);
    }

    const size_t firstFile = rows.size();

    collectFileRows(rows, root, folder, depth, isAllOpen);

    for (size_t i = firstFile; i < rows.size(); i++)
    {
        FilesRow& row = rows[i];

        if (row.kind != FilesRowKind::File)
            continue;

        const std::string_view stem = GetShownStem(snapshot, row);
        const s32 previous = i > firstFile && IsSiblingFileRow(row, rows[i - 1]) ? SharedPrefixLength(stem, GetShownStem(snapshot, rows[i - 1])) : 0;
        const s32 next = i + 1 < rows.size() && IsSiblingFileRow(row, rows[i + 1]) ? SharedPrefixLength(stem, GetShownStem(snapshot, rows[i + 1])) : 0;

        row.prefixLength = std::max(previous, next);
    }
}

void FilesPanel::collectFileRows(std::vector<FilesRow>& rows, s32 root, s32 folder, s32 depth, bool isAllOpen)
{
    const FileIndexSnapshot& snapshot = getSnapshot(root);
    std::vector<s32> files;

    for (s32 file : snapshot.folders[folder].files)
    {
        if (isFileShown(snapshot.files[file]))
            files.push_back(file);
    }

    std::vector<s32> leaders(files.size(), -1);
    std::vector<u32> groupSizes(files.size(), 0);
    const auto stemOf = [&](s32 position) -> const std::string& { return snapshot.files[files[position]].stem; };

    if (mGroupByPrefix)
    {
        std::vector<s32> order(files.size());

        for (s32 i = 0; i < static_cast<s32>(order.size()); i++)
            order[i] = i;

        std::stable_sort(order.begin(), order.end(), [&](s32 left, s32 right) { return GetGroupName(stemOf(left)) < GetGroupName(stemOf(right)); });

        for (size_t start = 0; start < order.size();)
        {
            const std::string_view name = GetGroupName(stemOf(order[start]));
            bool hasPrefixedFile = false;
            size_t end = start;

            while (end < order.size() && GetGroupName(stemOf(order[end])) == name)
                hasPrefixedFile |= !IsGroupBase(stemOf(order[end++]));

            if (hasPrefixedFile && end - start >= cPrefixGroupMin)
            {
                for (size_t member = start; member < end; member++)
                    leaders[order[member]] = order[start];

                groupSizes[order[start]] = static_cast<u32>(end - start);
            }

            start = end;
        }
    }

    for (s32 i = 0; i < static_cast<s32>(files.size()); i++)
    {
        if (leaders[i] < 0)
        {
            rows.push_back({ .kind = FilesRowKind::File, .root = root, .index = files[i], .depth = depth });
            continue;
        }

        if (leaders[i] != i)
            continue;

        const BrowserFile& first = snapshot.files[files[i]];
        const s32 prefixLength = static_cast<s32>(GetGroupName(first.stem).size() + 1);
        FilesRow group = { .kind = FilesRowKind::Prefix, .root = root, .index = folder, .sound = files[i], .depth = depth, .nameStart = prefixLength, .childCount = groupSizes[i] };

        for (s32 member = i; member < static_cast<s32>(files.size()); member++)
        {
            if (leaders[member] != i)
                continue;

            const BrowserFile& file = snapshot.files[files[member]];

            group.groupSize += file.size;
            group.isMixedFormat |= strcmp(file.probe.formatName, first.probe.formatName) != 0;
        }

        rows.push_back(group);

        if (!isAllOpen && !isRowOpen(group))
            continue;

        for (s32 member = i; member < static_cast<s32>(files.size()); member++)
        {
            if (leaders[member] == i)
                rows.push_back({ .kind = FilesRowKind::File, .root = root, .index = files[member], .depth = depth + 1, .nameStart = IsGroupBase(stemOf(member)) ? 0 : prefixLength });
        }
    }
}

void FilesPanel::invalidateRows()
{
    mRowsGeneration++;
}

bool FilesPanel::isRowsKeyCurrent(const FrameContext& ctx) const
{
    const auto isSamePath = [](const std::string& path, const OpenFileMark& mark) { return path == mark.path; };

    return mRowsKey.rowsGeneration == mRowsGeneration && mRowsKey.snapshotGeneration == mSnapshotGeneration && mRowsKey.em == ctx.em && mRowsKey.search == mSearch
        && std::equal(mRowsKey.openPaths.begin(), mRowsKey.openPaths.end(), ctx.openFiles.begin(), ctx.openFiles.end(), isSamePath);
}

void FilesPanel::updateRows(FrameContext& ctx)
{
    if (isRowsKeyCurrent(ctx))
        return;

    mRowsKey.rowsGeneration = mRowsGeneration;
    mRowsKey.snapshotGeneration = mSnapshotGeneration;
    mRowsKey.em = ctx.em;
    mRowsKey.search = mSearch;
    mRowsKey.openPaths.clear();

    for (const OpenFileMark& mark : ctx.openFiles)
        mRowsKey.openPaths.push_back(mark.path);

    collectRows(ctx);

    std::vector<FilesRow> allOpenRows;
    const bool hasClosedRows = !ctx.isSearching || !mSearchClosed.empty();

    if (hasClosedRows)
        collectAllOpenRows(ctx, allOpenRows);

    const std::vector<FilesRow>& measureRows = hasClosedRows ? allOpenRows : ctx.rows;

    PushItemTablePadding();
    calcValueWidths(ctx, measureRows);
    ctx.nameWant = calcNameWant(ctx, measureRows);
    ImGui::PopStyleVar();

    if (!mSelectedPaths.empty())
        onSelectionChanged();
}

void FilesPanel::collectAllOpenRows(const FrameContext& ctx, std::vector<FilesRow>& rows)
{
    if (ctx.isSearching)
    {
        FrameContext search;
        std::vector<std::string> closed = std::exchange(mSearchClosed, {});

        for (s32 root = 0; root < static_cast<s32>(mRoots.size()); root++)
            collectSearchRows(search, root);

        mSearchClosed = std::move(closed);
        rows = std::move(search.rows);
        return;
    }

    for (s32 root = 0; root < static_cast<s32>(mRoots.size()); root++)
    {
        const FilesRoot& entry = mRoots[root];
        const s32 folder = getRootFolder(root);

        if (folder >= 0 && entry.isOpen && !entry.isMissing && entry.hasSnapshot && folder < static_cast<s32>(ctx.shownCounts[root].size()) && ctx.shownCounts[root][folder] > 0)
            collectFolderRows(ctx, rows, root, folder, 1, true);
    }
}

void FilesPanel::collectRows(FrameContext& ctx)
{
    ctx.isSearching = isSearching();
    ctx.rows.clear();
    ctx.shownCounts.resize(mRoots.size());
    ctx.shownFileCount = 0;
    ctx.shownSoundCount = 0;
    ctx.totalFileCount = 0;
    ctx.totalSize = 0;

    if (ctx.isSearching)
        updateSoundMatches();
    else
        mSearchClosed.clear();

    const bool hasPinned = !mRoots.empty() && mRoots.front().isPinned;

    for (s32 root = 0; root < static_cast<s32>(mRoots.size()); root++)
    {
        const FileIndexSnapshot& snapshot = getSnapshot(root);

        ctx.shownCounts[root].assign(snapshot.folders.size(), 0);

        if (!snapshot.folders.empty())
            countShownFiles(ctx, root, 0);

        if (!mRoots[root].isNested)
        {
            ctx.totalFileCount += static_cast<u32>(snapshot.files.size());
            ctx.totalSize += snapshot.totalSize;
        }

        if (ctx.isSearching)
        {
            collectSearchRows(ctx, root);
            continue;
        }

        if (root == 0 && hasPinned)
            ctx.rows.push_back({ .kind = FilesRowKind::Group, .index = 0 });
        else if (hasPinned && !mRoots[root].isPinned && mRoots[root - 1].isPinned)
            ctx.rows.push_back({ .kind = FilesRowKind::Group, .index = 1 });

        collectRootRows(ctx, root);
    }

    if (ctx.isSearching && !mRoots.empty())
        return;

    if (hasPinned && mRoots.back().isPinned)
        ctx.rows.push_back({ .kind = FilesRowKind::Group, .index = 1 });

    ctx.rows.push_back({ .kind = FilesRowKind::AddFolder });
}

void FilesPanel::collectRootRows(FrameContext& ctx, s32 root)
{
    const FilesRoot& entry = mRoots[root];
    const u32 shown = ctx.shownCounts[root].empty() ? 0 : ctx.shownCounts[root][0];
    const s32 folder = getRootFolder(root);

    ctx.shownFileCount += entry.isNested ? 0 : shown;
    ctx.rows.push_back({ .kind = FilesRowKind::Root, .root = root });

    if (!entry.isOpen || (entry.isFile && folder < 0))
        return;

    if (entry.isMissing || !entry.hasSnapshot || shown == 0)
    {
        ctx.rows.push_back({ .kind = FilesRowKind::Status, .root = root, .depth = 1 });
        return;
    }

    collectFolderRows(ctx, ctx.rows, root, folder, 1, false);
}

void FilesPanel::collectFileRootSearchRows(FrameContext& ctx, s32 root)
{
    const BrowserFile* file = getRootFile(root);

    if (!file || !isFileShown(*file))
        return;

    const bool isNameMatch = ContainsIgnoreCase(file->name, mSearch);
    const std::vector<FilesRow>& soundRows = mSoundMatches.rows[root];

    if (!isNameMatch && soundRows.empty())
        return;

    ctx.rows.push_back({ .kind = FilesRowKind::Root, .root = root, .childCount = static_cast<u32>(soundRows.size()) });
    ctx.shownFileCount += isNameMatch ? 1 : 0;
    ctx.shownSoundCount += static_cast<u32>(soundRows.size());

    if (isSearchClosed(mRoots[root].path))
        return;

    for (FilesRow row : soundRows)
    {
        row.depth = 1;
        ctx.rows.push_back(row);
    }
}

void FilesPanel::collectSearchRows(FrameContext& ctx, s32 root)
{
    if (mRoots[root].isFile && getRootFolder(root) < 0)
    {
        collectFileRootSearchRows(ctx, root);
        return;
    }

    const FileIndexSnapshot& snapshot = getSnapshot(root);
    const std::vector<FilesRow>& soundRows = mSoundMatches.rows[root];
    const std::vector<u8>& hasMatch = mSoundMatches.hasMatch[root];
    std::vector<FilesRow> rows;

    for (s32 file = 0; file < static_cast<s32>(snapshot.files.size()); file++)
    {
        const BrowserFile& entry = snapshot.files[file];

        if (!isFileShown(entry) || hasMatch[file] || !ContainsIgnoreCase(entry.name, mSearch))
            continue;

        rows.push_back({ .kind = FilesRowKind::File, .root = root, .index = file, .depth = 1 });
        ctx.shownFileCount++;
    }

    s32 archive = -1;
    size_t archiveRow = 0;
    bool isArchiveOpen = true;

    for (const FilesRow& row : soundRows)
    {
        const BrowserFile& entry = snapshot.files[row.index];

        if (!isFileShown(entry))
            continue;

        if (row.index != archive)
        {
            archive = row.index;
            archiveRow = rows.size();
            isArchiveOpen = !isSearchClosed(entry.path);
            rows.push_back({ .kind = FilesRowKind::File, .root = root, .index = archive, .depth = 1 });
            ctx.shownFileCount += ContainsIgnoreCase(entry.name, mSearch) ? 1 : 0;
        }

        rows[archiveRow].childCount++;
        ctx.shownSoundCount++;

        if (isArchiveOpen)
            rows.push_back(row);
    }

    if (rows.empty())
        return;

    ctx.rows.push_back({ .kind = FilesRowKind::Root, .root = root, .childCount = static_cast<u32>(rows.size()) });

    if (!isSearchClosed(mRoots[root].path))
        ctx.rows.insert(ctx.rows.end(), rows.begin(), rows.end());
}

void FilesPanel::updateSoundMatches()
{
    if (mSoundMatches.generation == mSnapshotGeneration && mSoundMatches.query == mSearch)
        return;

    mSoundMatches.query = mSearch;
    mSoundMatches.generation = mSnapshotGeneration;
    mSoundMatches.rows.assign(mRoots.size(), {});
    mSoundMatches.hasMatch.assign(mRoots.size(), {});

    for (s32 root = 0; root < static_cast<s32>(mRoots.size()); root++)
    {
        const std::vector<BrowserFile>& files = getSnapshot(root).files;

        mSoundMatches.hasMatch[root].assign(files.size(), 0);

        for (s32 file = 0; file < static_cast<s32>(files.size()); file++)
        {
            const std::vector<ArchiveSound>& sounds = files[file].probe.contents.sounds;

            for (s32 sound = 0; sound < static_cast<s32>(sounds.size()); sound++)
            {
                if (sounds[sound].name.empty() || !ContainsIgnoreCase(sounds[sound].name, mSearch))
                    continue;

                mSoundMatches.rows[root].push_back({ .kind = FilesRowKind::Sound, .root = root, .index = file, .sound = sound, .depth = 2 });
                mSoundMatches.hasMatch[root][file] = 1;
            }
        }
    }
}

const std::string& FilesPanel::getRowName(const FilesRow& row) const
{
    if (row.kind == FilesRowKind::Root)
        return mRoots[row.root].name;

    if (row.kind == FilesRowKind::Folder)
        return getSnapshot(row.root).folders[row.index].name;

    const BrowserFile& file = getFile(row);

    return row.kind == FilesRowKind::Sound ? file.probe.contents.sounds[row.sound].name : file.name;
}

f32 FilesPanel::calcRowNoteWidth(const FrameContext& ctx, const FilesRow& row) const
{
    const f32 gap = ImFloor(ctx.em * cCountGapEm);

    if (row.kind == FilesRowKind::Folder || row.kind == FilesRowKind::Prefix)
        return gap + ImGui::CalcTextSize(cWidthSampleChar).x * cFolderCountChars;

    if (row.kind == FilesRowKind::File && findOpenFile(ctx, getFile(row).path))
        return gap + ImGui::CalcTextSize(cOpenFileMark).x;

    return 0.0f;
}

f32 FilesPanel::calcNameWant(const FrameContext& ctx, const std::vector<FilesRow>& rows) const
{
    const f32 charWidth = ImGui::CalcTextSize(cWidthSampleChar).x;
    const f32 indent = ImFloor(ctx.em * cIndentEm);
    const f32 lead = ctx.chevronWidth + ctx.em + ImFloor(ctx.em * cIconGapEm) + ImGui::GetStyle().CellPadding.x * 2.0f;
    f32 widest = 0.0f;

    for (const FilesRow& row : rows)
    {
        if (!IsContentRow(row))
            continue;

        const size_t nameLength = row.kind == FilesRowKind::Prefix ? row.nameStart : getRowName(row).size() - row.nameStart;
        const f32 width = row.depth * indent + lead + nameLength * charWidth + calcRowNoteWidth(ctx, row);

        widest = sead::Mathf::max(widest, width);
    }

    return std::clamp(widest, ctx.em * cNameMinEm, ctx.em * cNameComfortEm);
}

void FilesPanel::calcValueWidths(FrameContext& ctx, const std::vector<FilesRow>& rows) const
{
    const char* widestFormat = nullptr;
    size_t widestFormatLength = 0;
    u64 widestSize = 0;
    s32 widestSizeLength = 0;
    sead::FixedSafeString<32> size;

    for (const FilesRow& row : rows)
    {
        const BrowserFolder* pack = getPackFolder(row);
        const char* format = nullptr;
        u64 rowSize = 0;
        s32 sizeLength = 0;

        if (pack)
        {
            format = pack->packFormat.c_str();
            rowSize = pack->packSize;
        }
        else if (row.kind == FilesRowKind::Prefix)
        {
            format = row.isMixedFormat ? "" : getSnapshot(row.root).files[row.sound].probe.formatName;
            rowSize = row.groupSize;
        }
        else if (row.kind == FilesRowKind::File)
        {
            format = getFile(row).probe.formatName;
            rowSize = getFile(row).size;
            sizeLength = mRoots[row.root].sizeLabelLengths[row.index];
        }
        else if (row.kind == FilesRowKind::Sound)
        {
            format = GetSoundType(getFile(row).probe.contents.sounds[row.sound].type).shortLabel;
        }
        else
        {
            continue;
        }

        if (pack || row.kind == FilesRowKind::Prefix)
        {
            FormatItemSize(&size, rowSize);
            sizeLength = size.calcLength();
        }

        const size_t formatLength = strlen(format);

        if (formatLength > widestFormatLength)
        {
            widestFormat = format;
            widestFormatLength = formatLength;
        }

        if (sizeLength > widestSizeLength)
        {
            widestSize = rowSize;
            widestSizeLength = sizeLength;
        }
    }

    ctx.formatWidth = widestFormat ? CalcItemValueCellWidth(widestFormat) : 0.0f;
    ctx.sizeWidth = 0.0f;

    if (widestSizeLength == 0)
        return;

    FormatItemSize(&size, widestSize);

    ctx.sizeWidth = CalcItemValueCellWidth(size.cstr());
}

void FilesPanel::setupColumns(FrameContext& ctx)
{
    const f32 cellPadding = ImGui::GetStyle().CellPadding.x * 2.0f;
    const f32 avail = ctx.tableWidth;
    const f32 nameWant = ctx.nameWant;
    const f32 formatWidth = ctx.formatWidth + cellPadding;
    const f32 sizeWidth = ctx.sizeWidth + cellPadding;

    ctx.showSize = ctx.sizeWidth > 0.0f && avail >= nameWant + sizeWidth;
    ctx.showFormat = ctx.showSize && ctx.formatWidth > 0.0f && avail >= nameWant + sizeWidth + formatWidth;
}

f32 FilesPanel::drawRowLabel(const ImVec2& pos, f32 right, const char* icon, ImU32 iconColor, const char* name, ImU32 nameColor, const char* note, ImU32 noteColor, s32 prefixLength)
{
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const f32 em = ImGui::GetFontSize();
    const f32 iconWidth = ImGui::CalcTextSize(icon).x;
    const f32 nameX = pos.x + iconWidth + ImFloor(em * cIconGapEm);

    drawList->AddText(pos, iconColor, icon);

    const f32 noteGap = ImFloor(em * cCountGapEm);
    const f32 noteWidth = note ? noteGap + ImGui::CalcTextSize(note).x : 0.0f;
    const f32 nameEnd = drawRowName(drawList, ImVec2(nameX, pos.y), sead::Mathf::max(right - noteWidth, nameX), name, nameColor, prefixLength);

    if (!note)
        return nameEnd;

    drawList->AddText(ImVec2(nameEnd + noteGap, pos.y), noteColor, note);

    return nameEnd + noteWidth;
}

f32 FilesPanel::drawRowName(ImDrawList* drawList, const ImVec2& pos, f32 right, const char* name, ImU32 color, s32 prefixLength)
{
    const ImU32 prefixColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const char* rest = name + prefixLength;
    const bool isFull = pos.x + ImGui::CalcTextSize(name).x <= right;
    const s32 hiddenLength = isFull ? 0 : CalcHiddenPrefixLength(name, prefixLength, right - pos.x);
    f32 restX = pos.x;

    if (hiddenLength > 0)
    {
        drawList->AddText(ImVec2(restX, pos.y), prefixColor, cPrefixMarker);
        restX += ImGui::CalcTextSize(cPrefixMarker).x;
    }

    if (hiddenLength < prefixLength)
    {
        drawList->AddText(ImVec2(restX, pos.y), prefixColor, name + hiddenLength, rest);
        restX += ImGui::CalcTextSize(name + hiddenLength, rest).x;
    }

    const f32 nameRight = sead::Mathf::min(restX + ImGui::CalcTextSize(rest).x, right);

    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::RenderTextEllipsis(drawList, ImVec2(restX, pos.y), ImVec2(nameRight, pos.y + ImGui::GetFontSize()), nameRight, nameRight, rest, nullptr, nullptr);
    ImGui::PopStyleColor();

    return nameRight;
}

FilesPanel::RowFrame FilesPanel::beginRow(FrameContext& ctx, const FilesRow& row, f32 indent)
{
    ImGui::TableSetColumnIndex(static_cast<s32>(FilesColumn::Name));
    ImGui::AlignTextToFramePadding();

    const ImVec2 cursor = ImGui::GetCursorScreenPos();

    RowFrame frame;
    frame.textPos = ImVec2(cursor.x + row.depth * ImFloor(ctx.em * cIndentEm) + indent, cursor.y + ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset);
    frame.right = cursor.x + ImGui::GetContentRegionAvail().x;

    const BrowserFile* file = getRowFile(row);
    const bool isSelectionClick = file && (shortcuts::Held(shortcuts::Modifier::ToggleSelection) || shortcuts::Held(shortcuts::Modifier::ExtendSelection));

    bool hovered = false;

    if (DrawTableRowSelectable("##row", isRowSelected(row), &hovered))
    {
        if (isSelectionClick)
        {
            clickSelection(ctx, row, *file);
        }
        else
        {
            select(ctx, row);
            frame.isPressed = true;
        }
    }

    frame.isDoubleClicked = hovered && !isSelectionClick && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    frame.rowMin = ImGui::GetItemRectMin();
    frame.rowMax = ImGui::GetItemRectMax();

    return frame;
}

bool FilesPanel::isChevronClicked(const FrameContext& ctx, const RowFrame& frame)
{
    return frame.isPressed && ImGui::GetIO().MouseClickedPos[ImGuiMouseButton_Left].x < frame.textPos.x + ctx.chevronWidth;
}

void FilesPanel::drawFolderRow(FrameContext& ctx, const FilesRow& row)
{
    const BrowserFolder& folder = getSnapshot(row.root).folders[row.index];
    const bool isExpanded = isRowOpen(row);
    const RowFrame frame = beginRow(ctx, row, 0.0f);

    if (frame.isPressed)
        setRowOpen(row, !isExpanded);

    ImGui::OpenPopupOnItemClick(cFolderMenuId, ImGuiPopupFlags_MouseButtonRight);

    const char* chevron = isExpanded ? ICON_LC_CHEVRON_DOWN : ICON_LC_CHEVRON_RIGHT;
    const char* folderIcon = isExpanded ? ICON_LC_FOLDER_OPEN : ICON_LC_FOLDER_CLOSED;
    const char* icon = folder.isPack ? ICON_LC_FOLDER_ARCHIVE : folderIcon;
    const ImVec4 iconColor = folder.isPack ? GetFileKindColor(BrowserFileKind::AudioResource) : GetItemValueColor();
    const sead::FixedSafeString<32> count = FormatCountText(ctx.shownCounts[row.root][row.index]);

    ImGui::GetWindowDrawList()->AddText(frame.textPos, ImGui::GetColorU32(ImGuiCol_TextDisabled), chevron);
    drawRowLabel(ImVec2(frame.textPos.x + ctx.chevronWidth, frame.textPos.y), frame.right, icon, ImGui::GetColorU32(iconColor), folder.name.c_str(), ImGui::GetColorU32(ImGuiCol_Text), count.cstr(), ImGui::GetColorU32(ImGuiCol_TextDisabled));

    if (folder.isPack)
    {
        sead::FixedSafeString<32> size;
        FormatItemSize(&size, folder.packSize);

        drawValueCells(ctx, folder.packFormat.c_str(), size.cstr());
    }

    drawFolderMenu(row.root, row.index);
}

void FilesPanel::drawPrefixRow(FrameContext& ctx, const FilesRow& row)
{
    const BrowserFile& first = getSnapshot(row.root).files[row.sound];
    const bool isExpanded = isRowOpen(row);
    const RowFrame frame = beginRow(ctx, row, 0.0f);

    if (frame.isPressed)
        setRowOpen(row, !isExpanded);

    const std::string name = std::string(GetGroupName(first.stem)) + cPrefixSeparator;
    const char* chevron = isExpanded ? ICON_LC_CHEVRON_DOWN : ICON_LC_CHEVRON_RIGHT;
    const ImU32 dimColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const sead::FixedSafeString<32> count = FormatCountText(row.childCount);
    sead::FixedSafeString<32> size;

    FormatItemSize(&size, row.groupSize);

    ImGui::GetWindowDrawList()->AddText(frame.textPos, dimColor, chevron);
    drawRowLabel(ImVec2(frame.textPos.x + ctx.chevronWidth, frame.textPos.y), frame.right, ICON_LC_FILE_STACK, ImGui::GetColorU32(GetFileKindColor(first.probe.kind)), name.c_str(), ImGui::GetColorU32(ImGuiCol_Text), count.cstr(), dimColor);
    drawValueCells(ctx, row.isMixedFormat ? "" : first.probe.formatName, size.cstr());
}

void FilesPanel::drawArchiveContents(const ArchiveContents& contents)
{
    const u32* typeCounts = contents.soundTypeCounts;

    const CountRow rows[] = {
        { GetSoundType(ArchiveSoundType::Unknown).icon,  messages::common::cSounds,          contents.soundCount,                                     0 },
        { GetSoundType(ArchiveSoundType::Stream).icon,   messages::common::cStream,          typeCounts[static_cast<s32>(ArchiveSoundType::Stream)],   1 },
        { GetSoundType(ArchiveSoundType::Wave).icon,     messages::common::cWave,            typeCounts[static_cast<s32>(ArchiveSoundType::Wave)],     1 },
        { GetSoundType(ArchiveSoundType::Sequence).icon, messages::common::cSequence,        typeCounts[static_cast<s32>(ArchiveSoundType::Sequence)], 1 },
        { ICON_LC_MUSIC_4,                               messages::project::cRowSoundSets,    contents.soundSetCount,                                  0 },
        { ICON_LC_PIANO,                                 messages::project::cRowBanks,        contents.bankCount,                                      0 },
        { ICON_LC_ARCHIVE,                               messages::project::cRowWaveArchives, contents.waveArchiveCount,                               0 },
        { ICON_LC_FOLDERS,                               messages::project::cRowGroups,       contents.groupCount,                                     0 },
        { ICON_LC_VOLUME_2,                              messages::project::cRowPlayers,      contents.playerCount,                                    0 },
    };

    f32 countWidth = 0.0f;

    for (const CountRow& row : rows)
        countWidth = sead::Mathf::max(countWidth, ImGui::CalcTextSize(FormatCountText(row.count).cstr()).x);

    if (!ImGui::BeginTable("##contents", 2, ImGuiTableFlags_SizingFixedFit))
        return;

    ImGui::TableSetupColumn("##label");
    ImGui::TableSetupColumn("##count", ImGuiTableColumnFlags_WidthFixed, countWidth);

    const f32 indent = ImFloor(ImGui::GetFontSize() * cIndentEm);

    for (const CountRow& row : rows)
    {
        const sead::FixedSafeString<32> count = FormatCountText(row.count);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + row.depth * indent);
        ImGui::TextDisabled("%s  %s", row.icon, row.label);

        ImGui::TableNextColumn();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + countWidth - ImGui::CalcTextSize(count.cstr()).x);
        ImGui::TextUnformatted(count.cstr());
    }

    ImGui::EndTable();
}

void FilesPanel::drawFileTooltip(const BrowserFile& file, bool showPath)
{
    if (!ImGui::BeginItemTooltip())
        return;

    const FileProbe& probe = file.probe;

    ImGui::TextUnformatted(file.name.c_str());
    ImGui::TextDisabled(messages::files::cFormatVersionFormat, probe.formatName, probe.versionText);

    if (probe.isCompressed)
        ImGui::TextDisabled("%s", messages::files::cCompressed);

    if (probe.hasItemCount)
        ImGui::TextDisabled(probe.itemCount == 1 ? messages::files::cAssetCountOne : messages::files::cAssetCountFormat, FormatCountText(probe.itemCount).cstr());

    if (probe.sampleRate > 0)
    {
        sead::FixedSafeString<32> length;
        sead::FixedSafeString<32> channels;

        FormatSampleTime(&length, probe.sampleCount, probe.sampleRate);
        FormatChannelCount(&channels, probe.channelCount);

        ImGui::TextDisabled("%s, %s", length.cstr(), channels.cstr());
        ImGui::TextDisabled(messages::unit::cHertzFormat, probe.sampleRate);
    }

    if (probe.isPrefetch)
        ImGui::TextDisabled("%s", messages::files::cStreamStart);

    if (probe.isLoop)
        ImGui::TextDisabled("%s", messages::common::cLoops);

    if (probe.kind == BrowserFileKind::SoundArchive)
        drawArchiveContents(probe.contents);

    if (showPath)
        ImGui::TextDisabled("%s", file.path.c_str());

    ImGui::EndTooltip();
}

void FilesPanel::drawSoundTooltip(const BrowserFile& file, const ArchiveSound& sound)
{
    if (!ImGui::BeginItemTooltip())
        return;

    ImGui::TextUnformatted(sound.name.c_str());
    ImGui::TextDisabled(messages::files::cSoundInFileFormat, GetSoundType(sound.type).label, file.name.c_str());
    ImGui::EndTooltip();
}

static std::optional<FileExportMode> DrawExportCommands(bool canExport)
{
    if (ContextMenuItem(ICON_LC_FILE_OUTPUT, messages::common::cExport, canExport, messages::files::cExportNeedsAudio))
        return FileExportMode::Files;

    if (ContextMenuItem(ICON_LC_FILE_AUDIO, messages::menu::cExportWav, canExport, messages::files::cExportNeedsAudio))
        return FileExportMode::Wav;

    return std::nullopt;
}

void FilesPanel::drawFileCommands(const BrowserFile& file)
{
    const bool isAudio = isPreviewable(file.probe);

    if (ContextMenuItem(ICON_LC_FILE_PEN, messages::common::cOpen))
        openFile(file);

    if (isAudio && ContextMenuItem(ICON_LC_PLAY, messages::common::cPlay))
        previewFile(file);

    ContextMenuSeparator();

    if (!CanExportFileContents(file.probe))
        return;

    if (const std::optional<FileExportMode> mode = DrawExportCommands(true))
        StartFileExport({ &file }, *mode);
}

bool FilesPanel::drawSelectionMenu(const BrowserFile& file)
{
    if (mSelectedSorted.size() < 2 || !isPathSelected(file.path))
        return false;

    if (ContextMenuItem(ICON_LC_FILE_PEN, messages::common::cOpen))
    {
        for (const BrowserFile* selected : collectSelectedFiles())
            openFile(*selected);
    }

    ContextMenuSeparator();

    if (const std::optional<FileExportMode> mode = DrawExportCommands(mCanExportSelection))
        StartFileExport(collectSelectedFiles(), *mode);

    return true;
}

void FilesPanel::drawFileMenu(const FilesRow& row)
{
    if (!BeginContextMenu(cRowMenuId))
        return;

    const BrowserFile& file = getFile(row);

    if (drawSelectionMenu(file))
    {
        EndContextMenu();
        return;
    }

    drawFileCommands(file);
    ContextMenuSeparator();

    if (findPackAncestor(row.root, file.folder) < 0)
    {
        drawPinItem(file.path);
        ContextMenuSeparator();
    }

    if (ContextMenuItem(ICON_LC_FOLDER_OPEN, messages::recent::cShowInFileManager))
        ShowInFileManager(file.path);

    if (ContextMenuItem(ICON_LC_COPY, messages::mainmenu::cCopyPath))
        ImGui::SetClipboardText(file.path.c_str());

    EndContextMenu();
}

void FilesPanel::drawValueCells(FrameContext& ctx, const char* format, const char* size)
{
    if (ctx.showFormat && ImGui::TableSetColumnIndex(static_cast<s32>(FilesColumn::Format)))
        DrawItemValueCell(format, cItemColumnFlag_Text);

    if (ctx.showSize && ImGui::TableSetColumnIndex(static_cast<s32>(FilesColumn::Size)))
        DrawItemValueCell(size, cItemColumnFlag_None);
}

void FilesPanel::drawFileRow(FrameContext& ctx, const FilesRow& row)
{
    const BrowserFile& file = getFile(row);
    const OpenFileMark* openMark = findOpenFile(ctx, file.path);
    const bool canOpen = isExpandable(row);
    const bool isExpanded = isRowOpen(row);
    const RowFrame frame = beginRow(ctx, row, 0.0f);

    if (canOpen && isChevronClicked(ctx, frame))
        setRowOpen(row, !isExpanded);
    else if (frame.isDoubleClicked)
        openFile(file);
    else if (frame.isPressed)
        showFile(file);

    ImGui::OpenPopupOnItemClick(cRowMenuId, ImGuiPopupFlags_MouseButtonRight);

    if (IsItemHoveredForDetail())
        drawFileTooltip(file);

    ImU32 iconColor = 0;
    const char* icon = getFileIcon(file, &iconColor);

    if (canOpen)
        ImGui::GetWindowDrawList()->AddText(frame.textPos, ImGui::GetColorU32(ImGuiCol_TextDisabled), isExpanded ? ICON_LC_CHEVRON_DOWN : ICON_LC_CHEVRON_RIGHT);

    drawRowLabel(ImVec2(frame.textPos.x + ctx.chevronWidth, frame.textPos.y), frame.right, icon, iconColor, file.name.c_str() + row.nameStart, getOpenNameColor(openMark), openMark ? cOpenFileMark : nullptr, getOpenMarkColor(openMark), row.prefixLength);

    sead::FixedSafeString<32> size;
    FormatItemSize(&size, file.size);

    drawValueCells(ctx, file.probe.formatName, size.cstr());
    drawFileMenu(row);
}

ImU32 FilesPanel::getOpenNameColor(const OpenFileMark* mark)
{
    return ImGui::GetColorU32(mark && mark->isActive ? GetAccentTextColor() : ImGui::GetStyleColorVec4(ImGuiCol_Text));
}

ImU32 FilesPanel::getOpenMarkColor(const OpenFileMark* mark)
{
    return ImGui::GetColorU32(mark && mark->isUnsaved ? uicolors::Warning() : GetAccentTextColor());
}

const char* FilesPanel::getFileIcon(const BrowserFile& file, ImU32* color) const
{
    const bool isPlaying = mPreview.isPlaying(file.path);

    *color = ImGui::GetColorU32(isPlaying ? uicolors::Playback() : GetFileKindColor(file.probe.kind));

    return isPlaying ? ICON_LC_VOLUME_2 : GetFileKindIcon(file.probe.kind);
}

void FilesPanel::drawSoundRow(FrameContext& ctx, const FilesRow& row)
{
    const BrowserFile& file = getFile(row);
    const ArchiveSound& sound = file.probe.contents.sounds[row.sound];
    const SoundTypeInfo& type = GetSoundType(sound.type);
    const RowFrame frame = beginRow(ctx, row, ctx.chevronWidth);

    if (frame.isDoubleClicked)
        openSound(ctx, file, sound);

    ImGui::OpenPopupOnItemClick(cRowMenuId, ImGuiPopupFlags_MouseButtonRight);

    if (IsItemHoveredForDetail())
        drawSoundTooltip(file, sound);

    const ImU32 iconColor = ImGui::GetColorU32(GetSeriesColor(static_cast<s32>(KindFilter::Archives)));

    drawRowLabel(frame.textPos, frame.right, type.icon, iconColor, sound.name.c_str(), ImGui::GetColorU32(ImGuiCol_Text), nullptr, 0);
    drawValueCells(ctx, type.shortLabel, "");
    drawSoundMenu(ctx, file, sound);
}

void FilesPanel::drawSoundMenu(const FrameContext& ctx, const BrowserFile& file, const ArchiveSound& sound)
{
    if (!BeginContextMenu(cRowMenuId))
        return;

    if (ContextMenuItem(ICON_LC_FILE_PEN, messages::common::cOpen))
        openSound(ctx, file, sound);

    ContextMenuSeparator();

    if (ContextMenuItem(ICON_LC_COPY, messages::files::cCopyName))
        ImGui::SetClipboardText(sound.name.c_str());

    EndContextMenu();
}
