#include <ui/browser/FilesPanelInternal.h>

#include <ui/browser/FilesPanel.h>
#include <ui/items/ItemColumns.h>
#include <ui/widgets/Widgets.h>
#include <ui/Messages.h>

#include <bfsar/io/FileIo.h>
#include <bfsar/io/PackFile.h>
#include <libs/IconsLucide.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <string>
#include <vector>

static constexpr const char* cDropTargetId = "##pinDrop";

std::string FilesPanel::getRowPath(const FilesRow& row) const
{
    if (const BrowserFile* file = getRowFile(row))
        return file->path;

    if (row.kind == FilesRowKind::Root)
        return mRoots[row.root].path;

    if (row.kind == FilesRowKind::Folder && findPackAncestor(row.root, row.index) < 0)
        return getFolderPath(row.root, row.index);

    return std::string();
}

FileDrag FilesPanel::makeRowDrag(const FilesRow& row, const std::string& path) const
{
    FileDrag drag;
    drag.origin = FileDragOrigin::Browser;

    const BrowserFile* file = getRowFile(row);

    if (file && mSelectedSorted.size() > 1 && isPathSelected(file->path))
    {
        for (const BrowserFile* selected : collectSelectedFiles())
            drag.paths.push_back(selected->path);

        drag.name = sead::FormatFixedSafeString<64>(messages::common::cFileCountFormat, FormatCountText(drag.paths.size()).cstr()).cstr();
        drag.icon = ICON_LC_FILES;
        drag.iconColor = ImGui::GetColorU32(GetItemValueColor());

        return drag;
    }

    drag.paths.push_back(path);
    drag.name = getRowName(row);

    if (file)
    {
        drag.icon = GetFileKindIcon(file->probe.kind);
        drag.iconColor = ImGui::GetColorU32(GetFileKindColor(file->probe.kind));
    }
    else
    {
        drag.icon = ICON_LC_FOLDER_CLOSED;
        drag.iconColor = ImGui::GetColorU32(GetItemValueColor());
    }

    return drag;
}

void FilesPanel::drawRowDragSource(const FilesRow& row)
{
    const std::string path = getRowPath(row);

    if (path.empty())
        return;

    FileDragSource([&]() { return makeRowDrag(row, path); });
}

static std::vector<std::string> CollectPinnablePaths(const FileDrag& drag)
{
    std::vector<std::string> paths;

    for (const std::string& path : drag.paths)
    {
        if (packfile::IsPackPath(path))
            continue;

        std::string canonical = fileio::CanonicalPath(path.c_str());

        if (!canonical.empty())
            paths.push_back(std::move(canonical));
    }

    return paths;
}

void FilesPanel::beginDrop(FrameContext& ctx)
{
    ctx.drop = FilesDropFrame();

    const bool isWindowHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    if (const ImGuiPayload* payload = ImGui::GetDragDropPayload(); payload && payload->IsDataType(cSectionPayloadType))
    {
        ctx.drop.draggedSection = *static_cast<const u32*>(payload->Data);
        ctx.drop.isHovered = isWindowHovered && !isSearching();
        return;
    }

    const FileDrag* drag = GetFileDrag();

    if (!drag)
    {
        mDropSerial = 0;
        mDropPaths.clear();
        return;
    }

    if (drag->serial != mDropSerial)
    {
        mDropSerial = drag->serial;
        mDropPaths = CollectPinnablePaths(*drag);
    }

    ctx.drop.isHovered = !mDropPaths.empty() && isWindowHovered;
}

void FilesPanel::recordDropRow(FrameContext& ctx, const FilesRow& row)
{
    if (!ctx.drop.isHovered)
        return;

    const f32 top = ImGui::GetCurrentTable()->RowPosY1;
    const f32 rowHeight = ImGui::GetFrameHeight() + ImGui::GetStyle().CellPadding.y * 2.0f;

    ctx.drop.endY = top + rowHeight;

    if (row.kind == FilesRowKind::Section)
    {
        ctx.drop.rows.push_back({ .section = row.index, .top = top, .split = top + rowHeight * 0.5f });
        return;
    }

    if (row.kind != FilesRowKind::Root)
        return;

    const bool isOpen = isExpandable(row) && isRowOpen(row);

    ctx.drop.rows.push_back({ .root = row.root, .top = top, .split = top + (isOpen ? rowHeight : rowHeight * 0.5f) });
}

bool FilesPanel::isRootPath(const std::string& path) const
{
    return findRoot(path) >= 0;
}

bool FilesPanel::isDropMove() const
{
    return std::all_of(mDropPaths.begin(), mDropPaths.end(), [this](const std::string& path) { return isRootPath(path); });
}

bool FilesPanel::isDropNoMove(const FilesDropFrame& drop) const
{
    if (drop.draggedSection != 0)
    {
        const s32 index = findSection(drop.draggedSection);

        return drop.sectionSlot == index || drop.sectionSlot == index + 1;
    }

    if (mDropPaths.size() != 1 || !isDropMove())
        return false;

    const s32 root = findRoot(mDropPaths.front());

    return mRoots[root].section == drop.section && (drop.slot == root || drop.slot == root + 1);
}

void FilesPanel::findRootDrop(FilesDropFrame& drop) const
{
    const f32 mouseY = ImGui::GetMousePos().y;

    drop.slot = static_cast<s32>(mRoots.size());
    drop.section = mSections.empty() ? 0 : mSections.back().id;
    drop.lineY = -1.0f;

    for (size_t i = 0; i < drop.rows.size(); i++)
    {
        const FilesDropRow& row = drop.rows[i];
        const bool isLast = i + 1 == drop.rows.size();
        const f32 nextTop = isLast ? drop.endY : drop.rows[i + 1].top;

        if (!isLast && mouseY >= nextTop)
            continue;

        if (row.section >= 0)
        {
            const FilesSection& section = mSections[row.section];
            const bool isAbove = mouseY < row.split;

            drop.section = isAbove ? getPreviousSection(section.id) : section.id;
            drop.slot = isAbove || !section.isFolded ? getSectionStart(section.id) : getSectionEnd(section.id);
            drop.lineY = isAbove ? row.top : row.split * 2.0f - row.top;

            if (!isAbove && section.isFolded)
            {
                drop.lineY = -1.0f;
                drop.headTop = row.top;
            }

            return;
        }

        drop.section = mRoots[row.root].section;
        drop.slot = mouseY < row.split ? row.root : row.root + 1;
        drop.lineY = mouseY < row.split ? row.top : nextTop;
        return;
    }
}

void FilesPanel::findSectionDrop(FilesDropFrame& drop) const
{
    const f32 mouseY = ImGui::GetMousePos().y;

    drop.sectionSlot = static_cast<s32>(mSections.size());
    drop.lineY = drop.endY;

    for (const FilesDropRow& row : drop.rows)
    {
        if (row.section >= 0 && mouseY < row.split)
        {
            drop.sectionSlot = row.section;
            drop.lineY = row.top;
            return;
        }
    }
}

void FilesPanel::drawDropLine(FrameContext& ctx)
{
    FilesDropFrame& drop = ctx.drop;

    if (!drop.isHovered)
        return;

    if (drop.draggedSection != 0)
        findSectionDrop(drop);
    else
        findRootDrop(drop);

    const ImGuiTable* table = ImGui::GetCurrentTable();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    ImGui::PushClipRect(table->InnerClipRect.Min, table->InnerClipRect.Max, false);

    if (drop.headTop >= 0.0f)
    {
        const f32 rowHeight = ImGui::GetFrameHeight() + ImGui::GetStyle().CellPadding.y * 2.0f;
        drawList->AddRect(ImVec2(table->WorkRect.Min.x, drop.headTop), ImVec2(table->WorkRect.Max.x, drop.headTop + rowHeight), ImGui::GetColorU32(ImGuiCol_CheckMark), ImGui::GetStyle().FrameRounding);
    }
    else if (drop.lineY >= 0.0f && !isDropNoMove(drop))
    {
        DrawInsertLine(drawList, table->WorkRect.Min.x, table->WorkRect.Max.x, drop.lineY);
    }

    ImGui::PopClipRect();
}

void FilesPanel::drawDropTarget(const FrameContext& ctx)
{
    const bool isSectionDrag = ctx.drop.draggedSection != 0;

    if ((!isSectionDrag && mDropPaths.empty()) || !ImGui::BeginDragDropTargetCustom(ImGui::GetCurrentWindow()->Rect(), ImGui::GetID(cDropTargetId)))
        return;

    if (isSectionDrag)
    {
        if (ImGui::AcceptDragDropPayload(cSectionPayloadType))
        {
            mPendingSectionMove = ctx.drop.draggedSection;
            mPendingSectionSlot = ctx.drop.sectionSlot;
        }
    }
    else if (AcceptFileDrop(isDropMove() ? messages::common::cMove : messages::common::cAdd))
    {
        mPendingDrop = { mDropPaths, ctx.drop.slot, ctx.drop.section };
    }

    ImGui::EndDragDropTarget();
}

void FilesPanel::applyPendingDrop()
{
    if (mPendingSectionMove != 0)
    {
        moveSection(mPendingSectionMove, mPendingSectionSlot);
        mPendingSectionMove = 0;
    }

    if (mPendingDrop.paths.empty())
        return;

    const PendingDrop drop = std::move(mPendingDrop);
    mPendingDrop = PendingDrop();

    addPaths(drop.paths, drop.slot, drop.section);
}
