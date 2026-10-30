#include <ui/file/DocumentTabs.h>

#include <ui/widgets/ContextMenu.h>
#include <ui/file/Document.h>
#include <ui/file/DocumentMgr.h>
#include <ui/file/DocumentState.h>
#include <ui/file/FileDrag.h>
#include <ui/file/FileFlow.h>
#include <ui/file/FileWindow.h>
#include <ui/browser/FilesPanel.h>
#include <messages/Messages.h>
#include <ui/file/PathText.h>
#include <ui/file/SystemShell.h>
#include <ui/items/Selection.h>
#include <ui/project/Sidebar.h>
#include <ui/settings/UiColors.h>
#include <ui/widgets/Widgets.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_custom.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <string>
#include <vector>

static constexpr f32 cStripPadYEm        = 0.25f;
static constexpr f32 cTabPadXEm          = 0.6f;
static constexpr f32 cIconGapEm          = 0.4f;
static constexpr f32 cCloseGapEm         = 0.3f;
static constexpr f32 cTabSpacingEm       = 0.25f;
static constexpr f32 cTabMinWidthEm      = 6.0f;
static constexpr f32 cTabMaxWidthEm      = 18.0f;
static constexpr f32 cActiveShareMax     = 0.4f;
static constexpr f32 cDirtyDotRadiusEm   = 0.16f;
static constexpr f32 cObliqueSkew        = 0.2f;
static constexpr f32 cWheelScrollEm      = 3.0f;
static constexpr f32 cTooltipHintGapEm   = 0.8f;
static constexpr f32 cGroupLineEm        = 0.14f;
static constexpr f32 cGroupHeadTint      = 0.22f;
static constexpr f32 cGroupOpenSeconds   = 0.15f;
static constexpr f32 cGroupFoldSeconds   = 0.12f;

static constexpr const char* cDocumentListId = "##DocumentList";

namespace
{

enum class TabKind
{
    Archive,
    File,
};

enum class CloseScope
{
    None,
    Others,
    All,
};

struct TabLayout
{
    TabKind kind = TabKind::Archive;
    Document* document = nullptr;
    FileWindow* fileWindow = nullptr;
    std::string name;
    const char* icon = nullptr;
    ImVec4 iconColor;
    ImU32 groupColor = 0;
    bool isActive = false;
    bool isGroupHead = false;
    bool hasGroup = false;
    bool isDirty = false;
    bool isOblique = false;
    bool isDim = false;
    bool hasCloseSlot = true;
    f32 fold = 1.0f;
    f32 groupFold = 0.0f;
    f32 labelWidth = 0.0f;
    f32 fullWidth = 0.0f;
    f32 compactWidth = 0.0f;
    f32 width = 0.0f;
    f32 left = 0.0f;
};

struct TabStatus
{
    const char* icon = nullptr;
    ImVec4 color;
    const char* label = nullptr;
    const char* hint = nullptr;
};

struct GroupLine
{
    f32 left = 0.0f;
    f32 right = 0.0f;
    ImU32 color = 0;
};

class DocumentTabStrip
{
public:
    void draw();

private:
    struct FrameContext
    {
        f32 em = 0.0f;
        f32 tabHeight = 0.0f;
        f32 padX = 0.0f;
        f32 iconGap = 0.0f;
        f32 closeGap = 0.0f;
        f32 closeSize = 0.0f;
        f32 spacing = 0.0f;
        f32 viewWidth = 0.0f;
        f32 contentWidth = 0.0f;
        bool scrolls = false;
        bool isLocked = false;
        std::vector<TabLayout> tabs;
        std::vector<GroupLine> groups;

        Document* activate = nullptr;
        Document* close = nullptr;
        Document* pin = nullptr;
        Document* save = nullptr;
        Document* saveAs = nullptr;
        Document* reload = nullptr;

        bool showRequest = false;
        FileWindow* show = nullptr;
        FileWindow* closeFile = nullptr;
        FileWindow* reveal = nullptr;

        CloseScope closeScope = CloseScope::None;
        const TabLayout* closeAnchor = nullptr;
    };

    void addTab(FrameContext& ctx, TabLayout tab) const;
    void measureFileTabs(FrameContext& ctx, Document* document, f32 fold) const;
    void measureTabs(FrameContext& ctx) const;
    void fitTabs(FrameContext& ctx, f32 available) const;
    f32 shareWidth(FrameContext& ctx, f32 available) const;
    void placeTabs(FrameContext& ctx) const;
    void scrollToActive(const FrameContext& ctx);
    void scrollWithWheel(const FrameContext& ctx) const;
    void drawBackground(f32 top, f32 bottom, f32 underlineLeft) const;
    void drawTabs(FrameContext& ctx);
    void drawGroupLine(const FrameContext& ctx) const;
    void drawTab(FrameContext& ctx, const TabLayout& tab);
    void drawTabItem(FrameContext& ctx, const TabLayout& tab, const ImRect& bb);
    void drawTabFace(const FrameContext& ctx, const TabLayout& tab, const ImRect& bb, ImGuiID tabId, bool isHovered, bool showsClose) const;
    void handleTabClick(FrameContext& ctx, const TabLayout& tab) const;
    void requestClose(FrameContext& ctx, const TabLayout& tab) const;
    void drawTabLabel(const FrameContext& ctx, const TabLayout& tab, const ImRect& bb, bool showsClose) const;
    void drawTabTooltip(const TabLayout& tab) const;
    void drawTabMenu(FrameContext& ctx, const TabLayout& tab) const;
    void drawFileTabMenu(FrameContext& ctx, const TabLayout& tab) const;
    void drawCloseItems(FrameContext& ctx, const TabLayout& tab) const;
    void drawDocumentList(FrameContext& ctx) const;
    void drawDocumentMenu(FrameContext& ctx) const;
    void closeArchiveTabs(const FrameContext& ctx) const;
    void closeFileTabs(const FrameContext& ctx) const;
    void applyFileActions(const FrameContext& ctx) const;
    void applyActions(const FrameContext& ctx) const;

    const void* mScrolledTo = nullptr;
};

}

static DocumentTabStrip sStrip;

static std::vector<TabStatus> CollectTabStatuses(const Document& document)
{
    const DocumentState& state = document.state();
    std::vector<TabStatus> statuses;

    if (state.unsaved)
        statuses.push_back({ ICON_LC_CIRCLE_DOT, uicolors::Warning(), messages::mainmenu::cUnsaved, nullptr });

    if (state.isPreview)
        statuses.push_back({ ICON_LC_EYE, ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), messages::documents::cPreview, messages::documents::cPreviewHint });

    if (!document.isLoaded())
        statuses.push_back({ ICON_LC_MOON, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), messages::documents::cUnloaded, messages::documents::cUnloadedHint });

    return statuses;
}

static f32 CalcTabStatusWidth(const TabStatus& status, f32 iconGap, f32 hintGap)
{
    f32 width = ImGui::CalcTextSize(status.icon).x + iconGap + ImGui::CalcTextSize(status.label).x;

    if (status.hint)
        width += hintGap + ImGui::CalcTextSize(status.hint).x;

    return width;
}

static void DrawTabStatus(const TabStatus& status, f32 iconGap, f32 hintGap)
{
    ImGui::TextColored(status.color, "%s", status.icon);

    ImGui::SameLine(0.0f, iconGap);
    ImGui::TextUnformatted(status.label);

    if (!status.hint)
        return;

    ImGui::SameLine(0.0f, hintGap);
    ImGui::TextDisabled("%s", status.hint);
}

static void DrawObliqueText(ImDrawList* drawList, const ImVec2& pos, f32 right, const char* text)
{
    const s32 firstVertex = drawList->VtxBuffer.Size;
    const f32 baseline = pos.y + ImGui::GetFont()->Ascent * (ImGui::GetFontSize() / ImGui::GetFont()->FontSize);

    ImGui::RenderTextEllipsis(drawList, pos, ImVec2(right, pos.y + ImGui::GetFontSize()), right, right, text, nullptr, nullptr);

    for (s32 i = firstVertex; i < drawList->VtxBuffer.Size; i++)
    {
        ImDrawVert& vertex = drawList->VtxBuffer[i];
        vertex.pos.x += (baseline - vertex.pos.y) * cObliqueSkew;
    }
}

static const void* TabKey(const TabLayout& tab)
{
    if (tab.kind == TabKind::File)
        return tab.fileWindow;

    return tab.document;
}

static bool HasOpenFileWindow(ItemList& fileWindows)
{
    for (Item* item : fileWindows)
    {
        if (static_cast<FileWindow*>(item)->isOpen())
            return true;
    }

    return false;
}

static f32 CalcGroupFold(const Document& document, bool isOpen)
{
    ImGui::PushID(static_cast<s32>(document.state().serial));
    const ImGuiID id = ImGui::GetID("##GroupFold");
    ImGui::PopID();

    return ImGui::GetTransitionRatio(id, isOpen, cGroupOpenSeconds, cGroupFoldSeconds);
}

static bool IsFoldingTab(const TabLayout& tab)
{
    return tab.kind == TabKind::File && tab.document != ActiveDocument();
}

static std::vector<const TabLayout*> CollectCloseTargets(const std::vector<TabLayout>& tabs, CloseScope scope, const TabLayout& anchor)
{
    std::vector<const TabLayout*> targets;

    for (const TabLayout& tab : tabs)
    {
        if (tab.kind != anchor.kind || IsFoldingTab(tab))
            continue;

        if (scope == CloseScope::All || (scope == CloseScope::Others && &tab != &anchor))
            targets.push_back(&tab);
    }

    return targets;
}

static void DrawTabDragSource(const TabLayout& tab)
{
    if (tab.kind != TabKind::Archive || tab.document->state().canonicalPath.empty())
        return;

    FileDragSource([&tab]()
    {
        FileDrag drag;
        drag.paths.push_back(tab.document->state().canonicalPath);
        drag.name = tab.name;
        drag.icon = tab.icon;
        drag.iconColor = ImGui::GetColorU32(tab.iconColor);
        drag.origin = FileDragOrigin::Tab;

        return drag;
    });
}

void DocumentTabStrip::addTab(FrameContext& ctx, TabLayout tab) const
{
    const f32 minWidth = ctx.em * cTabMinWidthEm;
    const f32 maxWidth = ctx.em * cTabMaxWidthEm;
    const bool keepsCloseSlot = tab.isActive || tab.isGroupHead || tab.isDirty;

    tab.labelWidth = ctx.padX * 2.0f + ImGui::CalcTextSize(tab.icon).x + ctx.iconGap + ImGui::CalcTextSize(tab.name.c_str()).x;
    tab.fullWidth = ImFloor(std::clamp(tab.labelWidth + ctx.closeGap + ctx.closeSize, minWidth, maxWidth) * tab.fold);
    tab.compactWidth = keepsCloseSlot ? tab.fullWidth : ImFloor(std::clamp(tab.labelWidth, minWidth, maxWidth) * tab.fold);
    tab.width = tab.fullWidth;

    ctx.tabs.push_back(std::move(tab));
}

void DocumentTabStrip::measureFileTabs(FrameContext& ctx, Document* document, f32 fold) const
{
    const bool isActiveDocument = document == ActiveDocument();
    const FileWindow* shown = isActiveDocument ? GetShownFileWindow() : nullptr;
    const ImVec4 textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);

    for (Item* item : document->state().fileWindows)
    {
        FileWindow* window = static_cast<FileWindow*>(item);

        if (!window->isOpen() || !window->getFileRef().isAttached())
            continue;

        TabLayout file;
        file.kind = TabKind::File;
        file.document = document;
        file.fileWindow = window;
        file.name = window->getFileRef().getItem()->getFormattedName().cstr();
        file.icon = GetFileWindowIcon(*window);
        file.iconColor = textColor;
        file.isActive = shown == window;
        file.isDirty = IsFileWindowDirty(*window);
        file.fold = fold;

        addTab(ctx, std::move(file));
    }
}

void DocumentTabStrip::measureTabs(FrameContext& ctx) const
{
    for (Document* document : GetOpenDocuments())
    {
        const DocumentState& state = document->state();
        const bool isActiveDocument = document == ActiveDocument();
        const bool hasFiles = HasOpenFileWindow(document->state().fileWindows);
        const bool isGroupOpen = isActiveDocument && hasFiles;
        const f32 groupFold = hasFiles ? CalcGroupFold(*document, isGroupOpen) : 0.0f;

        TabLayout tab;
        tab.document = document;
        tab.name = document->displayName();
        tab.icon = GetFileKindIcon(document->fileKind());
        tab.iconColor = GetFileKindColor(document->fileKind());
        tab.groupColor = ImGui::GetColorU32(tab.iconColor);
        tab.isActive = isActiveDocument && (!isGroupOpen || GetShownFileWindow() == nullptr);
        tab.isGroupHead = isGroupOpen;
        tab.hasGroup = hasFiles;
        tab.groupFold = groupFold;
        tab.isDirty = state.unsaved;
        tab.isOblique = state.isPreview;
        tab.isDim = !document->isLoaded();

        addTab(ctx, std::move(tab));

        if (groupFold > 0.0f)
            measureFileTabs(ctx, document, groupFold);
    }
}

static f32 CalcSpacingTotal(const std::vector<TabLayout>& tabs, f32 spacing)
{
    f32 total = 0.0f;

    for (size_t i = 0; i + 1 < tabs.size(); i++)
        total += ImFloor(spacing * tabs[i].fold);

    return total;
}

f32 DocumentTabStrip::shareWidth(FrameContext& ctx, f32 available) const
{
    const s32 tabNum = static_cast<s32>(ctx.tabs.size());
    const f32 spacingWidth = CalcSpacingTotal(ctx.tabs, ctx.spacing);

    f32 remaining = available - spacingWidth;
    s32 remainingNum = tabNum;
    f32 total = spacingWidth;

    std::vector<TabLayout*> byWidth;

    for (TabLayout& tab : ctx.tabs)
    {
        tab.width = tab.compactWidth;
        tab.hasCloseSlot = tab.compactWidth == tab.fullWidth;

        if (!tab.isActive)
        {
            byWidth.push_back(&tab);
            continue;
        }

        const f32 othersMinWidth = ctx.em * cTabMinWidthEm * static_cast<f32>(tabNum - 1);
        const f32 activeMax = sead::Mathf::min(ImFloor(remaining * cActiveShareMax), remaining - othersMinWidth);

        tab.width = sead::Mathf::max(sead::Mathf::min(tab.width, activeMax), ImFloor(ctx.em * cTabMinWidthEm * tab.fold));
        remaining -= tab.width;
        remainingNum--;
        total += tab.width;
    }

    std::sort(byWidth.begin(), byWidth.end(), [](const TabLayout* a, const TabLayout* b)
    {
        return a->width < b->width;
    });

    for (TabLayout* tab : byWidth)
    {
        const f32 share = sead::Mathf::max(ImFloor(remaining / static_cast<f32>(remainingNum)), ctx.em * cTabMinWidthEm);

        tab->width = sead::Mathf::min(tab->width, share);
        remaining -= tab->width;
        remainingNum--;
        total += tab->width;
    }

    return total;
}

void DocumentTabStrip::fitTabs(FrameContext& ctx, f32 available) const
{
    f32 fullTotal = CalcSpacingTotal(ctx.tabs, ctx.spacing);

    for (const TabLayout& tab : ctx.tabs)
        fullTotal += tab.fullWidth;

    ctx.viewWidth = available;

    if (fullTotal > available && shareWidth(ctx, available) > available)
    {
        ctx.scrolls = true;
        ctx.viewWidth = available - ctx.tabHeight - ctx.spacing;

        shareWidth(ctx, ctx.viewWidth);
    }

    placeTabs(ctx);
}

void DocumentTabStrip::placeTabs(FrameContext& ctx) const
{
    f32 left = 0.0f;

    ctx.groups.clear();

    for (TabLayout& tab : ctx.tabs)
    {
        tab.left = left;
        left += tab.width + ImFloor(ctx.spacing * tab.fold);

        if (tab.kind == TabKind::File)
            ctx.groups.back().right = tab.left + tab.width;
        else if (tab.hasGroup)
            ctx.groups.push_back({ tab.left, tab.left + tab.width, tab.groupColor });
    }

    ctx.contentWidth = left - ImFloor(ctx.spacing * ctx.tabs.back().fold);
}

void DocumentTabStrip::scrollToActive(const FrameContext& ctx)
{
    for (const TabLayout& tab : ctx.tabs)
    {
        if (!tab.isActive || TabKey(tab) == mScrolledTo)
            continue;

        mScrolledTo = TabKey(tab);

        const f32 scroll = ImGui::GetScrollX();

        if (tab.left < scroll)
            ImGui::SetScrollX(tab.left);
        else if (tab.left + tab.width > scroll + ctx.viewWidth)
            ImGui::SetScrollX(tab.left + tab.width - ctx.viewWidth);
    }
}

void DocumentTabStrip::scrollWithWheel(const FrameContext& ctx) const
{
    if (!ctx.scrolls || !ImGui::IsWindowHovered())
        return;

    const ImGuiIO& io = ImGui::GetIO();
    const f32 wheel = io.MouseWheelH != 0.0f ? -io.MouseWheelH : io.MouseWheel;

    if (wheel != 0.0f)
        ImGui::SetScrollX(ImGui::GetScrollX() - wheel * ctx.em * cWheelScrollEm);
}

void DocumentTabStrip::drawTabLabel(const FrameContext& ctx, const TabLayout& tab, const ImRect& bb, bool showsClose) const
{
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const f32 textY = bb.Min.y + ImFloor((bb.GetHeight() - ImGui::GetFontSize()) * 0.5f);

    const f32 iconX = bb.Min.x + ctx.padX;
    const f32 nameX = iconX + ImGui::CalcTextSize(tab.icon).x + ctx.iconGap;
    const f32 nameRight = bb.Max.x - ctx.padX - (tab.hasCloseSlot ? ctx.closeSize + ctx.closeGap : 0.0f);

    drawList->AddText(ImVec2(iconX, textY), ImGui::GetColorU32(tab.iconColor), tab.icon);

    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(tab.isDim ? ImGuiCol_TextDisabled : ImGuiCol_Text));

    if (tab.isOblique)
        DrawObliqueText(drawList, ImVec2(nameX, textY), nameRight, tab.name.c_str());
    else
        ImGui::RenderTextEllipsis(drawList, ImVec2(nameX, textY), ImVec2(nameRight, textY + ImGui::GetFontSize()), nameRight, nameRight, tab.name.c_str(), nullptr, nullptr);

    ImGui::PopStyleColor();

    if (tab.isDirty && !showsClose)
    {
        const ImVec2 center(bb.Max.x - ctx.padX - ctx.closeSize * 0.5f, bb.GetCenter().y);
        drawList->AddCircleFilled(center, ImFloor(ctx.em * cDirtyDotRadiusEm), ImGui::GetColorU32(uicolors::Warning()));
    }
}

void DocumentTabStrip::drawTabTooltip(const TabLayout& tab) const
{
    if (!ImGui::BeginItemTooltip())
        return;

    const f32 em = ImGui::GetFontSize();
    const f32 iconGap = em * cIconGapEm;
    const f32 hintGap = em * cTooltipHintGapEm;
    const std::vector<TabStatus> statuses = CollectTabStatuses(*tab.document);

    f32 width = ImGui::CalcTextSize(tab.name.c_str()).x;

    for (const TabStatus& status : statuses)
        width = ImMax(width, CalcTabStatusWidth(status, iconGap, hintGap));

    ImGui::TextUnformatted(tab.name.c_str());

    pathtext::DrawTooltipFolder(tab.document->filePath().cstr(), width);

    if (!statuses.empty())
        ImGui::Separator();

    for (const TabStatus& status : statuses)
        DrawTabStatus(status, iconGap, hintGap);

    ImGui::EndTooltip();
}

void DocumentTabStrip::drawTabMenu(FrameContext& ctx, const TabLayout& tab) const
{
    if (!BeginItemContextMenu("##TabMenu"))
        return;

    Document* document = tab.document;
    const sead::SafeString& path = document->filePath();
    const char* lockReason = messages::documents::cSwitchLocked;

    if (document->state().isPreview && ContextMenuItem(ICON_LC_PIN, messages::documents::cKeepOpen))
        ctx.pin = document;

    ContextMenuSeparator();

    if (ContextMenuItem(ICON_LC_SAVE, messages::common::cSave, !ctx.isLocked && tab.isDirty, ctx.isLocked ? lockReason : messages::unsaved::cNoChanges))
        ctx.save = document;

    if (ContextMenuItem(ICON_LC_SAVE_ALL, messages::mainmenu::cSaveAs, !ctx.isLocked, lockReason))
        ctx.saveAs = document;

    if (!path.isEmpty() && ContextMenuItem(ICON_LC_ROTATE_CCW, messages::documents::cReload, !ctx.isLocked, lockReason))
        ctx.reload = document;

    ContextMenuSeparator();

    if (ContextMenuItem(ICON_LC_FOLDER_SEARCH, messages::recent::cShowInFileManager, !path.isEmpty()))
        ShowInFileManager(path.cstr());

    if (ContextMenuItem(ICON_LC_COPY, messages::mainmenu::cCopyPath, !path.isEmpty()))
        ImGui::SetClipboardText(path.cstr());

    drawCloseItems(ctx, tab);

    EndContextMenu();
}

void DocumentTabStrip::drawFileTabMenu(FrameContext& ctx, const TabLayout& tab) const
{
    if (!BeginItemContextMenu("##TabMenu"))
        return;

    if (ContextMenuItem(ICON_LC_LOCATE, messages::documents::cReveal))
        ctx.reveal = tab.fileWindow;

    drawCloseItems(ctx, tab);

    EndContextMenu();
}

void DocumentTabStrip::drawCloseItems(FrameContext& ctx, const TabLayout& tab) const
{
    const char* lockReason = messages::documents::cSwitchLocked;

    ContextMenuSeparator();

    if (ContextMenuItem(ICON_LC_X, messages::common::cClose, !ctx.isLocked, lockReason))
        requestClose(ctx, tab);

    const bool hasOthers = !CollectCloseTargets(ctx.tabs, CloseScope::Others, tab).empty();
    CloseScope scope = CloseScope::None;

    if (ContextMenuItem(ICON_LC_COPY_X, messages::documents::cCloseOthers, !ctx.isLocked && hasOthers, ctx.isLocked ? lockReason : messages::documents::cNoOtherTabs))
        scope = CloseScope::Others;

    if (ContextMenuItem(ICON_LC_LIST_X, messages::documents::cCloseAll, !ctx.isLocked, lockReason))
        scope = CloseScope::All;

    if (scope == CloseScope::None)
        return;

    ctx.closeScope = scope;
    ctx.closeAnchor = &tab;
}

void DocumentTabStrip::requestClose(FrameContext& ctx, const TabLayout& tab) const
{
    if (tab.kind == TabKind::File)
        ctx.closeFile = tab.fileWindow;
    else
        ctx.close = tab.document;
}

void DocumentTabStrip::handleTabClick(FrameContext& ctx, const TabLayout& tab) const
{
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
    {
        requestClose(ctx, tab);
        return;
    }

    if (tab.kind == TabKind::Archive)
        ctx.activate = tab.document;

    if (tab.kind == TabKind::File || tab.document == ActiveDocument())
    {
        ctx.showRequest = true;
        ctx.show = tab.fileWindow;
    }
}

void DocumentTabStrip::drawTabFace(const FrameContext& ctx, const TabLayout& tab, const ImRect& bb, ImGuiID tabId, bool isHovered, bool showsClose) const
{
    ImVec4 idleColor = ImGui::GetStyleColorVec4(tab.isActive ? ImGuiCol_TabActive : ImGuiCol_Tab);
    ImVec4 hoverColor = ImGui::GetStyleColorVec4(tab.isActive ? ImGuiCol_TabActive : ImGuiCol_TabHovered);

    if (tab.groupFold > 0.0f && !tab.isActive)
    {
        const ImVec4 groupColor = ImGui::ColorConvertU32ToFloat4(tab.groupColor);
        const f32 tint = cGroupHeadTint * tab.groupFold;

        idleColor = ImLerp(idleColor, groupColor, tint);
        hoverColor = ImLerp(hoverColor, groupColor, tint);
    }

    ImGui::TabItemBackground(ImGui::GetWindowDrawList(), bb, ImGuiTabItemFlags_None, ImGui::GetHoverColorU32(tabId, isHovered, ImGui::GetColorU32(idleColor), ImGui::GetColorU32(hoverColor)));

    drawTabLabel(ctx, tab, bb, showsClose);
}

void DocumentTabStrip::drawTab(FrameContext& ctx, const TabLayout& tab)
{
    if (tab.width < 1.0f)
        return;

    ImGui::PushID(static_cast<s32>(tab.document->state().serial));
    ImGui::PushID(TabKey(tab));

    const ImVec2 origin = ImGui::GetWindowPos() - ImVec2(ImGui::GetScrollX(), 0.0f);
    const ImRect bb(origin + ImVec2(tab.left, 0.0f), origin + ImVec2(tab.left + tab.width, ctx.tabHeight));
    const bool isMoving = tab.fold < 1.0f;

    if (isMoving)
    {
        ImGui::PushClipRect(bb.Min, bb.Max, true);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * tab.fold);
    }

    if (IsFoldingTab(tab))
        drawTabFace(ctx, tab, bb, 0, false, false);
    else
        drawTabItem(ctx, tab, bb);

    if (isMoving)
    {
        ImGui::PopStyleVar();
        ImGui::PopClipRect();
    }

    ImGui::PopID();
    ImGui::PopID();
}

void DocumentTabStrip::drawTabItem(FrameContext& ctx, const TabLayout& tab, const ImRect& bb)
{
    ImGui::SetCursorScreenPos(bb.Min);
    ImGui::SetNextItemAllowOverlap();

    const ImGuiButtonFlags buttonFlags = static_cast<ImGuiButtonFlags>(ImGuiButtonFlags_PressedOnClick) | ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle;
    const bool clicked = ImGui::InvisibleButton("##Tab", bb.GetSize(), buttonFlags);
    const bool isHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseHoveringRect(bb.Min, bb.Max);
    const ImGuiID tabId = ImGui::GetItemID();

    DrawTabDragSource(tab);

    if (clicked && !ctx.isLocked)
        handleTabClick(ctx, tab);

    if (tab.kind == TabKind::Archive)
    {
        if (isHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            ctx.pin = tab.document;

        drawTabTooltip(tab);
        drawTabMenu(ctx, tab);
    }
    else
    {
        if (tab.width < tab.labelWidth)
            ImGui::SetItemTooltip("%s", tab.name.c_str());

        drawFileTabMenu(ctx, tab);
    }

    const bool showsClose = tab.hasCloseSlot && (isHovered || tab.isActive || tab.isGroupHead);

    drawTabFace(ctx, tab, bb, tabId, isHovered, showsClose);

    if (showsClose)
    {
        const ImVec2 closePos(bb.Max.x - ctx.padX - ctx.closeSize, bb.GetCenter().y - ctx.closeSize * 0.5f);

        if (ImGui::CloseButton(ImGui::GetID("##Close"), closePos) && !ctx.isLocked)
            requestClose(ctx, tab);
    }
}

void DocumentTabStrip::drawGroupLine(const FrameContext& ctx) const
{
    const ImVec2 origin = ImGui::GetWindowPos() - ImVec2(ImGui::GetScrollX(), 0.0f);
    const f32 thickness = ImMax(2.0f, ImFloor(ctx.em * cGroupLineEm));

    for (const GroupLine& group : ctx.groups)
    {
        const ImVec2 lineMin = origin + ImVec2(group.left, ctx.tabHeight - thickness);
        const ImVec2 lineMax = origin + ImVec2(group.right, ctx.tabHeight);

        ImGui::GetWindowDrawList()->AddRectFilled(lineMin, lineMax, group.color, thickness * 0.5f);
    }
}

void DocumentTabStrip::drawBackground(f32 top, f32 bottom, f32 underlineLeft) const
{
    const ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImDrawList* drawList = window->DrawList;
    const f32 left = window->Pos.x;
    const f32 right = left + window->Size.x;
    const f32 underlineTop = bottom - ImGui::GetStyle().TabBarBorderSize;

    drawList->AddRectFilled(ImVec2(left, top), ImVec2(right, bottom), ImGui::GetColorU32(ImGuiCol_MenuBarBg));
    drawList->AddRectFilled(ImVec2(underlineLeft, underlineTop), ImVec2(right, bottom), ImGui::GetColorU32(ImGuiCol_TabActive));
}

void DocumentTabStrip::drawTabs(FrameContext& ctx)
{
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav;

    if (ImGui::BeginChild("##DocumentTabs", ImVec2(ctx.viewWidth, ctx.tabHeight), ImGuiChildFlags_None, flags))
    {
        for (const TabLayout& tab : ctx.tabs)
            drawTab(ctx, tab);

        drawGroupLine(ctx);

        ImGui::SetCursorPos(ImVec2(ctx.contentWidth, 0.0f));
        ImGui::Dummy(ImVec2(0.0f, ctx.tabHeight));

        scrollWithWheel(ctx);
        scrollToActive(ctx);
    }

    ImGui::EndChild();
}

void DocumentTabStrip::drawDocumentList(FrameContext& ctx) const
{
    if (!ctx.scrolls)
        return;

    ImGui::SameLine(0.0f, ctx.spacing);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav;

    bool isPressed = false;
    ImVec2 menuPos;

    if (ImGui::BeginChild("##DocumentListButton", ImVec2(ctx.tabHeight, ctx.tabHeight), ImGuiChildFlags_None, flags))
    {
        isPressed = DrawIconButton("##Button", ICON_LC_CHEVRON_DOWN, ImVec2(ctx.tabHeight, ctx.tabHeight), ctx.tabHeight, ctx.em, false);

        ImGui::SetItemTooltip("%s", messages::documents::cOpenFiles);

        menuPos = ImVec2(ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y + ctx.spacing);
    }

    ImGui::EndChild();

    if (isPressed)
        ImGui::OpenPopup(cDocumentListId);

    ImGui::SetNextWindowPos(menuPos, ImGuiCond_Appearing, ImVec2(1.0f, 0.0f));

    drawDocumentMenu(ctx);
}

void DocumentTabStrip::drawDocumentMenu(FrameContext& ctx) const
{
    if (!BeginContextMenu(cDocumentListId))
        return;

    for (const TabLayout& tab : ctx.tabs)
    {
        if (tab.kind != TabKind::Archive)
            continue;

        ImGui::PushID(static_cast<s32>(tab.document->state().serial));

        if (ContextMenuCheckItem(tab.name.c_str(), tab.document == ActiveDocument(), !ctx.isLocked))
            ctx.activate = tab.document;

        ImGui::PopID();
    }

    EndContextMenu();
}

void DocumentTabStrip::closeArchiveTabs(const FrameContext& ctx) const
{
    Document* active = ActiveDocument();
    bool closesActive = false;

    for (const TabLayout* tab : CollectCloseTargets(ctx.tabs, ctx.closeScope, *ctx.closeAnchor))
    {
        if (tab->document == active)
            closesActive = true;
        else
            RequestCloseDocument(tab->document);
    }

    if (!closesActive)
        return;

    if (ctx.closeScope == CloseScope::Others)
        RequestDocumentActivation(ctx.closeAnchor->document);

    RequestCloseDocument(active);
}

void DocumentTabStrip::closeFileTabs(const FrameContext& ctx) const
{
    for (const TabLayout* tab : CollectCloseTargets(ctx.tabs, ctx.closeScope, *ctx.closeAnchor))
    {
        if (!tab->isDirty)
            *tab->fileWindow->getOpenPtr() = false;
    }

    const FileWindow* shown = GetShownFileWindow();

    if (shown && !shown->isOpen() && ctx.closeScope != CloseScope::All)
        ShowFileWindow(ctx.closeAnchor->fileWindow);
}

void DocumentTabStrip::applyFileActions(const FrameContext& ctx) const
{
    if (ctx.showRequest)
        ShowFileWindow(ctx.show);

    if (ctx.closeFile)
        RequestCloseFileWindow(ctx.closeFile);

    if (ctx.reveal && ctx.reveal->getFileRef().isAttached())
        SelectItem(ctx.reveal->getFileRef().getItem());
}

void DocumentTabStrip::applyActions(const FrameContext& ctx) const
{
    applyFileActions(ctx);

    if (ctx.pin)
        PinDocument(ctx.pin);

    if (ctx.activate && ctx.activate != ActiveDocument())
        RequestDocumentActivation(ctx.activate);

    if (ctx.save)
        RequestSaveDocument(ctx.save);

    if (ctx.saveAs)
        RequestSaveDocumentAs(ctx.saveAs);

    if (ctx.reload)
        RequestReloadDocument(ctx.reload);

    if (ctx.close)
        RequestCloseDocument(ctx.close);

    if (ctx.closeScope == CloseScope::None)
        return;

    if (ctx.closeAnchor->kind == TabKind::File)
        closeFileTabs(ctx);
    else
        closeArchiveTabs(ctx);
}

void DocumentTabStrip::draw()
{
    FrameContext ctx;
    ctx.em = ImGui::GetFontSize();
    ctx.tabHeight = ImGui::GetFrameHeight();
    ctx.padX = ImFloor(ctx.em * cTabPadXEm);
    ctx.iconGap = ImFloor(ctx.em * cIconGapEm);
    ctx.closeGap = ImFloor(ctx.em * cCloseGapEm);
    ctx.closeSize = ctx.em;
    ctx.spacing = ImFloor(ctx.em * cTabSpacingEm);
    ctx.isLocked = IsDocumentSwitchLocked();

    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 stripMin = ImGui::GetCursorScreenPos();
    const f32 tabsTop = stripMin.y + ImFloor(ctx.em * cStripPadYEm);
    const f32 tabsBottom = tabsTop + ctx.tabHeight;

    ImRect sidebar;
    const bool hasSidebar = FindSidebarStripColumn(&sidebar);
    const bool hasDocuments = !GetOpenDocuments().empty();

    if (!hasSidebar && !hasDocuments)
        return;

    const f32 underlineLeft = hasSidebar ? sidebar.Max.x : stripMin.x;
    const f32 tabsLeft = hasSidebar ? sidebar.Max.x + style.DockingSeparatorSize : stripMin.x;

    drawBackground(stripMin.y, tabsBottom, underlineLeft);

    if (hasSidebar)
        DrawSidebarSwitchInStrip(sidebar, tabsTop, tabsBottom);

    if (hasDocuments)
    {
        measureTabs(ctx);
        fitTabs(ctx, ImGui::GetContentRegionAvail().x - (tabsLeft - stripMin.x) - style.WindowPadding.x * 2.0f);

        ImGui::SetCursorScreenPos(ImVec2(tabsLeft + style.WindowPadding.x, tabsTop));

        drawTabs(ctx);
        drawDocumentList(ctx);
    }

    ImGui::SetCursorScreenPos(ImVec2(stripMin.x, tabsBottom + style.DockingSeparatorSize));

    applyActions(ctx);
}

void DrawDocumentTabStrip()
{
    sStrip.draw();
}
