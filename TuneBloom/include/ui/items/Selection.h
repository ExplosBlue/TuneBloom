#pragma once

#include <ui/app/Workspace.h>

#include <bfsar/model/Item.h>

#include <vector>

struct SelectionState
{
    Item* selectedItem = nullptr;
    Item* subSelectedItem = nullptr;
    bool selectedItemIsSubWindow = false;
    std::vector<Item*> multiSelectedItems;
    Item* multiSelectAnchor = nullptr;
};

SelectionState& GetTabSelection(UIType tab);
SelectionState& GetActiveTabSelection();

#define sSelectedItem (GetActiveTabSelection().selectedItem)
#define sSubSelectedItem (GetActiveTabSelection().subSelectedItem)
#define sSelectedItemIsSubWindow (GetActiveTabSelection().selectedItemIsSubWindow)
#define sMultiSelectedItems (GetActiveTabSelection().multiSelectedItems)
#define sMultiSelectAnchor (GetActiveTabSelection().multiSelectAnchor)

void SelectItem(Item* item);
void SelectSubItem(Item* item);
void ForgetRemovedItems(const std::vector<Item*>& removed);
bool IsItemMultiSelected(const Item* item);

Item* GetActiveSelection();

void ClearSelection();

template <typename T>
std::vector<T*> CollectForAction(Item* item)
{
    std::vector<T*> result;

    if (!item)
        return result;

    if (!IsItemMultiSelected(item))
    {
        result.push_back(static_cast<T*>(item));
        return result;
    }

    for (Item* selected : sMultiSelectedItems)
        result.push_back(static_cast<T*>(selected));

    return result;
}
