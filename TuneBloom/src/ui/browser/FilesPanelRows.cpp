#include <ui/browser/FilesPanelInternal.h>

#include <ui/widgets/ContextMenu.h>
#include <ui/browser/FilesPanel.h>
#include <ui/items/ItemColumns.h>
#include <ui/app/Shortcuts.h>
#include <messages/Messages.h>
#include <ui/file/PathText.h>
#include <ui/file/SystemShell.h>
#include <ui/settings/Theme.h>
#include <ui/settings/UiColors.h>
#include <ui/wave/WaveFormatUI.h>
#include <ui/widgets/Widgets.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

static constexpr f32 cNameMinEm        = 8.0f;
static constexpr f32 cNameComfortEm    = 20.0f;
static constexpr f32 cFolderCountChars = 4.0f;
static constexpr u32 cPrefixMinLength  = 3;
static constexpr u32 cPrefixGroupMin   = 3;
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

static bool IsUpperLetter(char c)
{
    return c >= 'A' && c <= 'Z';
}

static bool IsLowerLetter(char c)
{
    return c >= 'a' && c <= 'z';
}

static bool IsDigit(char c)
{
    return c >= '0' && c <= '9';
}

static bool IsWordStart(std::string_view name, size_t at)
{
    if (at == 0 || at >= name.size())
        return false;

    const char previous = name[at - 1];
    const char current = name[at];

    if (previous == cPrefixSeparator || current == cPrefixSeparator)
        return previous == cPrefixSeparator && current != cPrefixSeparator;

    if (IsLowerLetter(previous))
        return IsUpperLetter(current) || IsDigit(current);

    if (IsDigit(previous))
        return IsUpperLetter(current);

    if (IsUpperLetter(previous))
        return IsDigit(current) || (IsUpperLetter(current) && at + 1 < name.size() && IsLowerLetter(name[at + 1]));

    return false;
}

static size_t FindWordEnd(std::string_view name, size_t start)
{
    size_t end = start + 1;

    while (end < name.size() && !IsWordStart(name, end))
        end++;

    return std::min(end, name.size());
}

static s32 SharedPrefixLength(std::string_view left, std::string_view right)
{
    const size_t common = std::mismatch(left.begin(), left.begin() + std::min(left.size(), right.size()), right.begin()).first - left.begin();

    for (size_t length = common; length >= cPrefixMinLength; length--)
    {
        if (IsWordStart(left, length) && (length == right.size() || IsWordStart(right, length)))
            return static_cast<s32>(length);
    }

    return 0;
}

static std::string_view GetNameCore(const std::string& stem)
{
    return std::string_view(stem).substr(0, stem.find('.'));
}

static size_t SkipSeparators(std::string_view core, size_t start)
{
    while (start < core.size() && core[start] == cPrefixSeparator)
        start++;

    return start;
}

static std::string_view GetGroupKey(std::string_view core, size_t start)
{
    const size_t wordStart = SkipSeparators(core, start);

    if (wordStart >= core.size())
        return {};

    size_t end = FindWordEnd(core, wordStart);

    while (end > wordStart + 1 && core[end - 1] == cPrefixSeparator)
        end--;

    return core.substr(start, end - start);
}

static bool HasLongerStem(const std::vector<std::string_view>& stems, size_t end)
{
    return std::any_of(stems.begin(), stems.end(), [end](std::string_view stem) { return stem.size() > end; });
}

static bool HasBaseStem(const std::vector<std::string_view>& stems, size_t end)
{
    return std::any_of(stems.begin(), stems.end(), [end](std::string_view stem) { return stem.size() <= end; });
}

static bool IsSharedWord(const std::vector<std::string_view>& stems, size_t start, size_t end)
{
    const std::string_view word = stems.front().substr(start, end - start);

    return std::all_of(stems.begin(), stems.end(), [&](std::string_view stem) { return stem.substr(start, end - start) == word && (stem.size() == end || IsWordStart(stem, end)); });
}

static size_t CalcGroupEnd(const std::vector<std::string_view>& stems, size_t end)
{
    const auto isBaseOrSeparator = [&end](std::string_view stem) { return stem.size() <= end || stem[end] == cPrefixSeparator; };

    while (HasLongerStem(stems, end + 1) && std::all_of(stems.begin(), stems.end(), isBaseOrSeparator))
        end++;

    while (!HasBaseStem(stems, end))
    {
        const size_t wordEnd = FindWordEnd(stems.front(), end);

        if (!HasLongerStem(stems, wordEnd) || !IsSharedWord(stems, end, wordEnd))
            break;

        end = wordEnd;
    }

    return end;
}

static std::vector<s32> FindPrefixGroups(const FileIndexSnapshot& snapshot, const std::vector<s32>& files, size_t start)
{
    std::vector<s32> leaders(files.size(), -1);
    std::vector<std::string_view> keys(files.size());
    std::vector<s32> order(files.size());

    for (s32 i = 0; i < static_cast<s32>(files.size()); i++)
    {
        keys[i] = GetGroupKey(GetNameCore(snapshot.files[files[i]].stem), start);
        order[i] = i;
    }

    std::stable_sort(order.begin(), order.end(), [&keys](s32 left, s32 right) { return keys[left] < keys[right]; });

    for (size_t first = 0; first < order.size();)
    {
        const std::string_view key = keys[order[first]];
        bool hasLongerStem = false;
        size_t end = first;

        while (end < order.size() && keys[order[end]] == key)
            hasLongerStem |= GetNameCore(snapshot.files[files[order[end++]]].stem).size() > start + key.size();

        if (!key.empty() && hasLongerStem && end - first >= cPrefixGroupMin)
        {
            for (size_t member = first; member < end; member++)
                leaders[order[member]] = order[first];
        }

        first = end;
    }

    return leaders;
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
    const std::string_view text = name;

    for (s32 cut = 1; cut <= prefixLength; cut++)
    {
        if (IsWordStart(text, cut) && markerWidth + ImGui::CalcTextSize(name + cut).x <= width)
            return cut;
    }

    return prefixLength;
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
    markSharedPrefixes(rows, firstFile);
}

void FilesPanel::markSharedPrefixes(std::vector<FilesRow>& rows, size_t firstFile) const
{
    for (size_t i = firstFile; i < rows.size(); i++)
    {
        FilesRow& row = rows[i];

        if (row.kind != FilesRowKind::File)
            continue;

        const FileIndexSnapshot& snapshot = getSnapshot(row.root);
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

    collectPrefixRows(rows, root, folder, depth, isAllOpen, files, 0, 0);
}

void FilesPanel::collectPrefixRows(std::vector<FilesRow>& rows, s32 root, s32 folder, s32 depth, bool isAllOpen, const std::vector<s32>& files, s32 groupStart, s32 start)
{
    const FileIndexSnapshot& snapshot = getSnapshot(root);
    const std::vector<s32> leaders = mGroupByPrefix ? FindPrefixGroups(snapshot, files, start) : std::vector<s32>(files.size(), -1);

    for (s32 i = 0; i < static_cast<s32>(files.size()); i++)
    {
        const std::string_view core = GetNameCore(snapshot.files[files[i]].stem);
        const s32 wordStart = static_cast<s32>(SkipSeparators(core, start));

        if (leaders[i] < 0)
        {
            const bool isBase = wordStart >= static_cast<s32>(core.size());
            rows.push_back({ .kind = FilesRowKind::File, .root = root, .index = files[i], .depth = depth, .nameStart = isBase ? groupStart : wordStart });
            continue;
        }

        if (leaders[i] != i)
            continue;

        const BrowserFile& first = snapshot.files[files[i]];
        std::vector<s32> members;
        std::vector<std::string_view> cores;
        FilesRow group = { .kind = FilesRowKind::Prefix, .root = root, .index = folder, .depth = depth, .groupStart = wordStart };

        for (s32 member = i; member < static_cast<s32>(files.size()); member++)
        {
            if (leaders[member] != i)
                continue;

            const BrowserFile& file = snapshot.files[files[member]];

            members.push_back(files[member]);
            cores.push_back(GetNameCore(file.stem));
            group.groupSize += file.size;
            group.isMixedFormat |= strcmp(file.probe.formatName, first.probe.formatName) != 0;
        }

        group.nameStart = static_cast<s32>(CalcGroupEnd(cores, start + GetGroupKey(core, start).size()));
        group.childCount = static_cast<u32>(members.size());
        group.sound = members[std::find_if(cores.begin(), cores.end(), [&group](std::string_view stem) { return static_cast<s32>(stem.size()) > group.nameStart; }) - cores.begin()];
        rows.push_back(group);

        if (isAllOpen || isRowOpen(group))
            collectPrefixRows(rows, root, folder, depth + 1, isAllOpen, members, group.groupStart, group.nameStart);
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

    for (const FilesRow& row : ctx.rows)
    {
        if (row.isSame(mSelected))
        {
            mSelected = row;
            break;
        }
    }

    std::vector<FilesRow> allOpenRows;
    collectAllOpenRows(ctx, allOpenRows);

    PushItemTablePadding();
    calcValueWidths(ctx, allOpenRows);
    ctx.nameWant = calcNameWant(ctx, allOpenRows);
    ImGui::PopStyleVar();

    if (!mSelectedPaths.empty())
        onSelectionChanged();
}

void FilesPanel::collectAllOpenRows(const FrameContext& ctx, std::vector<FilesRow>& rows)
{
    if (ctx.isSearching)
    {
        FrameContext search;

        for (s32 root = 0; root < static_cast<s32>(mRoots.size()); root++)
            collectSearchRows(search, root, true);

        rows = std::move(search.rows);
        return;
    }

    for (s32 root = 0; root < static_cast<s32>(mRoots.size()); root++)
    {
        const FilesRoot& entry = mRoots[root];
        const s32 folder = getRootFolder(root);
        const char* format = nullptr;
        u64 size = 0;

        if (getRootValues(root, &format, &size))
            rows.push_back({ .kind = FilesRowKind::Root, .root = root });

        if (folder >= 0 && entry.isOpen && !entry.isMissing && entry.hasSnapshot && folder < static_cast<s32>(ctx.shownCounts[root].size()) && ctx.shownCounts[root][folder] > 0)
            collectFolderRows(ctx, rows, root, folder, 1, true);
    }
}

void FilesPanel::collectRows(FrameContext& ctx)
{
    resolveSearchScope();

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
        mSearchToggled.clear();

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
    }

    countScopeFiles(ctx);

    for (s32 section = -1; section < static_cast<s32>(mSections.size()); section++)
        collectSectionRows(ctx, section);

    if (mRoots.empty() && mSections.empty())
        ctx.rows.push_back({ .kind = FilesRowKind::AddFolder });
}

void FilesPanel::collectSectionRows(FrameContext& ctx, s32 section)
{
    const u32 id = section < 0 ? 0 : mSections[section].id;
    const s32 end = getSectionEnd(id);
    const size_t headRow = ctx.rows.size();

    if (section >= 0)
        ctx.rows.push_back({ .kind = FilesRowKind::Section, .index = section, .childCount = static_cast<u32>(end - getSectionStart(id)) });

    for (s32 root = getSectionStart(id); root < end; root++)
    {
        if (ctx.isSearching)
            collectSearchRows(ctx, root, false);
        else if (section < 0 || !mSections[section].isFolded)
            collectRootRows(ctx, root);
        else
            ctx.shownFileCount += mRoots[root].isNested || ctx.shownCounts[root].empty() ? 0 : ctx.shownCounts[root][0];
    }

    if (ctx.isSearching && section >= 0 && ctx.rows.size() == headRow + 1)
        ctx.rows.pop_back();
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

const std::string& FilesPanel::getRowName(const FilesRow& row) const
{
    if (row.kind == FilesRowKind::Root)
        return mRoots[row.root].name;

    if (row.kind == FilesRowKind::Section)
        return mSections[row.index].name;

    if (row.kind == FilesRowKind::Folder)
        return getSnapshot(row.root).folders[row.index].name;

    const BrowserFile& file = getFile(row);

    return row.kind == FilesRowKind::Sound ? file.probe.contents.sounds[row.sound].name : getShownFileName(file);
}

const std::string& FilesPanel::getShownFileName(const BrowserFile& file) const
{
    return mShowExtensions ? file.name : file.stem;
}

f32 FilesPanel::calcRowNoteWidth(const FrameContext& ctx, const FilesRow& row) const
{
    const f32 gap = ImFloor(ctx.em * cCountGapEm);

    if (row.kind == FilesRowKind::File && findOpenFile(ctx, getFile(row).path))
        return gap + ImGui::CalcTextSize(cOpenFileMark).x;

    if (mShowCount && (row.kind == FilesRowKind::Folder || row.kind == FilesRowKind::Prefix || (row.kind == FilesRowKind::File && row.childCount > 0)))
        return gap + ImGui::CalcTextSize(cWidthSampleChar).x * cFolderCountChars;

    return 0.0f;
}

bool FilesPanel::isSpanningRow(const FilesRow& row) const
{
    return row.kind == FilesRowKind::Folder && !getPackFolder(row);
}

f32 FilesPanel::calcNameWant(const FrameContext& ctx, const std::vector<FilesRow>& rows) const
{
    const f32 charWidth = ImGui::CalcTextSize(cWidthSampleChar).x;
    const f32 indent = ImFloor(ctx.em * cIndentEm);
    const f32 lead = ctx.chevronWidth + ctx.em + ImFloor(ctx.em * cIconGapEm) + ImGui::GetStyle().CellPadding.x * 2.0f;
    f32 widest = 0.0f;
    const char* format = nullptr;
    u64 size = 0;

    for (const FilesRow& row : rows)
    {
        const bool isValueRoot = row.kind == FilesRowKind::Root && getRootValues(row.root, &format, &size);

        if ((!IsContentRow(row) && !isValueRoot) || isSpanningRow(row))
            continue;

        const size_t nameLength = row.kind == FilesRowKind::Prefix ? row.nameStart - row.groupStart : getRowName(row).size() - row.nameStart;
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

        if (row.kind == FilesRowKind::Root)
        {
            if (!getRootValues(row.root, &format, &rowSize))
                continue;

            FormatItemSize(&size, rowSize);
            sizeLength = size.calcLength();
        }
        else if (pack)
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

    ctx.showSize = mShowSize && ctx.sizeWidth > 0.0f && avail >= nameWant + sizeWidth;
    ctx.showFormat = mShowFormat && ctx.formatWidth > 0.0f && (ctx.showSize || !mShowSize) && avail >= nameWant + (ctx.showSize ? sizeWidth : 0.0f) + formatWidth;
}

f32 FilesPanel::drawRowLabel(const ImVec2& pos, f32 right, const char* icon, ImU32 iconColor, const char* name, ImU32 nameColor, const char* note, ImU32 noteColor, s32 prefixLength, const char* count, bool isCountShown)
{
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const f32 em = ImGui::GetFontSize();
    const f32 iconWidth = ImGui::CalcTextSize(icon).x;
    const f32 nameX = pos.x + iconWidth + ImFloor(em * cIconGapEm);
    const f32 noteGap = ImFloor(em * cCountGapEm);

    drawList->AddText(pos, iconColor, icon);

    if (count)
    {
        const f32 countWidth = ImGui::CalcTextSize(count).x;
        const f32 slotWidth = sead::Mathf::max(countWidth, ImGui::CalcTextSize(cWidthSampleChar).x * cFolderCountChars);

        if (isCountShown)
            drawList->AddText(ImVec2(right - countWidth, pos.y), ImGui::GetColorU32(GetItemValueColor()), count);

        right -= slotWidth + noteGap;
    }

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
            select(row, false);
            frame.isPressed = true;
        }
    }

    drawRowDragSource(row);

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
    const sead::FixedSafeString<32> count = FormatCountText(ctx.isSearching ? row.childCount : ctx.shownCounts[row.root][row.index]);
    const bool isSpanning = isSpanningRow(row);
    const f32 right = isSpanning ? CalcTableValueRight() : frame.right;

    if (isSpanning)
        PushTableRowClipRect(frame.rowMin, frame.rowMax);

    ImGui::GetWindowDrawList()->AddText(frame.textPos, ImGui::GetColorU32(ImGuiCol_TextDisabled), chevron);
    drawRowLabel(ImVec2(frame.textPos.x + ctx.chevronWidth, frame.textPos.y), right, icon, ImGui::GetColorU32(iconColor), folder.name.c_str(), ImGui::GetColorU32(ImGuiCol_Text), nullptr, 0, 0, mShowCount ? count.cstr() : nullptr);

    if (isSpanning)
    {
        ImGui::PopClipRect();
    }
    else
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

    const std::string name = first.stem.substr(row.groupStart, row.nameStart - row.groupStart);
    const char* chevron = isExpanded ? ICON_LC_CHEVRON_DOWN : ICON_LC_CHEVRON_RIGHT;
    const ImU32 dimColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const sead::FixedSafeString<32> count = FormatCountText(row.childCount);
    sead::FixedSafeString<32> size;

    FormatItemSize(&size, row.groupSize);

    ImGui::GetWindowDrawList()->AddText(frame.textPos, dimColor, chevron);
    drawRowLabel(ImVec2(frame.textPos.x + ctx.chevronWidth, frame.textPos.y), frame.right, ICON_LC_FILE_STACK, ImGui::GetColorU32(GetFileKindColor(first.probe.kind)), name.c_str(), ImGui::GetColorU32(ImGuiCol_Text), nullptr, 0, 0, mShowCount ? count.cstr() : nullptr);
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
        pathtext::DrawTooltipFolder(file.path, ImGui::CalcTextSize(file.name.c_str()).x);

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

    if (isOpenable(file.probe) && ContextMenuItem(ICON_LC_FILE_PEN, messages::common::cOpen))
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
        openSelectedFiles();

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
        drawBrowserItem(file.path, mRoots[row.root].section);
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
        DrawItemValueCell(format, ctx.showSize ? cItemColumnFlag_Text : cItemColumnFlag_None);

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

    const sead::FixedSafeString<32> count = FormatCountText(row.childCount);
    const char* note = openMark ? cOpenFileMark : nullptr;
    const ImU32 noteColor = openMark ? getOpenMarkColor(openMark) : 0;

    drawRowLabel(ImVec2(frame.textPos.x + ctx.chevronWidth, frame.textPos.y), frame.right, icon, iconColor, getShownFileName(file).c_str() + row.nameStart, getOpenNameColor(openMark), note, noteColor, row.prefixLength, mShowCount && ctx.isSearching && row.childCount > 0 ? count.cstr() : nullptr);

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
