#include <ui/items/Selection.h>

#include <ui/file/DocumentState.h>

SelectionState& GetTabSelection(UIType tab)
{
    return ActiveDocumentState().selections[static_cast<size_t>(tab)];
}

SelectionState& GetActiveTabSelection()
{
    return GetTabSelection(GetUITab());
}

Item* GetActiveSelection()
{
    return sSelectedItemIsSubWindow ? sSubSelectedItem : sSelectedItem;
}

void ClearSelection()
{
    for (SelectionState& selection : ActiveDocumentState().selections)
        selection = SelectionState();
}
