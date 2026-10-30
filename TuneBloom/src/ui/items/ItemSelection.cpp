#include <ui/items/ItemListInternal.h>

#include <bfsar/files/BankFile.h>
#include <bfsar/model/Group.h>
#include <bfsar/model/Sound.h>

#include <ui/file/Document.h>
#include <ui/file/DocumentState.h>
#include <ui/app/PopupMgr.h>
#include <ui/project/References.h>
#include <ui/widgets/RowRename.h>
#include <ui/widgets/RowSelection.h>
#include <ui/app/Shortcuts.h>
#include <ui/app/Workspace.h>

#include <algorithm>

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

void PruneFilteredSelection(const Item::List& list, ItemFilterCallback filterCallback)
{
    if (!filterCallback || sMultiSelectedItems.empty())
        return;

    const auto isFilteredOut = [&list, filterCallback](const Item* item)
    {
        return item && item->list() == &list && !filterCallback(item);
    };

    std::erase_if(sMultiSelectedItems, isFilteredOut);

    if (isFilteredOut(sMultiSelectAnchor))
        sMultiSelectAnchor = nullptr;
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
        target -= cRowSelectionPageStep;
    else if (shortcuts::Pressed(shortcuts::Action::SelectPageDown))
        target += cRowSelectionPageStep;
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
        if (!sMultiSelectedItems.empty() && sMultiSelectedItems.front()->list() == &ctx.list)
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

static void AppendRemovedItem(Item* item, std::vector<Item*>* out);

static void AppendRemovedChildren(Item::List& children, std::vector<Item*>* out)
{
    for (auto it = children.robustBegin(); it != children.robustEnd(); ++it)
        AppendRemovedItem((*it).val(), out);
}

static void AppendRemovedItem(Item* item, std::vector<Item*>* out)
{
    if (!item)
        return;

    out->push_back(item);

    switch (item->getItemType())
    {
        case Item::ItemType::Sound:
            AppendRemovedChildren(static_cast<Sound*>(item)->getStreamSoundInfo().getTrackList(), out);
            break;

        case Item::ItemType::Group:
            AppendRemovedChildren(static_cast<Group*>(item)->getItemInfoList(), out);
            break;

        case Item::ItemType::BankFile:
            AppendRemovedChildren(static_cast<BankFile*>(item)->getInstrumentList(), out);
            break;

        case Item::ItemType::BankFileInstrument:
            AppendRemovedChildren(static_cast<BankFile::Instrument*>(item)->getKeyRegionList(), out);
            break;

        case Item::ItemType::BankFileKeyRegion:
            AppendRemovedChildren(static_cast<BankFile::KeyRegion*>(item)->getVelocityRegionList(), out);
            break;

        default:
            break;
    }
}

void ForgetRemovedItems(const std::vector<Item*>& removedItems)
{
    if (removedItems.empty())
        return;

    std::vector<Item*> removed;

    for (Item* item : removedItems)
        AppendRemovedItem(item, &removed);

    std::sort(removed.begin(), removed.end());

    auto isRemoved = [&removed](Item* item)
    {
        return item && std::binary_search(removed.begin(), removed.end(), item);
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
    ForgetReferenceEntries();

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
    selection.multiSelectedItems.clear();
    selection.multiSelectAnchor = item;

    SetUITab(tab);

    FocusInfoWindow();
    FocusPropertiesWindow();
}
