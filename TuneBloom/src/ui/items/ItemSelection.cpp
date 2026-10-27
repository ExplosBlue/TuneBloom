#include <ui/items/ItemListInternal.h>

#include <ui/file/Document.h>
#include <ui/file/DocumentState.h>
#include <ui/app/PopupMgr.h>
#include <ui/widgets/RowRename.h>
#include <ui/widgets/RowSelection.h>
#include <ui/app/Shortcuts.h>
#include <ui/app/Workspace.h>

#include <algorithm>

static constexpr s32 cItemPageRows = 20;

bool IsItemMultiSelected(const Item* item)
{
    return item && std::find(sMultiSelectedItems.begin(), sMultiSelectedItems.end(), item) != sMultiSelectedItems.end();
}

void SetSelectionInSubWindow(bool isSubWindow)
{
    sSelectedItemIsSubWindow = isSubWindow;

    if (!isSubWindow)
        sSubSelectedItem = nullptr;
}

void PruneFilteredSelection(const std::vector<Item*>& listItems, ItemFilterCallback filterCallback)
{
    if (!filterCallback || sMultiSelectedItems.empty())
        return;

    std::vector<Item*>& selection = sMultiSelectedItems;

    for (Item* item : listItems)
    {
        if (!item || filterCallback(item))
            continue;

        auto it = std::find(selection.begin(), selection.end(), item);
        if (it != selection.end())
            selection.erase(it);

        if (sMultiSelectAnchor == item)
            sMultiSelectAnchor = nullptr;
    }
}

void HandleItemListKeys(ItemListContext& ctx)
{
    if (!ctx.tableOpen || !ctx.listFocused)
        return;

    Item*& selectedItem = *ctx.selection;

    const s32 current = static_cast<s32>(std::find(ctx.rows.begin(), ctx.rows.end(), selectedItem) - ctx.rows.begin());
    s32 target = current < ctx.rowNum ? current : -1;
    bool move = true;

    if (shortcuts::Pressed(shortcuts::Action::SelectPrevious))
        target -= 1;
    else if (shortcuts::Pressed(shortcuts::Action::SelectNext))
        target += 1;
    else if (shortcuts::Pressed(shortcuts::Action::SelectPageUp))
        target -= cItemPageRows;
    else if (shortcuts::Pressed(shortcuts::Action::SelectPageDown))
        target += cItemPageRows;
    else if (shortcuts::Pressed(shortcuts::Action::SelectFirst))
        target = 0;
    else if (shortcuts::Pressed(shortcuts::Action::SelectLast))
        target = ctx.rowNum - 1;
    else
        move = false;

    if (move)
    {
        sMultiSelectedItems.clear();
        sMultiSelectAnchor = selectedItem;

        target = sead::MathCalcCommon<s32>::clamp2(0, target, ctx.rowNum - 1);

        if (ctx.rowNum > 0 && ctx.rows[target] != selectedItem)
        {
            selectedItem = ctx.rows[target];
            ScrollItemListTo(selectedItem);

            if (sSubSelectedItem && sSubSelectedItem->getItemType() != selectedItem->getItemType())
            {
                sSubSelectedItem = nullptr;
                sSelectedItemIsSubWindow = false;
            }
        }
    }

    if (shortcuts::Pressed(shortcuts::Action::ScrollToSelection))
        ScrollItemListTo(selectedItem);

    if (shortcuts::Pressed(shortcuts::Action::SelectAll))
    {
        sMultiSelectedItems = ctx.rows;

        if (!sMultiSelectedItems.empty())
        {
            selectedItem = sMultiSelectedItems.back();
            sMultiSelectAnchor = sMultiSelectedItems.front();
        }
        ScrollItemListTo(selectedItem);
    }

    if (shortcuts::Pressed(shortcuts::Action::DuplicateSelection) && current < ctx.rowNum && CanDuplicateSelection(selectedItem))
        RequestSelectionDuplicate(selectedItem);

    if (shortcuts::Pressed(shortcuts::Action::DeleteSelection) && ctx.removable && sSubSelectedItem == nullptr && sSelectedItemIsSubWindow == false)
    {
        if (!sMultiSelectedItems.empty())
            RequestItemsDelete(sMultiSelectedItems);
        else if (current < ctx.rowNum)
            RequestItemDelete(selectedItem);
    }

    if (ctx.deselectable && shortcuts::Pressed(shortcuts::Action::ClearSelection))
        ClearItemListSelection(ctx);
}

void ClearItemListSelection(ItemListContext& ctx)
{
    *ctx.selection = nullptr;
    sMultiSelectedItems.clear();
    sMultiSelectAnchor = nullptr;
}

void SelectItemListRow(ItemListContext& ctx, Item* item)
{
    Item*& selectedItem = *ctx.selection;

    if (!sMultiSelectedItems.empty() && sMultiSelectedItems.front()->list() != &ctx.list)
        sMultiSelectedItems.clear();

    const RowSelection<Item*> selection { &sMultiSelectedItems, &sMultiSelectAnchor, &selectedItem };
    const bool canAddFocus = selectedItem && selectedItem->list() == &ctx.list;

    ClickRowSelection(selection, ctx.rows, item, shortcuts::Held(shortcuts::Modifier::ToggleSelection), shortcuts::Held(shortcuts::Modifier::ExtendSelection), canAddFocus);

    FocusPropertiesWindow();
    SetSelectionInSubWindow(ctx.isSubWindow);
}

void ForgetRemovedItems(const std::vector<Item*>& removed)
{
    if (removed.empty())
        return;

    auto isRemoved = [&removed](Item* item)
    {
        return item && std::find(removed.begin(), removed.end(), item) != removed.end();
    };

    for (SelectionState& selection : ActiveDocumentState().selections)
    {
        if (isRemoved(selection.selectedItem))
            selection.selectedItem = nullptr;

        if (isRemoved(selection.subSelectedItem))
            selection.subSelectedItem = nullptr;

        if (isRemoved(selection.multiSelectAnchor))
            selection.multiSelectAnchor = nullptr;

        std::vector<Item*>& multiSelected = selection.multiSelectedItems;
        multiSelected.erase(std::remove_if(multiSelected.begin(), multiSelected.end(), isRemoved), multiSelected.end());
    }

    if (isRemoved(GetItemListScrollTarget()))
        ScrollItemListTo(nullptr);

    ForgetItemListRequests(removed);

    for (Item* item : removed)
    {
        EndRowRenameOf(item);
        PopupMgr::instance()->forgetItem(item);
    }
}

void SelectSubItem(Item* item)
{
    SEAD_ASSERT(item);

    ScrollItemListTo(item);
    sSubSelectedItem = item;
    sSelectedItemIsSubWindow = true;
    sMultiSelectedItems.clear();
}

void SelectItem(Item* item)
{
    SEAD_ASSERT(item);

    ScrollItemListTo(item);

    UIType tab = UIType::ProjectInfo;
    switch (item->getItemType())
    {
        case Item::ItemType::Sound:
            tab = UIType::AllSounds;
            break;

        case Item::ItemType::SoundSet:
            switch (GetUITab())
            {
                case UIType::WaveSoundSets:
                case UIType::SequenceSoundSets:
                    tab = GetUITab();
                    break;
                default:
                    tab = UIType::AllSoundSets;
                    break;
            }
            break;

        case Item::ItemType::Bank:
            tab = UIType::Banks;
            break;

        case Item::ItemType::WaveArchive:
            tab = UIType::WaveArchives;
            break;

        case Item::ItemType::Group:
            tab = UIType::Groups;
            break;

        case Item::ItemType::Player:
            tab = UIType::Players;
            break;

        case Item::ItemType::WaveFile:
            tab = UIType::WaveFiles;
            break;

        case Item::ItemType::SequenceFile:
            tab = UIType::SequenceFiles;
            break;

        case Item::ItemType::BankFile:
            tab = UIType::BankFiles;
            break;

        case Item::ItemType::AudioAsset:
            tab = UIType::AudioAssets;
            break;

        default:
            break;
    }

    if (const Document* document = ActiveDocument(); document && !document->hasProjectList())
        tab = document->defaultTab();

    SelectionState& selection = GetTabSelection(tab);
    selection.selectedItem = item;
    selection.subSelectedItem = nullptr;
    selection.selectedItemIsSubWindow = false;

    SetUITab(tab);

    FocusInfoWindow();
    FocusPropertiesWindow();
}
