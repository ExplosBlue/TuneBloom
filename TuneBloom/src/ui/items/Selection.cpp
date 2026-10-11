#include <ui/items/Selection.h>

#include <ui/app/Workspace.h>

Item* sSelectedItemArr[(size_t)UIType::Max + 1] = {};
Item* sSubSelectedItemArr[(size_t)UIType::Max + 1] = {};
bool sSelectedItemIsSubWindowArr[(size_t)UIType::Max + 1] = {};

std::vector<Item*> sMultiSelectedItemsArr[(size_t)UIType::Max + 1];
Item* sMultiSelectAnchorArr[(size_t)UIType::Max + 1] = {};

Item* GetActiveSelection()
{
    return sSelectedItemIsSubWindow ? sSubSelectedItem : sSelectedItem;
}

void ClearSelection()
{
    for (size_t i = 0; i <= (size_t)UIType::Max; i++)
    {
        sSelectedItemArr[i] = nullptr;
        sSubSelectedItemArr[i] = nullptr;
        sSelectedItemIsSubWindowArr[i] = false;
        sMultiSelectedItemsArr[i].clear();
        sMultiSelectAnchorArr[i] = nullptr;
    }
}
