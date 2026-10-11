#include <ui/items/ItemListInternal.h>

#include <ui/widgets/ContextMenu.h>
#include <ui/Messages.h>

#include <libs/IconsLucide.h>

bool DrawItemContextMenu(ItemListContext& ctx, Item* item)
{
    const CreateItemCallback createCallback = ctx.createCallback;
    const ContextMenuCallback menuCallback = ctx.menuCallback;
    const ItemRemoveText& removeText = ctx.removeText;

    if (!item && !createCallback && !menuCallback)
        return false;

    const sead::FormatFixedSafeString<64> menuId("%p", item);

    if (!(item ? BeginItemContextMenu(menuId.cstr()) : BeginWindowContextMenu()))
        return false;

    if (item)
    {
        *ctx.selection = item;
        SetSelectionInSubWindow(ctx.selection == &sSubSelectedItem);
    }

    const bool multiActive = IsItemMultiSelected(item);
    bool add = false;

    // Create
    if (createCallback && ContextMenuItem(ICON_LC_PLUS, messages::common::cAdd))
    {
        SetInsertAfterItem(nullptr);
        sMultiSelectedItems.clear();
        add = true;
    }

    if (item && createCallback && ContextMenuItem(ICON_LC_LIST_PLUS, messages::menu::cInsertAfter))
    {
        SetInsertAfterItem(item);
        sMultiSelectedItems.clear();
        add = true;
    }

    if (item && item->getItemType() == Item::ItemType::Sound)
    {
        sead::FixedSafeString<64> label(messages::common::cDuplicate);

        if (multiActive)
            label.format(messages::menu::cDuplicateItemsFormat, sMultiSelectedItems.size());

        if (ContextMenuItem(ICON_LC_COPY, label.cstr()))
            RequestSelectionDuplicate(item);
    }

    // Items of the list type
    if (menuCallback)
        menuCallback(item);

    // Delete or remove
    if (item)
    {
        sead::FixedSafeString<64> label(removeText.label);

        if (multiActive)
            label.format(removeText.manyFormat, sMultiSelectedItems.size());

        ContextMenuSeparator();

        const bool pressed = removeText.removesData
            ? ContextMenuDangerItem(ICON_LC_TRASH_2, label.cstr())
            : ContextMenuItem(ICON_LC_LIST_MINUS, label.cstr());

        if (pressed)
        {
            if (multiActive)
                RequestItemsDelete(sMultiSelectedItems);
            else
                RequestItemDelete(item);
        }
    }

    EndContextMenu();

    return add;
}
