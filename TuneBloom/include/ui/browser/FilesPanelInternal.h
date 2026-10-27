#pragma once

#include <browser/FileIndex.h>
#include <ui/browser/FilePreview.h>
#include <ui/browser/FileWavExport.h>

#include <imgui/imgui.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

inline constexpr f32 cIconGapEm = 0.4f;
inline constexpr f32 cIndentEm  = 1.1f;

inline constexpr const char* cFolderMenuId = "##foldermenu";

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
    Group,
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
    bool isPinned = false;
    bool isOpen = true;
    bool isMissing = false;
    bool hasSnapshot = false;
    bool needsScan = false;
    bool isFullScan = false;
};

struct OpenFileMark
{
    std::string path;
    bool isActive = false;
    bool isUnsaved = false;
};

class FilesPanel
{
public:
    void loadConfig();
    void openFolder(const std::string& path, bool isPinned = false);
    bool chooseFolder();
    void draw();

    static KindFilter getKindFilter(BrowserFileKind kind);

private:
    enum class RootAction
    {
        None,
        TogglePin,
        MoveUp,
        MoveDown,
        Rescan,
        Remove,
        Open
    };

    struct PendingAction
    {
        RootAction action = RootAction::None;
        std::string rootPath;
        std::string path;
        bool isPinned = false;
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
        f32 em = 0.0f;
        f32 chevronWidth = 0.0f;
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

    struct RowsKey
    {
        u32 rowsGeneration = 0;
        u32 snapshotGeneration = 0;
        f32 em = 0.0f;
        std::string search;
        std::vector<std::string> openPaths;
    };

    s32 findRoot(const std::string& path) const;
    s32 addRoot(const std::string& path, bool isPinned);
    s32 insertRoot(FilesRoot root);
    void trimRecentRoots();
    void onRootsChanged();
    void selectRoot(s32 root);
    void requestScan(s32 root, bool isFullScan = false);
    void updateScans();
    void startScan(s32 root);
    void restoreExpanded(FilesRoot& root, std::vector<std::string> openFolders);
    void updateSizeLabelLengths(FilesRoot& root);
    std::string getFolderPath(s32 root, s32 folder) const;
    std::string makePrefixKey(const FilesRow& row) const;
    void requestAction(RootAction action, s32 root, const std::string& path = std::string(), bool isPinned = false);
    void applyPendingAction();
    void moveRoot(s32 root, s32 offset);
    s32 togglePin(s32 root);
    void saveConfig() const;

    const FileIndexSnapshot& getSnapshot(s32 root) const;
    const BrowserFile& getFile(const FilesRow& row) const;
    const BrowserFolder* getPackFolder(const FilesRow& row) const;
    s32 findPackAncestor(s32 root, s32 folder) const;
    bool isRootScanning(s32 root) const;

    static bool isPreviewable(const FileProbe& probe);

    bool isFileShown(const BrowserFile& file) const;
    static void collectOpenFiles(FrameContext& ctx);
    static const OpenFileMark* findOpenFile(const FrameContext& ctx, const std::string& path);
    void countShownFiles(FrameContext& ctx, s32 root, s32 folder);
    void invalidateRows();
    bool isRowsKeyCurrent(const FrameContext& ctx) const;
    void updateRows(FrameContext& ctx);
    void collectRows(FrameContext& ctx);
    void collectRootRows(FrameContext& ctx, s32 root);
    void collectFolderRows(const FrameContext& ctx, std::vector<FilesRow>& rows, s32 root, s32 folder, s32 depth, bool isAllOpen);
    void collectFileRows(std::vector<FilesRow>& rows, s32 root, s32 folder, s32 depth, bool isAllOpen);
    void collectAllOpenRows(const FrameContext& ctx, std::vector<FilesRow>& rows);
    void collectSearchRows(FrameContext& ctx, s32 root);
    void updateSoundMatches();

    void drawKindMenu();
    bool isAnyKindHidden() const;
    void drawEmptyState(const char* text);
    void drawTable(FrameContext& ctx);
    void drawRow(FrameContext& ctx, const FilesRow& row);
    const std::string& getRowName(const FilesRow& row) const;
    f32 calcRowNoteWidth(const FrameContext& ctx, const FilesRow& row) const;
    f32 calcNameWant(const FrameContext& ctx, const std::vector<FilesRow>& rows) const;
    void calcValueWidths(FrameContext& ctx, const std::vector<FilesRow>& rows) const;
    void setupColumns(FrameContext& ctx);
    RowFrame beginRow(FrameContext& ctx, const FilesRow& row, f32 indent);
    void drawRootRow(FrameContext& ctx, const FilesRow& row);
    void drawRootButtons(const FilesRoot& root, s32 index, const RowFrame& frame, bool isRowHovered);
    void drawRootMenu(const FilesRoot& root, s32 index);
    void drawStatusRow(FrameContext& ctx, const FilesRow& row);
    void drawGroupRow(const FilesRow& row);
    void drawScanProgress(const FilesRoot& root, const ImVec2& pos, f32 right);
    void drawAddFolderRow(const FrameContext& ctx);
    void drawFolderRow(FrameContext& ctx, const FilesRow& row);
    void drawFolderMenu(s32 root, s32 folder);
    void drawPrefixRow(FrameContext& ctx, const FilesRow& row);
    void drawFileRow(FrameContext& ctx, const FilesRow& row);
    void drawSoundRow(FrameContext& ctx, const FilesRow& row);
    void drawValueCells(FrameContext& ctx, const char* format, const char* size);
    f32 drawRowLabel(const ImVec2& pos, f32 right, const char* icon, ImU32 iconColor, const char* name, ImU32 nameColor, const char* note, ImU32 noteColor, s32 prefixLength = 0);
    f32 drawRowName(ImDrawList* drawList, const ImVec2& pos, f32 right, const char* name, ImU32 color, s32 prefixLength);
    void drawFileTooltip(const BrowserFile& file);
    void drawArchiveContents(const ArchiveContents& contents);
    void drawSoundTooltip(const BrowserFile& file, const ArchiveSound& sound);
    void drawFileMenu(const BrowserFile& file);
    void drawSoundMenu(const FrameContext& ctx, const BrowserFile& file, const ArchiveSound& sound);
    void drawFooter(FrameContext& ctx);
    void drawMatchSummary(const FrameContext& ctx);
    void handleKeys(FrameContext& ctx);
    void select(const FrameContext& ctx, const FilesRow& row);
    void setAutoPlay(bool isOn);
    void previewFile(const BrowserFile& file);
    void openFile(const BrowserFile& file);
    void showFile(const BrowserFile& file);
    void openSound(const FrameContext& ctx, const BrowserFile& file, const ArchiveSound& sound);
    void selectPendingSound(const FrameContext& ctx);
    void confirmRow(const FrameContext& ctx, const FilesRow& row);
    bool isRowOpen(const FilesRow& row) const;
    void setRowOpen(const FilesRow& row, bool isOpen);
    s32 collapseRow(const FrameContext& ctx, s32 current);
    s32 expandRow(const FrameContext& ctx, s32 current);

    struct SoundMatches
    {
        std::string query;
        u32 generation = 0;
        std::vector<std::vector<FilesRow>> rows;
        std::vector<std::vector<u8>> hasMatch;
    };

    struct PendingSound
    {
        std::string path;
        std::string name;
        std::string openPathAtRequest;
    };

    std::vector<FilesRoot> mRoots;
    FrameContext mFrame;
    RowsKey mRowsKey;
    FilePreview mPreview;
    FileWavExport mWavExport;
    char mSearch[128] = {};
    bool mKindShown[cKindFilterCount] = { true, true, true, true };
    FilesRow mSelected;
    SoundMatches mSoundMatches;
    PendingSound mPendingSound;
    PendingAction mPendingAction;
    u32 mSnapshotGeneration = 0;
    u32 mRowsGeneration = 1;
    u32 mScanRequestCount = 0;
    bool mScrollToSelected = false;
    bool mAutoPlay = true;
    bool mGroupByPrefix = false;
};
