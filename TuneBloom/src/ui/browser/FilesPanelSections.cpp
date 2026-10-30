#include <ui/browser/FilesPanelInternal.h>

#include <ui/app/Notifications.h>
#include <ui/items/ItemColumns.h>
#include <ui/widgets/ContextMenu.h>
#include <ui/widgets/RowRename.h>
#include <ui/widgets/Widgets.h>
#include <ui/Messages.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <string>

static constexpr const char* cSectionMenuId    = "##sectionmenu";
static constexpr const char* cSectionAddMenuId = "##sectionaddmenu";
static constexpr const char* cAddMenuId        = "##addmenu";
static constexpr const char* cAreaMenuId       = "##areamenu";

s32 FilesPanel::findSection(u32 id) const
{
    for (s32 i = 0; i < static_cast<s32>(mSections.size()); i++)
    {
        if (mSections[i].id == id)
            return i;
    }

    return -1;
}

s32 FilesPanel::getSectionRank(u32 id) const
{
    return findSection(id) + 1;
}

s32 FilesPanel::getSectionStart(u32 id) const
{
    const s32 rank = getSectionRank(id);
    const auto start = std::find_if(mRoots.begin(), mRoots.end(), [this, rank](const FilesRoot& root) { return getSectionRank(root.section) >= rank; });

    return static_cast<s32>(start - mRoots.begin());
}

s32 FilesPanel::getSectionEnd(u32 id) const
{
    const s32 rank = getSectionRank(id);
    const auto end = std::find_if(mRoots.begin(), mRoots.end(), [this, rank](const FilesRoot& root) { return getSectionRank(root.section) > rank; });

    return static_cast<s32>(end - mRoots.begin());
}

u32 FilesPanel::getPreviousSection(u32 id) const
{
    const s32 index = findSection(id);

    return index > 0 ? mSections[index - 1].id : 0;
}

u32 FilesPanel::getRowSection(const FilesRow& row) const
{
    if (row.kind == FilesRowKind::Section)
        return row.index >= 0 && row.index < static_cast<s32>(mSections.size()) ? mSections[row.index].id : 0;

    return row.root >= 0 && row.root < static_cast<s32>(mRoots.size()) ? mRoots[row.root].section : 0;
}

u32 FilesPanel::getTargetSection() const
{
    return getRowSection(mSelected);
}

void FilesPanel::sortRoots()
{
    for (FilesRoot& root : mRoots)
    {
        if (findSection(root.section) < 0)
            root.section = 0;
    }

    std::stable_sort(mRoots.begin(), mRoots.end(), [this](const FilesRoot& left, const FilesRoot& right) { return getSectionRank(left.section) < getSectionRank(right.section); });
}

std::string FilesPanel::makeSectionName() const
{
    std::string name = messages::files::cNewSection;

    for (u32 number = 2; std::any_of(mSections.begin(), mSections.end(), [&name](const FilesSection& section) { return section.name == name; }); number++)
        name = sead::FormatFixedSafeString<64>("%s %u", messages::files::cNewSection, number).cstr();

    return name;
}

u32 FilesPanel::addSection(s32 at)
{
    const u32 id = mNextSectionId++;
    const s32 index = at < 0 ? static_cast<s32>(mSections.size()) : std::min(at, static_cast<s32>(mSections.size()));

    mSections.insert(mSections.begin() + index, { id, makeSectionName(), false });
    sortRoots();
    onRootsChanged();
    mSelected = FilesRow { .kind = FilesRowKind::Section, .index = index };
    mScrollToSelected = true;
    mRenameRequest = id;

    invalidateRows();
    saveConfig();

    return id;
}

void FilesPanel::removeSection(u32 id)
{
    const s32 index = findSection(id);

    if (index < 0)
        return;

    const u32 previous = getPreviousSection(id);
    RemovedSection removed { mNextUndoToken++, index, mSections[index], {} };

    for (FilesRoot& root : mRoots)
    {
        if (root.section != id)
            continue;

        removed.rootPaths.push_back(root.path);
        root.section = previous;
    }

    mSections.erase(mSections.begin() + index);
    sortRoots();

    const std::string name = removed.section.name;
    const u32 token = removed.token;

    PushRemovedEntry(&mRemovedSections, std::move(removed));

    onRootsChanged();
    invalidateRows();
    saveConfig();

    notify::Add(notify::Kind::Info, messages::files::cSectionRemovedTitle, name.c_str(), { messages::common::cUndo, &UndoFilesPanelRemove, token });
}

void FilesPanel::moveSection(u32 id, s32 slot)
{
    const s32 index = findSection(id);

    if (index < 0 || slot == index || slot == index + 1)
        return;

    const FilesSection section = mSections[index];
    const s32 target = std::clamp(index < slot ? slot - 1 : slot, 0, static_cast<s32>(mSections.size()) - 1);

    mSections.erase(mSections.begin() + index);
    mSections.insert(mSections.begin() + target, section);
    sortRoots();

    onRootsChanged();
    invalidateRows();
    mSelected = FilesRow { .kind = FilesRowKind::Section, .index = target };
    mScrollToSelected = true;
    saveConfig();
}

void FilesPanel::setSectionFolded(u32 id, bool isFolded)
{
    const s32 index = findSection(id);

    if (index < 0 || mSections[index].isFolded == isFolded)
        return;

    mSections[index].isFolded = isFolded;
    invalidateRows();
    saveConfig();
}

void FilesPanel::foldAllSections(bool isFolded)
{
    for (FilesSection& section : mSections)
        section.isFolded = isFolded;

    invalidateRows();
    saveConfig();
}

void FilesPanel::drawSectionRow(FrameContext& ctx, const FilesRow& row)
{
    FilesSection& section = mSections[row.index];
    const ImGuiID tableId = ImGui::GetCurrentTable()->ID;

    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, 0);
    ImGui::GetCurrentTable()->RowBgColorCounter = -1;
    ImGui::SetNextItemAllowOverlap();

    const RowFrame frame = beginRow(ctx, row, 0.0f);

    drawSectionDragSource(section);
    ImGui::OpenPopupOnItemClick(cSectionMenuId, ImGuiPopupFlags_MouseButtonRight);

    if (frame.isDoubleClicked && !isSearching())
    {
        setSectionFolded(section.id, !section.isFolded);
        mRenameRequest = section.id;
        mIgnoredPressSection = section.id;
    }
    else if (frame.isPressed && mIgnoredPressSection == section.id)
    {
        mIgnoredPressSection = 0;
    }
    else if (frame.isPressed && isExpandable(row))
    {
        setRowOpen(row, !isRowOpen(row));
    }

    if (mRenameRequest == section.id)
    {
        BeginRowRename(&section, tableId, section.name.c_str());
        mRenameRequest = 0;
    }

    const bool isRowHovered = ImGui::IsPopupOpen(cSectionMenuId) || ImGui::IsPopupOpen(cSectionAddMenuId) || (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(frame.rowMin, frame.rowMax, false));

    PushTableRowClipRect(frame.rowMin, frame.rowMax);

    if (!isSearching())
        ImGui::GetWindowDrawList()->AddText(frame.textPos, ImGui::GetColorU32(ImGuiCol_TextDisabled), isRowOpen(row) ? ICON_LC_CHEVRON_DOWN : ICON_LC_CHEVRON_RIGHT);

    if (GetRenamedRow(tableId) == &section)
    {
        sead::FixedSafeString<256> name;
        const ImVec2 textPos(frame.textPos.x + ctx.chevronWidth, frame.rowMin.y + ImFloor((frame.rowMax.y - frame.rowMin.y - ImGui::GetFrameHeight()) * 0.5f));

        if (DrawRowRenameFieldAt(textPos, frame.rowMax.x - ImGui::GetStyle().CellPadding.x - textPos.x, &name) && !name.isEmpty())
        {
            section.name = name.cstr();
            invalidateRows();
            saveConfig();
        }
    }
    else
    {
        drawSectionLabel(ctx, section, frame, isRowHovered);
    }

    ImGui::PopClipRect();

    drawSectionMenu(section);
    drawAddMenu(cSectionAddMenuId, section.id, false);
}

void FilesPanel::drawSectionLabel(const FrameContext& ctx, const FilesSection& section, const RowFrame& frame, bool isRowHovered)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 dimColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const bool isOpen = isSearching() || !section.isFolded;
    const f32 hit = frame.rowMax.y - frame.rowMin.y;
    const f32 buttonX = frame.rowMax.x - style.CellPadding.x - hit;
    f32 right = buttonX;

    ImGui::SetCursorScreenPos(ImVec2(buttonX, frame.rowMin.y));

    if (DrawRowHoverButton("##add", ICON_LC_PLUS, isRowHovered, messages::files::cAddTip, ImVec2(hit, hit), sead::Mathf::min(GetRowButtonSize(), hit)))
        ImGui::OpenPopup(cSectionAddMenuId);

    if (!isOpen)
    {
        const sead::FixedSafeString<32> count = FormatCountText(static_cast<u32>(getSectionEnd(section.id) - getSectionStart(section.id)));

        right -= ImGui::CalcTextSize(count.cstr()).x;
        drawList->AddText(ImVec2(right, frame.textPos.y), dimColor, count.cstr());
        right -= ImFloor(ctx.em * cCountGapEm);
    }

    const f32 nameX = frame.textPos.x + ctx.chevronWidth;
    const f32 nameRight = sead::Mathf::min(nameX + ImGui::CalcTextSize(section.name.c_str()).x, right);

    ImGui::RenderTextEllipsis(drawList, ImVec2(nameX, frame.textPos.y), ImVec2(nameRight, frame.textPos.y + ctx.em), nameRight, nameRight, section.name.c_str(), nullptr, nullptr);

    const f32 lineLeft = nameRight + style.ItemSpacing.x;
    const f32 lineY = ImFloor((frame.rowMin.y + frame.rowMax.y) * 0.5f);

    if (lineLeft < right)
        drawList->AddLine(ImVec2(lineLeft, lineY), ImVec2(right, lineY), ImGui::GetColorU32(ImGuiCol_Separator), style.SeparatorTextBorderSize);
}

void FilesPanel::drawSectionDragSource(const FilesSection& section)
{
    if (isSearching() || !ImGui::BeginDragDropSource())
        return;

    ImGui::SetDragDropPayload(cSectionPayloadType, &section.id, sizeof(section.id));
    DrawDragLabel(ICON_LC_LIST_TREE, ImGui::GetColorU32(GetItemValueColor()), section.name.c_str(), mFrame.drop.isHovered ? messages::common::cMove : nullptr);
    ImGui::EndDragDropSource();
}

void FilesPanel::drawSectionMenu(const FilesSection& section)
{
    if (!BeginContextMenu(cSectionMenuId))
        return;

    if (ContextMenuItem(ICON_LC_TEXT_CURSOR_INPUT, messages::common::cRename))
        mRenameRequest = section.id;

    if (ContextMenuItem(section.isFolded ? ICON_LC_CHEVRON_DOWN : ICON_LC_CHEVRON_RIGHT, section.isFolded ? messages::files::cUnfold : messages::files::cFold, !isSearching()))
        setSectionFolded(section.id, !section.isFolded);

    ContextMenuSeparator();

    if (ContextMenuItem(ICON_LC_FOLDER_PLUS, messages::files::cAddFolder))
        requestAction(RootAction::ChooseFolder, -1, std::string(), section.id);

    if (ContextMenuItem(ICON_LC_FILE_PLUS, messages::files::cAddFile))
        requestAction(RootAction::ChooseFiles, -1, std::string(), section.id);

    if (ContextMenuItem(ICON_LC_LIST_PLUS, messages::files::cNewSection))
        requestAction(RootAction::NewSection, -1, std::string(), section.id);

    ContextMenuSeparator();

    if (ContextMenuItem(ICON_LC_X, messages::files::cRemoveSection))
        requestAction(RootAction::RemoveSection, -1, std::string(), section.id);

    EndContextMenu();
}

void FilesPanel::drawAddMenu(const char* id, u32 section, bool showsTarget)
{
    if (!BeginContextMenu(id))
        return;

    drawAddMenuItems(section, showsTarget, false);
    EndContextMenu();
}

void FilesPanel::drawAreaMenu()
{
    if (!BeginWindowContextMenu(cAreaMenuId))
        return;

    if (BeginContextSubMenu(ICON_LC_PLUS, messages::common::cAdd))
    {
        drawAddMenuItems(getTargetSection(), true, true);
        EndContextSubMenu();
    }

    EndContextMenu();
}

void FilesPanel::drawAddMenuItems(u32 section, bool showsTarget, bool isSubMenu)
{
    const s32 index = findSection(section);
    const char* target = nullptr;

    if (showsTarget && !mSections.empty())
        target = index >= 0 ? mSections[index].name.c_str() : messages::files::cNoSection;

    if (ContextMenuHintItem(ICON_LC_FOLDER_PLUS, isSubMenu ? messages::common::cFolder : messages::files::cAddFolder, target))
        requestAction(RootAction::ChooseFolder, -1, std::string(), section);

    if (ContextMenuHintItem(ICON_LC_FILE_PLUS, isSubMenu ? messages::common::cFile : messages::files::cAddFile, target))
        requestAction(RootAction::ChooseFiles, -1, std::string(), section);

    if (showsTarget)
    {
        ContextMenuSeparator();

        if (ContextMenuItem(ICON_LC_LIST_PLUS, isSubMenu ? messages::files::cSection : messages::files::cNewSection))
            requestAction(RootAction::NewSection, -1);
    }
}

void FilesPanel::drawAddButton()
{
    const f32 size = ImGui::GetFrameHeight();

    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));

    if (ImGui::Button(ICON_LC_PLUS "##add", ImVec2(size, size)))
        ImGui::OpenPopup(cAddMenuId);

    ImGui::PopStyleColor(3);
    ImGui::SetItemTooltip("%s", messages::files::cAddTip);

    drawAddMenu(cAddMenuId, getTargetSection(), true);
}

void FilesPanel::drawMoveToSectionMenu(s32 root)
{
    if (!BeginContextSubMenu(ICON_LC_LIST_TREE, messages::files::cMoveToSection))
        return;

    const u32 current = mRoots[root].section;

    if (ContextMenuCheckItem(messages::files::cNoSection, current == 0) && current != 0)
        requestAction(RootAction::MoveToSection, root, std::string(), 0);

    for (s32 i = 0; i < static_cast<s32>(mSections.size()); i++)
    {
        const FilesSection& section = mSections[i];

        ImGui::PushID(i);

        if (ContextMenuCheckItem(section.name.c_str(), current == section.id) && current != section.id)
            requestAction(RootAction::MoveToSection, root, std::string(), section.id);

        ImGui::PopID();
    }

    ContextMenuSeparator();

    if (ContextMenuItem(ICON_LC_LIST_PLUS, messages::files::cNewSection))
        requestAction(RootAction::MoveToNewSection, root);

    EndContextSubMenu();
}
