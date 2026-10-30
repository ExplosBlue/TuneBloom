#pragma once

#include <scan/FileIndex.h>
#include <ui/browser/FilePreview.h>
#include <ui/browser/FileExportRun.h>
#include <ui/file/FileDrag.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

inline constexpr f32 cIconGapEm  = 0.4f;
inline constexpr f32 cIndentEm   = 1.1f;
inline constexpr f32 cCountGapEm = 0.5f;

inline constexpr const char* cFolderMenuId       = "##foldermenu";
inline constexpr const char* cSectionPayloadType = "TB_BROWSER_SECTION";
inline constexpr const char* cOpenFileMark = ICON_LC_DOT;
inline constexpr u32 cRemovedUndoMax = 8;

void UndoFilesPanelRemove(u32 token);

template <typename T>
inline void PushRemovedEntry(std::vector<T>* entries, T&& entry)
{
    if (entries->size() >= cRemovedUndoMax)
        entries->erase(entries->begin());

    entries->push_back(std::move(entry));
}

enum class FilesColumn
{
    Name,
    Format,
    Size,

    Count
};

inline constexpr s32 cFilesColumnCount = static_cast<s32>(FilesColumn::Count);

enum class KindFilter
{
    Archives,
    Resources,
    Waves,
    Streams,
    Wav,

    Count
};

inline constexpr s32 cKindFilterCount = static_cast<s32>(KindFilter::Count);

enum class FilesRowKind : s32
{
    Root,
    Folder,
    Prefix,
    File,
    Sound,
    Status,
    Section,
    AddFolder
};

struct FilesRowKey
{
    s32 kind;
    s32 root;
    s32 index;
    s32 sound;
};

struct FilesRow
{
    FilesRowKind kind = FilesRowKind::File;
    s32 root = -1;
    s32 index = -1;
    s32 sound = -1;
    s32 depth = 0;
    s32 prefixLength = 0;
    s32 nameStart = 0;
    u32 childCount = 0;
    u64 groupSize = 0;
    bool isMixedFormat = false;
    bool isClosedAtFirst = false;

    bool isSame(const FilesRow& other) const
    {
        return kind == other.kind && root == other.root && index == other.index && sound == other.sound;
    }

    FilesRowKey key() const
    {
        return { static_cast<s32>(kind), root, index, sound };
    }
};

struct FilesRoot
{
    std::string path;
    std::string name;
    std::string note;
    std::unique_ptr<FileIndex> index = std::make_unique<FileIndex>();
    std::vector<u8> expanded;
    std::optional<std::vector<std::string>> savedOpenFolders;
    std::vector<std::string> openPrefixes;
    std::vector<u8> sizeLabelLengths;
    u32 scanRequest = 0;
    u32 section = 0;
    bool isFile = false;
    bool isNested = false;
    bool isOpen = true;
    bool isMissing = false;
    bool hasSnapshot = false;
    bool needsScan = false;
    bool isFullScan = false;
};

struct FilesSection
{
    u32 id = 0;
    std::string name;
    bool isFolded = false;
};

struct OpenFileMark
{
    std::string path;
    bool isActive = false;
    bool isUnsaved = false;
};

struct FilesDropRow
{
    s32 root = -1;
    s32 section = -1;
    f32 top = 0.0f;
    f32 split = 0.0f;
};

struct FilesDropFrame
{
    std::vector<FilesDropRow> rows;
    f32 endY = -1.0f;
    f32 lineY = -1.0f;
    f32 headTop = -1.0f;
    s32 slot = -1;
    u32 section = 0;
    s32 sectionSlot = -1;
    u32 draggedSection = 0;
    bool isHovered = false;
};

class FilesPanel
{
public:
    void loadConfig();
    s32 addPath(const std::string& path, u32 section = 0, s32 slot = -1);
    void addPaths(const std::vector<std::string>& paths, s32 slot = -1, u32 section = 0);
    void requestUndo(u32 token);
    bool chooseFolder(u32 section = 0);
    void draw();

    static KindFilter getKindFilter(BrowserFileKind kind);

private:
    enum class RootAction
    {
        None,
        MoveUp,
        MoveDown,
        MoveToSection,
        MoveToNewSection,
        Rescan,
        Remove,
        Open,
        ChooseFolder,
        ChooseFiles,
        NewSection,
        RemoveSection,
        SearchInFolder
    };

    struct PendingAction
    {
        RootAction action = RootAction::None;
        std::string rootPath;
        std::string path;
        u32 section = 0;
    };

    struct FrameContext
    {
        std::vector<std::vector<u32>> shownCounts;
        std::vector<FilesRow> rows;
        u32 shownFileCount = 0;
        u32 shownSoundCount = 0;
        u32 totalFileCount = 0;
        u64 totalSize = 0;
        bool isSearching = false;
        bool showFormat = true;
        bool showSize = true;
        f32 formatWidth = 0.0f;
        f32 sizeWidth = 0.0f;
        f32 nameWant = 0.0f;
        f32 tableWidth = 0.0f;
        std::string openPath;
        std::vector<OpenFileMark> openFiles;
        FilesDropFrame drop;
        f32 em = 0.0f;
        f32 chevronWidth = 0.0f;
    };

    struct PendingDrop
    {
        std::vector<std::string> paths;
        s32 slot = -1;
        u32 section = 0;
    };

    struct RowFrame
    {
        ImVec2 textPos;
        ImVec2 rowMin;
        ImVec2 rowMax;
        f32 right = 0.0f;
        bool isPressed = false;
        bool isDoubleClicked = false;
    };

    struct SearchScope
    {
        std::string rootPath;
        std::string folderKey;
        std::string name;
        std::string path;
        s32 root = -1;
        s32 folder = -1;
    };

    struct SearchCounts
    {
        std::vector<u32> listed;
        std::vector<u32> named;
        std::vector<u32> sounds;
        std::vector<u8> isInherited;
    };

    struct RowsKey
    {
        u32 rowsGeneration = 0;
        u32 snapshotGeneration = 0;
        f32 em = 0.0f;
        std::string search;
        std::vector<std::string> openPaths;
    };

    s32 findRoot(const std::string& path) const;
    s32 addRoot(const std::string& path, bool isFile, u32 section, s32 slot);
    void removeRoot(s32 root);
    void removeRootWithUndo(s32 root);
    void applyUndo();
    void addLegacyPins();
    s32 insertRoot(FilesRoot root, s32 slot = -1);
    void onRootsChanged();
    void selectRoot(s32 root);
    bool chooseFiles(u32 section);
    void requestScan(s32 root, bool isFullScan = false);
    void updateScans();
    void startScan(s32 root);
    void restoreExpanded(FilesRoot& root, std::vector<std::string> openFolders);
    void updateSizeLabelLengths(FilesRoot& root);
    std::string getFolderPath(s32 root, s32 folder) const;
    std::string getFolderKey(s32 root, s32 folder) const;
    s32 findFolder(s32 root, const std::string& key) const;
    std::string makePrefixKey(const FilesRow& row) const;
    void requestAction(RootAction action, s32 root, const std::string& path = std::string(), u32 section = 0);
    void applyPendingAction();
    void moveRoot(s32 root, s32 offset);
    s32 placeRoot(s32 root, s32 slot, u32 section);
    bool isRootPath(const std::string& path) const;
    void saveConfig() const;

    s32 findSection(u32 id) const;
    s32 getSectionRank(u32 id) const;
    s32 getSectionStart(u32 id) const;
    s32 getSectionEnd(u32 id) const;
    u32 getPreviousSection(u32 id) const;
    u32 getRowSection(const FilesRow& row) const;
    u32 getTargetSection() const;
    u32 addSection(s32 at = -1);
    void removeSection(u32 id);
    void moveSection(u32 id, s32 slot);
    void setSectionFolded(u32 id, bool isFolded);
    void foldAllSections(bool isFolded);
    void sortRoots();
    std::string makeSectionName() const;
    void collectSectionRows(FrameContext& ctx, s32 section);
    void drawSectionRow(FrameContext& ctx, const FilesRow& row);
    void drawSectionLabel(const FrameContext& ctx, const FilesSection& section, const RowFrame& frame, bool isRowHovered);
    void drawSectionMenu(const FilesSection& section);
    void drawSectionDragSource(const FilesSection& section);
    void drawAddMenu(const char* id, u32 section, bool showsTarget);
    void drawAreaMenu();
    void drawAddMenuItems(u32 section, bool showsTarget, bool isSubMenu);
    void drawAddButton();
    void drawMoveToSectionMenu(s32 root);

    const FileIndexSnapshot& getSnapshot(s32 root) const;
    const BrowserFile& getFile(const FilesRow& row) const;
    const BrowserFolder* getPackFolder(const FilesRow& row) const;
    s32 getRootFolder(s32 root) const;
    const BrowserFile* getRootFile(s32 root) const;
    bool getRootValues(s32 root, const char** format, u64* size) const;
    const BrowserFile* getRowFile(const FilesRow& row) const;
    bool isExpandable(const FilesRow& row) const;
    bool isSearching() const;
    std::string getSearchKey(const FilesRow& row) const;
    bool isSearchRowOpen(const FilesRow& row) const;
    void setSearchRowOpen(const FilesRow& row, bool isOpen);
    void setSearchScope(s32 root, const std::string& folderKey);
    void clearSearchScope();
    void resolveSearchScope();
    s32 findPackAncestor(s32 root, s32 folder) const;
    bool isRootScanning(s32 root) const;

    static bool isPreviewable(const FileProbe& probe);
    static bool isOpenable(const FileProbe& probe);

    bool isFileShown(const BrowserFile& file) const;
    static void collectOpenFiles(FrameContext& ctx);
    static const OpenFileMark* findOpenFile(const FrameContext& ctx, const std::string& path);
    static ImU32 getOpenNameColor(const OpenFileMark* mark);
    static ImU32 getOpenMarkColor(const OpenFileMark* mark);
    const char* getFileIcon(const BrowserFile& file, ImU32* color) const;
    void countShownFiles(FrameContext& ctx, s32 root, s32 folder);
    void invalidateRows();
    bool isRowsKeyCurrent(const FrameContext& ctx) const;
    void updateRows(FrameContext& ctx);
    void collectRows(FrameContext& ctx);
    void collectRootRows(FrameContext& ctx, s32 root);
    void collectFolderRows(const FrameContext& ctx, std::vector<FilesRow>& rows, s32 root, s32 folder, s32 depth, bool isAllOpen);
    void collectFileRows(std::vector<FilesRow>& rows, s32 root, s32 folder, s32 depth, bool isAllOpen);
    void markSharedPrefixes(std::vector<FilesRow>& rows, size_t firstFile) const;
    void collectAllOpenRows(const FrameContext& ctx, std::vector<FilesRow>& rows);
    void collectSearchRows(FrameContext& ctx, s32 root, bool isAllOpen);
    void collectFileRootSearchRows(FrameContext& ctx, s32 root, bool isAllOpen);
    void countSearchHits(SearchCounts& counts, s32 root, s32 folder, bool isInherited) const;
    void collectSearchFolderRows(FrameContext& ctx, const SearchCounts& counts, s32 root, s32 folder, s32 depth, bool isAllOpen);
    void collectSearchFileRows(FrameContext& ctx, const SearchCounts& counts, s32 root, s32 folder, s32 depth, bool isAllOpen);
    void countScopeFiles(FrameContext& ctx) const;
    u32 getSoundMatchCount(s32 root, s32 file) const;
    void updateSoundMatches();
    bool drawSearchField();

    void drawKindMenu();
    void drawViewOptions();
    bool isAnyKindHidden() const;
    void drawEmptyState(const char* text);
    void drawTable(FrameContext& ctx);
    void drawRow(FrameContext& ctx, const FilesRow& row);
    const std::string& getRowName(const FilesRow& row) const;
    const std::string& getShownFileName(const BrowserFile& file) const;
    f32 calcRowNoteWidth(const FrameContext& ctx, const FilesRow& row) const;
    f32 calcNameWant(const FrameContext& ctx, const std::vector<FilesRow>& rows) const;
    void calcValueWidths(FrameContext& ctx, const std::vector<FilesRow>& rows) const;
    void setupColumns(FrameContext& ctx);
    RowFrame beginRow(FrameContext& ctx, const FilesRow& row, f32 indent);
    static bool isChevronClicked(const FrameContext& ctx, const RowFrame& frame);
    void drawRootRow(FrameContext& ctx, const FilesRow& row);
    f32 drawRootLabel(const FrameContext& ctx, const FilesRow& row, const RowFrame& frame, f32 right, bool isCountHidden, bool hasValues);
    void drawRootButtons(const FilesRoot& root, s32 index, const RowFrame& frame, bool isRowHovered);
    void drawRootMenu(const FilesRoot& root, s32 index);
    void drawStatusRow(FrameContext& ctx, const FilesRow& row);
    void drawScanProgress(const FilesRoot& root, const ImVec2& pos, f32 right);
    void drawAddFolderRow(const FrameContext& ctx);
    void drawFolderRow(FrameContext& ctx, const FilesRow& row);
    void drawFolderMenu(s32 root, s32 folder);
    void drawBrowserItem(const std::string& path, u32 section);
    void drawPrefixRow(FrameContext& ctx, const FilesRow& row);
    void drawFileRow(FrameContext& ctx, const FilesRow& row);
    void drawSoundRow(FrameContext& ctx, const FilesRow& row);
    void drawValueCells(FrameContext& ctx, const char* format, const char* size);
    f32 drawRowLabel(const ImVec2& pos, f32 right, const char* icon, ImU32 iconColor, const char* name, ImU32 nameColor, const char* note, ImU32 noteColor, s32 prefixLength = 0, const char* count = nullptr, bool isCountShown = true);
    bool isSpanningRow(const FilesRow& row) const;
    f32 drawRowName(ImDrawList* drawList, const ImVec2& pos, f32 right, const char* name, ImU32 color, s32 prefixLength);
    void drawFileTooltip(const BrowserFile& file, bool showPath = false);
    void drawArchiveContents(const ArchiveContents& contents);
    void drawSoundTooltip(const BrowserFile& file, const ArchiveSound& sound);
    void drawFileCommands(const BrowserFile& file);
    bool drawSelectionMenu(const BrowserFile& file);
    void drawFileMenu(const FilesRow& row);
    void drawSoundMenu(const FrameContext& ctx, const BrowserFile& file, const ArchiveSound& sound);
    void drawFooter(FrameContext& ctx);
    void drawMatchSummary(const FrameContext& ctx);
    bool drawSelectionSummary();
    void handleKeys(FrameContext& ctx);
    void select(const FilesRow& row, bool reportsReadFailure = true);
    void clickSelection(const FrameContext& ctx, const FilesRow& row, const BrowserFile& file);
    void selectAll(const FrameContext& ctx);
    void clearMultiSelection();
    void onSelectionChanged();
    bool isRowSelected(const FilesRow& row) const;
    bool isPathSelected(const std::string& path) const;
    std::vector<std::string> collectSelectablePaths(const FrameContext& ctx) const;
    std::vector<const BrowserFile*> collectSelectedFiles() const;
    void setAutoPlay(bool isOn);
    void previewFile(const BrowserFile& file, bool reportsReadFailure = true);
    void openFile(const BrowserFile& file);
    void showFile(const BrowserFile& file);
    void openSound(const FrameContext& ctx, const BrowserFile& file, const ArchiveSound& sound);
    void selectPendingSound(const FrameContext& ctx);
    void confirmRow(const FrameContext& ctx, const FilesRow& row);
    void confirmSelection(const FrameContext& ctx);
    void openSelectedFiles();
    void handleSearchKeys();
    bool isRowOpen(const FilesRow& row) const;
    void setRowOpen(const FilesRow& row, bool isOpen);
    s32 collapseRow(const FrameContext& ctx, s32 current);
    s32 expandRow(const FrameContext& ctx, s32 current);

    std::string getRowPath(const FilesRow& row) const;
    FileDrag makeRowDrag(const FilesRow& row, const std::string& path) const;
    void drawRowDragSource(const FilesRow& row);
    void beginDrop(FrameContext& ctx);
    void recordDropRow(FrameContext& ctx, const FilesRow& row);
    void drawDropLine(FrameContext& ctx);
    void findRootDrop(FilesDropFrame& drop) const;
    void findSectionDrop(FilesDropFrame& drop) const;
    void drawDropTarget(const FrameContext& ctx);
    void applyPendingDrop();
    bool isDropMove() const;
    bool isDropNoMove(const FilesDropFrame& drop) const;

    struct SoundMatches
    {
        std::string query;
        u32 generation = 0;
        std::vector<std::vector<FilesRow>> rows;
        std::vector<std::vector<u32>> starts;
    };

    struct RemovedRoot
    {
        u32 token = 0;
        s32 index = 0;
        FilesRoot root;
    };

    struct RemovedSection
    {
        u32 token = 0;
        s32 index = 0;
        FilesSection section;
        std::vector<std::string> rootPaths;
    };

    struct PendingSound
    {
        std::string path;
        std::string name;
        std::string openPathAtRequest;
    };

    std::vector<FilesRoot> mRoots;
    std::vector<FilesSection> mSections;
    u32 mNextSectionId = 1;
    FrameContext mFrame;
    RowsKey mRowsKey;
    FilePreview mPreview;
    char mSearch[128] = {};
    bool mKindShown[cKindFilterCount] = { true, true, true, true, true };
    FilesRow mSelected;
    std::vector<std::string> mSelectedPaths;
    std::vector<std::string> mSelectedSorted;
    std::string mSelectAnchor;
    u64 mSelectedSize = 0;
    bool mCanExportSelection = false;
    SoundMatches mSoundMatches;
    PendingSound mPendingSound;
    PendingAction mPendingAction;
    PendingDrop mPendingDrop;
    u32 mPendingSectionMove = 0;
    s32 mPendingSectionSlot = -1;
    u32 mRenameRequest = 0;
    u32 mIgnoredPressSection = 0;
    std::vector<std::string> mDropPaths;
    u32 mDropSerial = 0;
    std::vector<RemovedRoot> mRemovedRoots;
    std::vector<RemovedSection> mRemovedSections;
    std::vector<std::string> mSearchToggled;
    SearchScope mScope;
    bool mFocusSearch = false;
    u32 mNextUndoToken = 1;
    u32 mUndoToken = 0;
    u32 mSnapshotGeneration = 0;
    u32 mRowsGeneration = 1;
    u32 mScanRequestCount = 0;
    bool mScrollToSelected = false;
    bool mAutoPlay = true;
    bool mGroupByPrefix = false;
    bool mShowExtensions = false;
    bool mShowFormat = true;
    bool mShowSize = false;
    bool mShowCount = false;
};
