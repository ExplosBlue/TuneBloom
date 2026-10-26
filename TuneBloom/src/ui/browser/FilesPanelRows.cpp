#include <ui/browser/FilesPanelInternal.h>

#include <ui/widgets/ContextMenu.h>
#include <ui/browser/FilesPanel.h>
#include <ui/items/ItemColumns.h>
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
#include <string>
#include <vector>

static constexpr f32 cIndentEm         = 1.1f;
static constexpr f32 cCountGapEm       = 0.5f;
static constexpr f32 cNameMinEm        = 8.0f;
static constexpr f32 cNameComfortEm    = 20.0f;
static constexpr f32 cFolderCountChars = 4.0f;
static constexpr u32 cPrefixMinLength  = 3;
static constexpr char cPrefixSeparator = '_';

static constexpr const char* cPrefixMarker    = "..";
static constexpr const char* cWidthSampleChar = "0";
static constexpr const char* cOpenFileMark    = ICON_LC_DOT;

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

static s32 SharedPrefixLength(const std::string& left, const std::string& right)
{
    const size_t common = std::mismatch(left.begin(), left.begin() + std::min(left.size(), right.size()), right.begin()).first - left.begin();
    const size_t separator = left.rfind(cPrefixSeparator, common == 0 ? 0 : common - 1);

    if (common == 0 || separator == std::string::npos || separator + 1 < cPrefixMinLength)
        return 0;

    return static_cast<s32>(separator + 1);
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

void FilesPanel::countShownFiles(FrameContext& ctx, s32 folder)
{
    const BrowserFolder& entry = ctx.snapshot->folders[folder];
    u32 count = 0;

    for (s32 child : entry.folders)
    {
        countShownFiles(ctx, child);
        count += ctx.shownCounts[child];
    }

    for (s32 file : entry.files)
    {
        if (isFileShown(ctx.snapshot->files[file]))
            count++;
    }

    ctx.shownCounts[folder] = count;
}

void FilesPanel::collectFolderRows(FrameContext& ctx, s32 folder, s32 depth)
{
    const BrowserFolder& entry = ctx.snapshot->folders[folder];

    for (s32 child : entry.folders)
    {
        if (ctx.shownCounts[child] == 0)
            continue;

        ctx.rows.push_back({ FilesRowKind::Folder, child, -1, depth });

        if (mExpanded[child])
            collectFolderRows(ctx, child, depth + 1);
    }

    const size_t firstFile = ctx.rows.size();

    for (s32 file : entry.files)
    {
        if (isFileShown(ctx.snapshot->files[file]))
            ctx.rows.push_back({ FilesRowKind::File, file, -1, depth });
    }

    for (size_t i = firstFile; i < ctx.rows.size(); i++)
    {
        const std::string& stem = ctx.snapshot->files[ctx.rows[i].index].stem;
        const s32 previous = i > firstFile ? SharedPrefixLength(stem, ctx.snapshot->files[ctx.rows[i - 1].index].stem) : 0;
        const s32 next = i + 1 < ctx.rows.size() ? SharedPrefixLength(stem, ctx.snapshot->files[ctx.rows[i + 1].index].stem) : 0;

        ctx.rows[i].prefixLength = std::max(previous, next);
    }
}

void FilesPanel::collectRows(FrameContext& ctx)
{
    ctx.shownCounts.assign(ctx.snapshot->folders.size(), 0);

    if (ctx.snapshot->folders.empty())
        return;

    countShownFiles(ctx, 0);
    ctx.isSearching = mSearch[0] != '\0';

    if (!ctx.isSearching)
    {
        ctx.shownFileCount = ctx.shownCounts[0];
        collectFolderRows(ctx, 0, 0);
        return;
    }

    collectSearchRows(ctx);
}

void FilesPanel::collectSearchRows(FrameContext& ctx)
{
    updateSoundMatches(ctx);

    for (s32 file = 0; file < static_cast<s32>(ctx.snapshot->files.size()); file++)
    {
        const BrowserFile& entry = ctx.snapshot->files[file];

        if (!isFileShown(entry) || mSoundMatches.hasMatch[file] || !ContainsIgnoreCase(entry.name, mSearch))
            continue;

        ctx.rows.push_back({ FilesRowKind::File, file, -1, 0 });
        ctx.shownFileCount++;
    }

    s32 archive = -1;

    for (const FilesRow& row : mSoundMatches.rows)
    {
        const BrowserFile& entry = ctx.snapshot->files[row.index];

        if (!isFileShown(entry))
            continue;

        if (row.index != archive)
        {
            archive = row.index;
            ctx.rows.push_back({ FilesRowKind::File, archive, -1, 0 });
            ctx.shownFileCount += ContainsIgnoreCase(entry.name, mSearch) ? 1 : 0;
        }

        ctx.rows.push_back(row);
        ctx.shownSoundCount++;
    }
}

void FilesPanel::updateSoundMatches(const FrameContext& ctx)
{
    if (mSoundMatches.generation == mSnapshotGeneration && mSoundMatches.query == mSearch)
        return;

    mSoundMatches.query = mSearch;
    mSoundMatches.generation = mSnapshotGeneration;
    mSoundMatches.rows.clear();
    mSoundMatches.hasMatch.assign(ctx.snapshot->files.size(), 0);

    for (s32 file = 0; file < static_cast<s32>(ctx.snapshot->files.size()); file++)
    {
        const std::vector<ArchiveSound>& sounds = ctx.snapshot->files[file].probe.contents.sounds;

        for (s32 sound = 0; sound < static_cast<s32>(sounds.size()); sound++)
        {
            if (sounds[sound].name.empty() || !ContainsIgnoreCase(sounds[sound].name, mSearch))
                continue;

            mSoundMatches.rows.push_back({ FilesRowKind::Sound, file, sound, 1 });
            mSoundMatches.hasMatch[file] = 1;
        }
    }
}

const std::string& FilesPanel::getRowName(const FrameContext& ctx, const FilesRow& row) const
{
    if (row.kind == FilesRowKind::Folder)
        return ctx.snapshot->folders[row.index].name;

    const BrowserFile& file = ctx.snapshot->files[row.index];

    return row.kind == FilesRowKind::Sound ? file.probe.contents.sounds[row.sound].name : file.stem;
}

f32 FilesPanel::calcRowNoteWidth(const FrameContext& ctx, const FilesRow& row) const
{
    const f32 gap = ImFloor(ctx.em * cCountGapEm);

    if (row.kind == FilesRowKind::Folder)
        return gap + ImGui::CalcTextSize(cWidthSampleChar).x * cFolderCountChars;

    if (row.kind == FilesRowKind::File && findOpenFile(ctx, ctx.snapshot->files[row.index].path))
        return gap + ImGui::CalcTextSize(cOpenFileMark).x;

    return 0.0f;
}

f32 FilesPanel::calcNameWant(const FrameContext& ctx) const
{
    const f32 charWidth = ImGui::CalcTextSize(cWidthSampleChar).x;
    const f32 indent = ImFloor(ctx.em * cIndentEm);
    const f32 lead = ctx.chevronWidth + ctx.em + ImFloor(ctx.em * cIconGapEm) + ImGui::GetStyle().CellPadding.x * 2.0f;
    f32 widest = 0.0f;

    for (const FilesRow& row : ctx.rows)
    {
        const f32 width = row.depth * indent + lead + getRowName(ctx, row).size() * charWidth + calcRowNoteWidth(ctx, row);

        widest = sead::Mathf::max(widest, width);
    }

    return std::clamp(widest, ctx.em * cNameMinEm, ctx.em * cNameComfortEm);
}

void FilesPanel::updateSizeLabelLengths()
{
    const std::vector<BrowserFile>& files = mIndex.getSnapshot().files;

    mSizeLabelLengths.resize(files.size());

    for (size_t i = 0; i < files.size(); i++)
    {
        sead::FixedSafeString<32> size;
        FormatItemSize(&size, files[i].size);

        mSizeLabelLengths[i] = static_cast<u8>(size.calcLength());
    }
}

void FilesPanel::calcValueWidths(FrameContext& ctx) const
{
    const char* widestFormat = nullptr;
    size_t widestFormatLength = 0;
    s32 widestSizeFile = -1;
    u8 widestSizeLength = 0;

    for (const FilesRow& row : ctx.rows)
    {
        if (row.kind == FilesRowKind::Folder)
            continue;

        const BrowserFile& file = ctx.snapshot->files[row.index];
        const char* format = row.kind == FilesRowKind::Sound ? GetSoundType(file.probe.contents.sounds[row.sound].type).shortLabel : file.probe.formatName;
        const size_t formatLength = strlen(format);

        if (formatLength > widestFormatLength)
        {
            widestFormat = format;
            widestFormatLength = formatLength;
        }

        if (row.kind == FilesRowKind::File && mSizeLabelLengths[row.index] > widestSizeLength)
        {
            widestSizeFile = row.index;
            widestSizeLength = mSizeLabelLengths[row.index];
        }
    }

    ctx.formatWidth = widestFormat ? CalcItemValueCellWidth(widestFormat) : 0.0f;

    if (widestSizeFile < 0)
        return;

    sead::FixedSafeString<32> size;
    FormatItemSize(&size, ctx.snapshot->files[widestSizeFile].size);

    ctx.sizeWidth = CalcItemValueCellWidth(size.cstr());
}

void FilesPanel::setupColumns(FrameContext& ctx)
{
    calcValueWidths(ctx);

    const f32 cellPadding = ImGui::GetStyle().CellPadding.x * 2.0f;
    const f32 avail = ImGui::GetContentRegionAvail().x;
    const f32 nameWant = calcNameWant(ctx);
    const f32 formatWidth = ctx.formatWidth + cellPadding;
    const f32 sizeWidth = ctx.sizeWidth + cellPadding;

    ctx.showSize = ctx.sizeWidth > 0.0f && avail >= nameWant + sizeWidth;
    ctx.showFormat = ctx.showSize && ctx.formatWidth > 0.0f && avail >= nameWant + sizeWidth + formatWidth;
}

void FilesPanel::drawRowLabel(const ImVec2& pos, f32 right, const char* icon, ImU32 iconColor, const char* name, ImU32 nameColor, const char* note, ImU32 noteColor, s32 prefixLength)
{
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const f32 em = ImGui::GetFontSize();
    const f32 iconWidth = ImGui::CalcTextSize(icon).x;
    const f32 nameX = pos.x + iconWidth + ImFloor(em * cIconGapEm);

    drawList->AddText(pos, iconColor, icon);

    const f32 noteGap = ImFloor(em * cCountGapEm);
    const f32 noteWidth = note ? noteGap + ImGui::CalcTextSize(note).x : 0.0f;
    const f32 nameEnd = drawRowName(drawList, ImVec2(nameX, pos.y), sead::Mathf::max(right - noteWidth, nameX), name, nameColor, prefixLength);

    if (note)
        drawList->AddText(ImVec2(nameEnd + noteGap, pos.y), noteColor, note);
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

    bool hovered = false;

    if (DrawTableRowSelectable("##row", mSelected.isSame(row), &hovered))
    {
        select(ctx, row);
        frame.isPressed = true;
    }

    frame.isDoubleClicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

    return frame;
}

void FilesPanel::drawFolderRow(FrameContext& ctx, const FilesRow& row)
{
    const BrowserFolder& folder = ctx.snapshot->folders[row.index];
    const bool isExpanded = mExpanded[row.index] != 0;
    const RowFrame frame = beginRow(ctx, row, 0.0f);

    if (frame.isPressed)
        mExpanded[row.index] = isExpanded ? 0 : 1;

    const char* chevron = isExpanded ? ICON_LC_CHEVRON_DOWN : ICON_LC_CHEVRON_RIGHT;
    const char* icon = isExpanded ? ICON_LC_FOLDER_OPEN : ICON_LC_FOLDER_CLOSED;
    sead::FormatFixedSafeString<32> count("%u", ctx.shownCounts[row.index]);

    ImGui::GetWindowDrawList()->AddText(frame.textPos, ImGui::GetColorU32(ImGuiCol_TextDisabled), chevron);
    drawRowLabel(ImVec2(frame.textPos.x + ctx.chevronWidth, frame.textPos.y), frame.right, icon, ImGui::GetColorU32(GetItemValueColor()), folder.name.c_str(), ImGui::GetColorU32(ImGuiCol_Text), count.cstr(), ImGui::GetColorU32(ImGuiCol_TextDisabled));
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
        countWidth = sead::Mathf::max(countWidth, ImGui::CalcTextSize(sead::FormatFixedSafeString<16>("%u", row.count).cstr()).x);

    if (!ImGui::BeginTable("##contents", 2, ImGuiTableFlags_SizingFixedFit))
        return;

    ImGui::TableSetupColumn("##label");
    ImGui::TableSetupColumn("##count", ImGuiTableColumnFlags_WidthFixed, countWidth);

    const f32 indent = ImFloor(ImGui::GetFontSize() * cIndentEm);

    for (const CountRow& row : rows)
    {
        sead::FormatFixedSafeString<16> count("%u", row.count);

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

void FilesPanel::drawFileTooltip(const BrowserFile& file)
{
    if (!ImGui::BeginItemTooltip())
        return;

    const FileProbe& probe = file.probe;

    ImGui::TextUnformatted(file.name.c_str());
    ImGui::TextDisabled(messages::files::cFormatVersionFormat, probe.formatName, probe.versionText);

    if (probe.isCompressed)
        ImGui::TextDisabled("%s", messages::files::cCompressed);

    if (probe.hasItemCount)
        ImGui::TextDisabled(probe.itemCount == 1 ? messages::files::cAssetCountOne : messages::files::cAssetCountFormat, probe.itemCount);

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
        ImGui::TextDisabled("%s", messages::files::cLooping);

    if (probe.kind == BrowserFileKind::SoundArchive)
        drawArchiveContents(probe.contents);

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

void FilesPanel::drawFileMenu(const BrowserFile& file)
{
    if (!BeginContextMenu("##menu"))
        return;

    if (ContextMenuItem(ICON_LC_FOLDER_OPEN, messages::files::cOpen))
        openFile(file);

    if (isPreviewable(file.probe) && ContextMenuItem(ICON_LC_PLAY, messages::common::cPlay))
        mPreview.play(file.path, file.stem);

    if (ContextMenuItem(ICON_LC_FOLDER_SEARCH, messages::recent::cShowInFileManager))
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
    const BrowserFile& file = ctx.snapshot->files[row.index];
    const OpenFileMark* openMark = findOpenFile(ctx, file.path);
    const RowFrame frame = beginRow(ctx, row, ctx.isSearching ? 0.0f : ctx.chevronWidth);

    if (frame.isDoubleClicked)
        openFile(file);
    else if (frame.isPressed)
        showFile(file);

    ImGui::OpenPopupOnItemClick("##menu", ImGuiPopupFlags_MouseButtonRight);

    if (IsItemHoveredForDetail())
        drawFileTooltip(file);

    const bool isPlaying = mPreview.isPlaying(file.path);
    const char* icon = isPlaying ? ICON_LC_VOLUME_2 : GetFileKindIcon(file.probe.kind);
    const ImU32 iconColor = ImGui::GetColorU32(isPlaying ? uicolors::Playback() : GetFileKindColor(file.probe.kind));
    const bool isActive = openMark && openMark->isActive;
    const ImU32 nameColor = ImGui::GetColorU32(isActive ? GetAccentTextColor() : ImGui::GetStyleColorVec4(ImGuiCol_Text));
    const ImU32 markColor = ImGui::GetColorU32(openMark && openMark->isUnsaved ? uicolors::Warning() : GetAccentTextColor());

    drawRowLabel(frame.textPos, frame.right, icon, iconColor, file.stem.c_str(), nameColor, openMark ? cOpenFileMark : nullptr, markColor, row.prefixLength);

    sead::FixedSafeString<32> size;
    FormatItemSize(&size, file.size);

    drawValueCells(ctx, file.probe.formatName, size.cstr());
    drawFileMenu(file);
}

void FilesPanel::drawSoundRow(FrameContext& ctx, const FilesRow& row)
{
    const BrowserFile& file = ctx.snapshot->files[row.index];
    const ArchiveSound& sound = file.probe.contents.sounds[row.sound];
    const SoundTypeInfo& type = GetSoundType(sound.type);
    const RowFrame frame = beginRow(ctx, row, 0.0f);

    if (frame.isDoubleClicked)
        openSound(ctx, file, sound);

    if (IsItemHoveredForDetail())
        drawSoundTooltip(file, sound);

    const ImU32 iconColor = ImGui::GetColorU32(GetSeriesColor(static_cast<s32>(KindFilter::Archives)));

    drawRowLabel(frame.textPos, frame.right, type.icon, iconColor, sound.name.c_str(), ImGui::GetColorU32(ImGuiCol_Text), nullptr, 0);
    drawValueCells(ctx, type.shortLabel, "");
}
