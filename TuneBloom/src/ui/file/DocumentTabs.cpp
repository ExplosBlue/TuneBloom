#include <ui/file/DocumentTabs.h>

#include <ui/widgets/ContextMenu.h>
#include <ui/file/Document.h>
#include <ui/file/DocumentMgr.h>
#include <ui/file/DocumentState.h>
#include <ui/file/FileFlow.h>
#include <ui/browser/FilesPanel.h>
#include <ui/Messages.h>
#include <ui/file/PathText.h>
#include <ui/file/SystemShell.h>
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
static constexpr f32 cTooltipFolderMinEm = 22.0f;

static constexpr const char* cDocumentListId = "##DocumentList";

namespace
{

struct TabLayout
{
    Document* document = nullptr;
    std::string name;
    bool isActive = false;
    bool hasCloseSlot = true;
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

        Document* activate = nullptr;
        Document* close = nullptr;
        Document* closeOthers = nullptr;
        Document* pin = nullptr;
    };

    void measureTabs(FrameContext& ctx) const;
    void fitTabs(FrameContext& ctx, f32 available) const;
    f32 shareWidth(FrameContext& ctx, f32 available) const;
    void placeTabs(FrameContext& ctx) const;
    void scrollToActive(const FrameContext& ctx);
    void scrollWithWheel(const FrameContext& ctx) const;
    void drawBackground(f32 top, f32 bottom) const;
    void drawTabs(FrameContext& ctx);
    void drawTab(FrameContext& ctx, const TabLayout& tab);
    void drawTabLabel(const FrameContext& ctx, const TabLayout& tab, const ImRect& bb, bool showsClose) const;
    void drawTabTooltip(const TabLayout& tab) const;
    void drawTabMenu(FrameContext& ctx, const TabLayout& tab) const;
    void drawDocumentList(FrameContext& ctx) const;
    void drawDocumentMenu(FrameContext& ctx) const;
    void applyActions(const FrameContext& ctx) const;

    const Document* mScrolledTo = nullptr;
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

void DocumentTabStrip::measureTabs(FrameContext& ctx) const
{
    const f32 iconWidth = ImGui::CalcTextSize(ICON_LC_PACKAGE).x;
    const f32 minWidth = ctx.em * cTabMinWidthEm;
    const f32 maxWidth = ctx.em * cTabMaxWidthEm;

    for (Document* document : GetOpenDocuments())
    {
        TabLayout tab;
        tab.document = document;
        tab.name = document->displayName();
        tab.isActive = document == ActiveDocument();

        const f32 labelWidth = ctx.padX * 2.0f + iconWidth + ctx.iconGap + ImGui::CalcTextSize(tab.name.c_str()).x;
        const bool keepsCloseSlot = tab.isActive || document->state().unsaved;

        tab.fullWidth = std::clamp(labelWidth + ctx.closeGap + ctx.closeSize, minWidth, maxWidth);
        tab.compactWidth = keepsCloseSlot ? tab.fullWidth : std::clamp(labelWidth, minWidth, maxWidth);
        tab.width = tab.fullWidth;

        ctx.tabs.push_back(std::move(tab));
    }
}

f32 DocumentTabStrip::shareWidth(FrameContext& ctx, f32 available) const
{
    const s32 tabNum = static_cast<s32>(ctx.tabs.size());
    const f32 spacingWidth = ctx.spacing * static_cast<f32>(tabNum - 1);

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

        tab.width = sead::Mathf::max(sead::Mathf::min(tab.width, activeMax), ctx.em * cTabMinWidthEm);
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
    f32 fullTotal = ctx.spacing * static_cast<f32>(ctx.tabs.size() - 1);

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

    for (TabLayout& tab : ctx.tabs)
    {
        tab.left = left;
        left += tab.width + ctx.spacing;
    }

    ctx.contentWidth = left - ctx.spacing;
}

void DocumentTabStrip::scrollToActive(const FrameContext& ctx)
{
    const Document* active = ActiveDocument();

    if (active == mScrolledTo)
        return;

    mScrolledTo = active;

    for (const TabLayout& tab : ctx.tabs)
    {
        if (!tab.isActive)
            continue;

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
    const DocumentState& state = tab.document->state();
    const f32 textY = bb.Min.y + ImFloor((bb.GetHeight() - ImGui::GetFontSize()) * 0.5f);

    const char* icon = GetFileKindIcon(tab.document->fileKind());
    const f32 iconX = bb.Min.x + ctx.padX;
    const f32 nameX = iconX + ImGui::CalcTextSize(icon).x + ctx.iconGap;
    const f32 nameRight = bb.Max.x - ctx.padX - (tab.hasCloseSlot ? ctx.closeSize + ctx.closeGap : 0.0f);

    drawList->AddText(ImVec2(iconX, textY), ImGui::GetColorU32(GetFileKindColor(tab.document->fileKind())), icon);

    const ImGuiCol nameColor = tab.document->isLoaded() ? ImGuiCol_Text : ImGuiCol_TextDisabled;

    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(nameColor));

    if (state.isPreview)
        DrawObliqueText(drawList, ImVec2(nameX, textY), nameRight, tab.name.c_str());
    else
        ImGui::RenderTextEllipsis(drawList, ImVec2(nameX, textY), ImVec2(nameRight, textY + ImGui::GetFontSize()), nameRight, nameRight, tab.name.c_str(), nullptr, nullptr);

    ImGui::PopStyleColor();

    if (state.unsaved && !showsClose)
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

    const std::string folder = pathtext::Folder(tab.document->filePath().cstr());
    if (!folder.empty())
        ImGui::TextDisabled("%s", pathtext::FitToWidth(folder, ImMax(width, em * cTooltipFolderMinEm)).c_str());

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

    const sead::SafeString& path = tab.document->filePath();

    if (tab.document->state().isPreview && ContextMenuItem(ICON_LC_PIN, messages::documents::cKeepOpen))
        ctx.pin = tab.document;

    if (ContextMenuItem(ICON_LC_FOLDER_SEARCH, messages::recent::cShowInFileManager, !path.isEmpty()))
        ShowInFileManager(path.cstr());

    if (ContextMenuItem(ICON_LC_COPY, messages::mainmenu::cCopyPath, !path.isEmpty()))
        ImGui::SetClipboardText(path.cstr());

    ContextMenuSeparator();

    if (ContextMenuItem(ICON_LC_X, messages::common::cClose, !ctx.isLocked, messages::documents::cSwitchLocked))
        ctx.close = tab.document;

    if (ContextMenuItem(ICON_LC_COPY_X, messages::documents::cCloseOthers, !ctx.isLocked && ctx.tabs.size() > 1, messages::documents::cSwitchLocked))
        ctx.closeOthers = tab.document;

    EndContextMenu();
}

void DocumentTabStrip::drawTab(FrameContext& ctx, const TabLayout& tab)
{
    ImGui::PushID(static_cast<s32>(tab.document->state().serial));

    const ImVec2 origin = ImGui::GetWindowPos() - ImVec2(ImGui::GetScrollX(), 0.0f);
    const ImRect bb(origin + ImVec2(tab.left, 0.0f), origin + ImVec2(tab.left + tab.width, ctx.tabHeight));

    ImGui::SetCursorScreenPos(bb.Min);
    ImGui::SetNextItemAllowOverlap();

    const ImGuiButtonFlags buttonFlags = static_cast<ImGuiButtonFlags>(ImGuiButtonFlags_PressedOnClick) | ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle;
    const bool clicked = ImGui::InvisibleButton("##Tab", bb.GetSize(), buttonFlags);
    const bool isHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseHoveringRect(bb.Min, bb.Max);
    const ImGuiID tabId = ImGui::GetItemID();

    if (clicked && !ctx.isLocked)
    {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
            ctx.close = tab.document;
        else
            ctx.activate = tab.document;
    }

    if (isHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        ctx.pin = tab.document;

    drawTabTooltip(tab);
    drawTabMenu(ctx, tab);

    const ImU32 idleColor = ImGui::GetColorU32(tab.isActive ? ImGuiCol_TabActive : ImGuiCol_Tab);
    const ImU32 hoverColor = ImGui::GetColorU32(tab.isActive ? ImGuiCol_TabActive : ImGuiCol_TabHovered);

    ImGui::TabItemBackground(ImGui::GetWindowDrawList(), bb, ImGuiTabItemFlags_None, ImGui::GetHoverColorU32(tabId, isHovered, idleColor, hoverColor));

    const bool showsClose = tab.hasCloseSlot && (isHovered || tab.isActive);

    drawTabLabel(ctx, tab, bb, showsClose);

    if (showsClose)
    {
        const ImVec2 closePos(bb.Max.x - ctx.padX - ctx.closeSize, bb.GetCenter().y - ctx.closeSize * 0.5f);

        if (ImGui::CloseButton(ImGui::GetID("##Close"), closePos) && !ctx.isLocked)
            ctx.close = tab.document;
    }

    ImGui::PopID();
}

void DocumentTabStrip::drawBackground(f32 top, f32 bottom) const
{
    const ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImDrawList* drawList = window->DrawList;
    const f32 left = window->Pos.x;
    const f32 right = left + window->Size.x;
    const f32 underlineTop = bottom - ImGui::GetStyle().TabBarBorderSize;

    drawList->AddRectFilled(ImVec2(left, top), ImVec2(right, bottom), ImGui::GetColorU32(ImGuiCol_MenuBarBg));
    drawList->AddRectFilled(ImVec2(left, underlineTop), ImVec2(right, bottom), ImGui::GetColorU32(ImGuiCol_TabActive));
}

void DocumentTabStrip::drawTabs(FrameContext& ctx)
{
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav;

    if (ImGui::BeginChild("##DocumentTabs", ImVec2(ctx.viewWidth, ctx.tabHeight), ImGuiChildFlags_None, flags))
    {
        for (const TabLayout& tab : ctx.tabs)
            drawTab(ctx, tab);

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
        ImGui::PushID(static_cast<s32>(tab.document->state().serial));

        if (ContextMenuCheckItem(tab.name.c_str(), tab.isActive, !ctx.isLocked))
            ctx.activate = tab.document;

        ImGui::PopID();
    }

    EndContextMenu();
}

void DocumentTabStrip::applyActions(const FrameContext& ctx) const
{
    if (ctx.pin)
        PinDocument(ctx.pin);

    if (ctx.activate && ctx.activate != ActiveDocument())
        RequestDocumentActivation(ctx.activate);

    if (ctx.close)
        RequestCloseDocument(ctx.close);

    if (!ctx.closeOthers)
        return;

    RequestDocumentActivation(ctx.closeOthers);

    const std::vector<Document*> documents = GetOpenDocuments();

    for (Document* document : documents)
    {
        if (document != ctx.closeOthers && !document->state().unsaved)
            CloseDocument(document);
    }
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

    measureTabs(ctx);
    fitTabs(ctx, ImGui::GetContentRegionAvail().x - style.WindowPadding.x * 2.0f);
    drawBackground(stripMin.y, tabsBottom);

    ImGui::SetCursorScreenPos(ImVec2(stripMin.x + style.WindowPadding.x, tabsTop));

    drawTabs(ctx);
    drawDocumentList(ctx);

    ImGui::SetCursorScreenPos(ImVec2(stripMin.x, tabsBottom + style.DockingSeparatorSize));

    applyActions(ctx);
}

void DrawDocumentTabStrip()
{
    if (!GetOpenDocuments().empty())
        sStrip.draw();
}
