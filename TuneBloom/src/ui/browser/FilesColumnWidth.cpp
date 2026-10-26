#include <ui/browser/FilesColumnWidth.h>

#include <core/Json.h>
#include <ui/widgets/DockNode.h>

static constexpr const char* cFilesWidthKey   = "filesWidth";
static constexpr const char* cProjectWidthKey = "projectWidth";

static constexpr f32 cDefaultFilesWidthEm = 21.0f;
static constexpr f32 cMinColumnEm         = 10.0f;
static constexpr f32 cMinNeighbourEm      = 30.0f;
static constexpr s32 cPendingFrames       = 3;

static const ImGuiDockNode* FindNeighbourNode(const ImGuiDockNode* column)
{
    const ImGuiDockNode* parent = column->ParentNode;
    const s32 side = parent->ChildNodes[0] == column ? 0 : 1;
    const ImGuiDockNode* neighbour = parent->ChildNodes[1 - side];

    while (neighbour->IsSplitNode() && neighbour->SplitAxis == ImGuiAxis_X)
        neighbour = neighbour->ChildNodes[side];

    return neighbour;
}

static f32 ClampColumnWidth(const ImGuiDockNode* column, f32 width)
{
    const f32 em = ImGui::GetFontSize();
    const f32 growthRoom = FindNeighbourNode(column)->Size.x - em * cMinNeighbourEm;
    const f32 maxWidth = column->Size.x + ImMax(growthRoom, 0.0f);

    return ImMax(ImMin(width, maxWidth), ImMin(em * cMinColumnEm, column->Size.x));
}

static ImGuiID GetComingTabId(const ImGuiDockNode* node)
{
    const ImGuiTabBar* tabBar = node->TabBar;

    return tabBar && tabBar->NextSelectedTabId != 0 ? tabBar->NextSelectedTabId : node->SelectedTabId;
}

void FilesColumnWidth::load(const json::Value& section)
{
    mFilesWidthEm = section.getFloat(cFilesWidthKey, cDefaultFilesWidthEm);
    mProjectWidthEm = section.getFloat(cProjectWidthKey, 0.0f);
}

void FilesColumnWidth::save(json::Value* section) const
{
    section->setFloat(cFilesWidthKey, mFilesWidthEm);

    if (mProjectWidthEm > 0.0f)
        section->setFloat(cProjectWidthKey, mProjectWidthEm);
}

f32& FilesColumnWidth::slotFor(bool isFiles)
{
    return isFiles ? mFilesWidthEm : mProjectWidthEm;
}

bool FilesColumnWidth::update(const ImGuiWindow* filesWindow)
{
    ImGuiDockNode* node = filesWindow ? filesWindow->DockNode : nullptr;
    ImGuiDockNode* column = FindDockColumnNode(node);

    if (!column)
    {
        mWasFilesActive.reset();
        return false;
    }

    if (column->Size.x <= 0.0f || FindNeighbourNode(column)->Size.x <= 0.0f)
        return false;

    const f32 em = ImGui::GetFontSize();
    const f32 width = column->Size.x;
    const f32 lastWidth = mLastWidth;
    const bool isFilesActive = GetComingTabId(node) == filesWindow->TabId;
    const bool isSettling = mPendingFrames > 0;
    const bool isPending = isSettling && ImAbs(width - mPendingWidth) >= 1.0f;

    mLastWidth = width;
    mPendingFrames = isPending ? mPendingFrames - 1 : 0;

    if (mWasFilesActive != isFilesActive)
    {
        f32& outgoing = slotFor(mWasFilesActive.has_value() ? *mWasFilesActive : !isFilesActive);

        if (outgoing <= 0.0f)
            outgoing = width / em;

        mWasFilesActive = isFilesActive;

        const f32 targetEm = slotFor(isFilesActive);

        if (targetEm <= 0.0f)
            return true;

        mPendingWidth = ClampColumnWidth(column, targetEm * em);
        mPendingFrames = cPendingFrames;
        SetDockNodeWidth(column, mPendingWidth);

        return true;
    }

    const bool isDragged = !isSettling && ImGui::IsMouseDown(ImGuiMouseButton_Left) && ImAbs(width - lastWidth) >= 1.0f;

    if (!isDragged)
        return false;

    slotFor(isFilesActive) = width / em;

    return true;
}
