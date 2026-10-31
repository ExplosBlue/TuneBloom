#include <ui/items/ItemListInternal.h>

#include <ui/widgets/ContextMenu.h>
#include <messages/Messages.h>

#include <libs/IconsLucide.h>

#include <utility>

static constexpr char cListMenuId[] = "##ListFreeMenu";
static constexpr char cAddMenuId[]  = "##ListAddMenu";

bool DrawItemContextMenu(ItemListContext& ctx, Item* item)
{
    const CreateItemCallback createCallback = ctx.createCallback;
    const ContextMenuCallback menuCallback = ctx.menuCallback;
    const ItemRemoveText& removeText = ctx.removeText;

    if (!item && !createCallback && !menuCallback)
        return false;

    if (!item && std::exchange(ctx.addRequested, false))
    {
        if (createCallback)
        {
            SetInsertAfterItem(nullptr);
            sMultiSelectedItems.clear();
            return true;
        }

        ImGui::OpenPopup(ctx.addMenuCallback ? cAddMenuId : cListMenuId);
        ImGui::SetNextWindowPos(ImVec2(ctx.addAnchor.x, ctx.addAnchor.y + ImGui::GetStyle().ItemInnerSpacing.y), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    }

    if (!item && ctx.addMenuCallback && BeginContextMenu(cAddMenuId))
    {
        ctx.addMenuCallback(nullptr);
        EndContextMenu();
    }

    const sead::FormatFixedSafeString<64> menuId("%p", item);

    if (!(item ? BeginItemContextMenu(menuId.cstr()) : BeginWindowContextMenu(cListMenuId)))
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

    if (item && createCallback && !ctx.fixedOrder && ContextMenuItem(ICON_LC_LIST_PLUS, messages::menu::cInsertAfter))
    {
        SetInsertAfterItem(item);
        sMultiSelectedItems.clear();
        add = true;
    }

    if (item && !multiActive && CanRenameItem(item) && ContextMenuItem(ICON_LC_TEXT_CURSOR_INPUT, messages::common::cRename))
        BeginItemRename(item, ctx.tableId);

    if (item && OffersItemDuplicate(item))
    {
        sead::FixedSafeString<64> label(messages::common::cDuplicate);

        if (multiActive)
            label.format(messages::menu::cDuplicateItemsFormat, sMultiSelectedItems.size());

        const char* reason = nullptr;
        const bool canDuplicate = CanDuplicateSelection(item, &reason);

        if (ContextMenuItem(ICON_LC_COPY, label.cstr(), canDuplicate, reason))
            RequestSelectionDuplicate(item);
    }

    // Items of the list type
    if (menuCallback)
        menuCallback(item);

    if (item && ctx.removable)
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
