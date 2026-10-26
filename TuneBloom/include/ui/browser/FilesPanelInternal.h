#pragma once

#include <browser/FileIndex.h>
#include <ui/browser/FilesColumnWidth.h>
#include <ui/browser/FilePreview.h>

#include <imgui/imgui.h>

#include <string>
#include <vector>

inline constexpr f32 cIconGapEm = 0.4f;

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
    Folder,
    File,
    Sound
};

struct FilesRowKey
{
    s32 kind;
    s32 index;
    s32 sound;
};

struct FilesRow
{
    FilesRowKind kind = FilesRowKind::File;
    s32 index = -1;
    s32 sound = -1;
    s32 depth = 0;
    s32 prefixLength = 0;

    bool isSame(const FilesRow& other) const
    {
        return kind == other.kind && index == other.index && sound == other.sound;
    }

    FilesRowKey key() const
    {
        return { static_cast<s32>(kind), index, sound };
    }
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
    void openFolder(const std::string& root);
    void updateColumnWidth();
    void draw();

    static KindFilter getKindFilter(BrowserFileKind kind);

private:
    struct FrameContext
    {
        const FileIndexSnapshot* snapshot = nullptr;
        std::vector<u32> shownCounts;
        std::vector<FilesRow> rows;
        u32 shownFileCount = 0;
        u32 shownSoundCount = 0;
        bool isSearching = false;
        bool showFormat = true;
        bool showSize = true;
        f32 formatWidth = 0.0f;
        f32 sizeWidth = 0.0f;
        std::string openPath;
        std::vector<OpenFileMark> openFiles;
        f32 em = 0.0f;
        f32 chevronWidth = 0.0f;
    };

    struct RowFrame
    {
        ImVec2 textPos;
        f32 right = 0.0f;
        bool isPressed = false;
        bool isDoubleClicked = false;
    };

    void setRoot(const std::string& root);
    void chooseFolder();
    void saveConfig() const;
    void syncSnapshot();
    void resetExpanded();
    void updateSizeLabelLengths();

    static bool isPreviewable(const FileProbe& probe);

    bool isFileShown(const BrowserFile& file) const;
    static void collectOpenFiles(FrameContext& ctx);
    static const OpenFileMark* findOpenFile(const FrameContext& ctx, const std::string& path);
    void countShownFiles(FrameContext& ctx, s32 folder);
    void collectRows(FrameContext& ctx);
    void collectFolderRows(FrameContext& ctx, s32 folder, s32 depth);
    void collectSearchRows(FrameContext& ctx);
    void updateSoundMatches(const FrameContext& ctx);

    void drawRootRow(FrameContext& ctx);
    void drawKindMenu(FrameContext& ctx);
    bool isAnyKindHidden() const;
    void drawEmptyState(const char* text, const char* detail, bool showButton);
    void drawTable(FrameContext& ctx);
    const std::string& getRowName(const FrameContext& ctx, const FilesRow& row) const;
    f32 calcRowNoteWidth(const FrameContext& ctx, const FilesRow& row) const;
    f32 calcNameWant(const FrameContext& ctx) const;
    void calcValueWidths(FrameContext& ctx) const;
    void setupColumns(FrameContext& ctx);
    RowFrame beginRow(FrameContext& ctx, const FilesRow& row, f32 indent);
    void drawFolderRow(FrameContext& ctx, const FilesRow& row);
    void drawFileRow(FrameContext& ctx, const FilesRow& row);
    void drawSoundRow(FrameContext& ctx, const FilesRow& row);
    void drawValueCells(FrameContext& ctx, const char* format, const char* size);
    void drawRowLabel(const ImVec2& pos, f32 right, const char* icon, ImU32 iconColor, const char* name, ImU32 nameColor, const char* note, ImU32 noteColor, s32 prefixLength = 0);
    f32 drawRowName(ImDrawList* drawList, const ImVec2& pos, f32 right, const char* name, ImU32 color, s32 prefixLength);
    void drawFileTooltip(const BrowserFile& file);
    void drawArchiveContents(const ArchiveContents& contents);
    void drawSoundTooltip(const BrowserFile& file, const ArchiveSound& sound);
    void drawFileMenu(const BrowserFile& file);
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
    s32 collapseRow(const FrameContext& ctx, s32 current);
    s32 expandRow(const FrameContext& ctx, s32 current);

    struct SoundMatches
    {
        std::string query;
        u32 generation = 0;
        std::vector<FilesRow> rows;
        std::vector<u8> hasMatch;
    };

    struct PendingSound
    {
        std::string path;
        std::string name;
        std::string openPathAtRequest;
    };

    FileIndex mIndex;
    FilePreview mPreview;
    FilesColumnWidth mColumnWidth;
    std::string mRoot;
    char mSearch[128] = {};
    bool mKindShown[cKindFilterCount] = { true, true, true, true };
    std::vector<u8> mExpanded;
    std::vector<u8> mSizeLabelLengths;
    FilesRow mSelected;
    SoundMatches mSoundMatches;
    PendingSound mPendingSound;
    u32 mSnapshotGeneration = 0;
    bool mScrollToSelected = false;
    bool mRootMissing = false;
    bool mAutoPlay = true;
    bool mIsColumnWidthDirty = false;
};
