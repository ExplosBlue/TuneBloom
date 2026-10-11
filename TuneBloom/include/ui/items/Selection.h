#pragma once

#include <ui/app/Workspace.h>

#include <bfsar/model/Item.h>

#include <vector>

extern Item* sSelectedItemArr[];
extern Item* sSubSelectedItemArr[];
extern bool sSelectedItemIsSubWindowArr[];

extern std::vector<Item*> sMultiSelectedItemsArr[];
extern Item* sMultiSelectAnchorArr[];

#define sSelectedItem sSelectedItemArr[(size_t)sSelectedUIType]
#define sSubSelectedItem sSubSelectedItemArr[(size_t)sSelectedUIType]
#define sSelectedItemIsSubWindow sSelectedItemIsSubWindowArr[(size_t)sSelectedUIType]
#define sMultiSelectedItems sMultiSelectedItemsArr[(size_t)sSelectedUIType]
#define sMultiSelectAnchor sMultiSelectAnchorArr[(size_t)sSelectedUIType]

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
