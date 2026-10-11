#include <ui/items/ItemListInternal.h>

#include <ui/widgets/Dialogs.h>
#include <ui/file/File.h>
#include <ui/Messages.h>
#include <ui/stream/StreamTrackImport.h>
#include <ui/file/UnsavedChanges.h>
#include <ui/wave/WaveImportPanel.h>

#include <bfsar/model/Sound.h>

#include <libs/IconsLucide.h>

#include <algorithm>

static Item* sDeleteItem = nullptr;
static std::vector<Item*> sDeleteItems;
static Item* sDuplicateItem = nullptr;
static std::vector<Item*> sDuplicateItems;
static Item* sInsertAfterItem = nullptr;

Item* GetInsertAfterItem()
{
    return sInsertAfterItem;
}

void ClearInsertAfterItem()
{
    sInsertAfterItem = nullptr;
}

void SetInsertAfterItem(Item* item)
{
    sInsertAfterItem = item;
}

void RequestItemDelete(Item* item)
{
    sDeleteItem = item;
}

void RequestItemsDelete(const std::vector<Item*>& items)
{
    sDeleteItems = items;
}

void RequestSelectionDuplicate(Item* sound)
{
    if (IsItemMultiSelected(sound))
        sDuplicateItems = sMultiSelectedItems;
    else
        sDuplicateItem = sound;
}

void ForgetItemListRequests(const std::vector<Item*>& removed)
{
    auto isRemoved = [&removed](Item* item)
    {
        return std::find(removed.begin(), removed.end(), item) != removed.end();
    };

    if (isRemoved(sDeleteItem))
        sDeleteItem = nullptr;

    if (isRemoved(sDuplicateItem))
        sDuplicateItem = nullptr;

    if (isRemoved(sInsertAfterItem))
        sInsertAfterItem = nullptr;

    sDeleteItems.erase(std::remove_if(sDeleteItems.begin(), sDeleteItems.end(), isRemoved), sDeleteItems.end());
    sDuplicateItems.erase(std::remove_if(sDuplicateItems.begin(), sDuplicateItems.end(), isRemoved), sDuplicateItems.end());
}

static bool HasRealReferences(const Item* item)
{
    for (auto it = item->getReferences().robustBegin(); it != item->getReferences().robustEnd(); ++it)
    {
        const ItemReference* ref = it->val();

        if (ref && ref->getOwner() && !ref->getOwner()->isFileWindow())
            return true;
    }

    return false;
}

static void DeleteListItems(ItemListContext& ctx, const std::vector<Item*>& items)
{
    Item*& selectedItem = *ctx.selection;

    for (Item* itemToDelete : items)
    {
        ForgetRemovedItems({itemToDelete});

        if (ctx.onRemove)
            ctx.onRemove(itemToDelete);
        else
            delete itemToDelete;

        selectedItem = nullptr;
    }

    sBfsar.updateList(ctx.list);
    SetUnsavedChanges(true);
}

static void DeleteRequestedItem(ItemListContext& ctx)
{
    Item* item = sDeleteItem;
    sDeleteItem = nullptr;

    DeleteListItems(ctx, {item});
}

static void DeleteRequestedItems(ItemListContext& ctx)
{
    const std::vector<Item*> items = std::move(sDeleteItems);
    sDeleteItems.clear();

    DeleteListItems(ctx, items);
}

static void HandleDeleteRequests(ItemListContext& ctx)
{
    if (sDeleteItem && sDeleteItem->list() == &ctx.list)
    {
        if (HasRealReferences(sDeleteItem))
        {
            ImGui::OpenPopup("###References");
        }
        else
        {
            DeleteRequestedItem(ctx);
        }
    }

    if (!sDeleteItems.empty() && sDeleteItems.front()->list() == &ctx.list)
    {
        if (std::any_of(sDeleteItems.begin(), sDeleteItems.end(), HasRealReferences))
        {
            ImGui::OpenPopup("###References");
        }
        else
        {
            DeleteRequestedItems(ctx);
        }
    }
}

static void DrawAddItemDialog(ItemListContext& ctx, const ImVec2& center)
{
    Item*& selectedItem = *ctx.selection;

    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

    const ImGuiWindowFlags addFlags = ImGuiWindowFlags_AlwaysAutoResize;

    // Wave import panel
    if (&ctx.list == &sBfsar.getWaveFileList())
        ImGui::SetNextWindowContentSize(ImVec2(CalcWaveImportWidth(), 0.0f));

    if (BeginDialog(sead::FormatFixedSafeString<64>(ICON_LC_PLUS " %s###Add", sead::FormatFixedSafeString<64>(messages::item::cAddDialogTitleFormat, ctx.listName).cstr()).cstr(), nullptr, addFlags))
    {
        SEAD_ASSERT(ctx.createCallback);
        InstanciateItemCallback instanciateItemCallback = ctx.createCallback(ImGui::IsWindowAppearing());
        if (!instanciateItemCallback)
        {
            sInsertAfterItem = nullptr;
            ImGui::CloseCurrentPopup();
        }

        BeginDialogFooter({messages::dialog::cAdd, messages::dialog::cCancel});

        if (ctx.disableAddWindow || DialogButton(messages::dialog::cAdd))
        {
            if (!instanciateItemCallback)
            {
                ImGui::CloseCurrentPopup();
            }
            else
            {
                Item* addedItem = instanciateItemCallback();

                if (addedItem)
                {
                    ScrollItemListTo(addedItem);
                    selectedItem = addedItem;
                    sMultiSelectedItems.clear();
                    SetSelectionInSubWindow(ctx.isSubWindow);

                    Item *insertAfter = sInsertAfterItem;
                    sInsertAfterItem = nullptr;

                    if (ctx.onInsert)
                    {
                        ctx.onInsert(insertAfter, addedItem);
                    }
                    else
                    {
                        if (insertAfter)
                            insertAfter->insertBack(addedItem);
                        else
                            ctx.list.pushBack(addedItem);

                        sBfsar.updateList(ctx.list);
                    }

                    SetUnsavedChanges(true);

                    ImGui::CloseCurrentPopup();
                }
            }
        }

        const bool cancelClicked = DialogButton(messages::dialog::cCancel);

        if (WaveImportConfirmCancel(cancelClicked))
        {
            sInsertAfterItem = nullptr;
            ResetStreamCreateImport();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

static void DrawReferencesDialog(ItemListContext& ctx, const ImVec2& center)
{
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (BeginDialog(sead::FormatFixedSafeString<64>(ICON_LC_ALERT_TRIANGLE " %s###References", messages::popup::cTitleWarning).cstr(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        if (sDeleteItem)
            ImGui::Text(messages::item::cDeleteReferencedFormat,sDeleteItem->getFormattedName().cstr());
        else
            ImGui::Text("%s", messages::item::cDeleteReferencedMany);

        BeginDialogFooter({messages::dialog::cDelete, messages::dialog::cCancel});

        if (DialogButton(messages::dialog::cDelete))
        {
            if (sDeleteItem)
                DeleteRequestedItem(ctx);
            else if (!sDeleteItems.empty())
                DeleteRequestedItems(ctx);

            ImGui::CloseCurrentPopup();
        }

        if (DialogButton(messages::dialog::cCancel))
        {
            sDeleteItem = nullptr;
            sDeleteItems.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

static void ApplyItemReorder(ItemListContext& ctx)
{
    if (!ctx.reorder)
        return;

    const bool isMultiReorder = IsItemMultiSelected(ctx.reorderSource);

    if (isMultiReorder && !IsItemMultiSelected(ctx.reorderTarget))
    {
        std::vector<Item*> itemsToMove(sMultiSelectedItems.begin(), sMultiSelectedItems.end());
        std::sort(itemsToMove.begin(), itemsToMove.end(),
            [](Item* a, Item* b) { return a->getId() < b->getId(); });

        for (Item* mi : itemsToMove)
            mi->erase();

        for (auto it = itemsToMove.rbegin(); it != itemsToMove.rend(); ++it)
            ctx.reorderTarget->insertBack(*it);

        sBfsar.updateList(ctx.list);
        SetUnsavedChanges(true);
    }
    else if (!isMultiReorder && ctx.reorderTarget->next() != ctx.reorderSource)
    {
        ctx.reorderSource->erase();
        ctx.reorderTarget->insertBack(ctx.reorderSource);

        sBfsar.updateList(ctx.list);
        SetUnsavedChanges(true);
    }
}

static void DuplicateSelectedSound(ItemListContext& ctx)
{
    if (!sDuplicateItem || sDuplicateItem->list() != &ctx.list)
        return;

    Item*& selectedItem = *ctx.selection;

    Sound* src = static_cast<Sound*>(sDuplicateItem);
    Sound* dup = CloneSound(src);

    if (ctx.onInsert)
        ctx.onInsert(src, dup);
    else
    {
        src->insertBack(dup);
        sBfsar.updateList(ctx.list);
    }
    SetUnsavedChanges(true);

    selectedItem = dup;
    SetSelectionInSubWindow(ctx.isSubWindow);

    sDuplicateItem = nullptr;
}

static void DuplicateSelectedSounds(ItemListContext& ctx)
{
    if (sDuplicateItems.empty())
        return;

    Item*& selectedItem = *ctx.selection;

    std::vector<Sound*> orderedSrcs;
    for (auto it = ctx.list.robustBegin(); it != ctx.list.robustEnd(); ++it)
    {
        Item* cur = static_cast<Item*>((*it).val());
        if (std::find(sDuplicateItems.begin(), sDuplicateItems.end(), cur) != sDuplicateItems.end())
            orderedSrcs.push_back(static_cast<Sound*>(cur));
    }

    std::vector<Item*> newDups;
    size_t i = 0;
    while (i < orderedSrcs.size())
    {
        // Contiguous block
        size_t blockEnd = i;
        while (blockEnd + 1 < orderedSrcs.size())
        {
            Item* after = static_cast<Item*>(ctx.list.next(orderedSrcs[blockEnd]));
            if (after == orderedSrcs[blockEnd + 1])
                ++blockEnd;
            else
                break;
        }

        Item *insertPoint = orderedSrcs[blockEnd];
        for (size_t j = i; j <= blockEnd; ++j)
        {
            Sound *dup = CloneSound(orderedSrcs[j]);

            if (ctx.onInsert)
                ctx.onInsert(insertPoint, dup);
            else
                insertPoint->insertBack(dup);

            insertPoint = dup;
            newDups.push_back(dup);
        }

        i = blockEnd + 1;
    }

    if (!orderedSrcs.empty())
    {
        sBfsar.updateList(ctx.list);
        SetUnsavedChanges(true);
        if (!newDups.empty())
        {
            selectedItem = newDups.back();
            sMultiSelectedItems = newDups;
            sMultiSelectAnchor = newDups.front();
        }
        sDuplicateItems.clear();
    }
}

void ApplyItemListEdits(ItemListContext& ctx)
{
    if (ctx.add)
        ImGui::OpenPopup("###Add");

    HandleDeleteRequests(ctx);

    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();

    DrawAddItemDialog(ctx, center);
    DrawReferencesDialog(ctx, center);
    ApplyItemReorder(ctx);
    DuplicateSelectedSound(ctx);
    DuplicateSelectedSounds(ctx);
}
