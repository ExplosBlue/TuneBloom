#include <ui/items/ItemListInternal.h>

#include <ui/widgets/Dialogs.h>
#include <ui/file/File.h>
#include <messages/Messages.h>
#include <bfsar/model/MessageLog.h>
#include <ui/sound/SoundViews.h>
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

static const AudioResource* GetAssetResource(const Item* item)
{
    if (item->getItemType() != Item::ItemType::AudioAsset)
        return nullptr;

    return static_cast<const AudioAsset*>(item)->getResource();
}

static bool CanDuplicateItem(const Item* item, const char** outReason)
{
    if (item->getItemType() == Item::ItemType::Sound)
        return true;

    const char* reason = nullptr;
    const AudioResource* resource = GetAssetResource(item);
    const bool canDuplicate = resource && resource->canDuplicateAsset(*static_cast<const AudioAsset*>(item), &reason);

    if (outReason)
        *outReason = reason;

    return canDuplicate;
}

bool OffersItemDuplicate(const Item* item)
{
    switch (item->getItemType())
    {
        case Item::ItemType::Sound:
            return true;

        case Item::ItemType::AudioAsset:
        {
            const AudioResource* resource = GetAssetResource(item);

            return resource && resource->canRemoveAssets();
        }

        default:
            return false;
    }
}

bool CanDuplicateSelection(const Item* item, const char** outReason)
{
    if (!OffersItemDuplicate(item))
        return false;

    if (!IsItemMultiSelected(item))
        return CanDuplicateItem(item, outReason);

    for (const Item* selected : sMultiSelectedItems)
    {
        if (!CanDuplicateItem(selected, outReason))
            return false;
    }

    return true;
}

void RequestSelectionDuplicate(Item* item)
{
    if (IsItemMultiSelected(item))
        sDuplicateItems = sMultiSelectedItems;
    else
        sDuplicateItem = item;
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

static bool InsertListItem(ItemListContext& ctx, Item* afterItem, Item* newItem)
{
    if (ctx.onInsert)
        return ctx.onInsert(afterItem, newItem);

    if (afterItem)
        afterItem->insertBack(newItem);
    else
        ctx.list.pushBack(newItem);

    RenumberDocumentList(ctx.list);
    SetUnsavedChanges(true);

    return true;
}

static bool RemoveListItem(ItemListContext& ctx, Item* item)
{
    ForgetRemovedItems({item});

    if (ctx.onRemove)
        return ctx.onRemove(item);

    delete item;

    return true;
}

static void DeleteListItems(ItemListContext& ctx, const std::vector<Item*>& items)
{
    bool removed = false;

    for (Item* itemToDelete : items)
    {
        if (!RemoveListItem(ctx, itemToDelete))
            break;

        *ctx.selection = nullptr;
        removed = true;
    }

    if (!removed)
        return;

    RenumberDocumentList(ctx.list);
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

static void AddCreatedItem(ItemListContext& ctx, Item* item)
{
    Item* insertAfter = sInsertAfterItem;
    sInsertAfterItem = nullptr;

    if (!InsertListItem(ctx, insertAfter, item))
    {
        delete item;
        return;
    }

    ScrollItemListTo(item);
    *ctx.selection = item;
    sMultiSelectedItems.clear();
    SetSelectionInSubWindow(ctx.isSubWindow);
}

static void DrawAddItemDialog(ItemListContext& ctx, const ImVec2& center)
{
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

    const ImGuiWindowFlags addFlags = ImGuiWindowFlags_AlwaysAutoResize;

    if (ctx.addDialogWidth > 0.0f)
        ImGui::SetNextWindowContentSize(ImVec2(ctx.addDialogWidth, 0.0f));

    if (BeginDialog(sead::FormatFixedSafeString<64>(ICON_LC_PLUS " %s###Add", sead::FormatFixedSafeString<64>(messages::item::cAddDialogTitleFormat, ctx.listName).cstr()).cstr(), nullptr, addFlags))
    {
        SEAD_ASSERT(ctx.createCallback);
        InstanciateItemCallback instanciateItemCallback = ctx.createCallback(ImGui::IsWindowAppearing());
        if (!instanciateItemCallback)
        {
            sInsertAfterItem = nullptr;
            ImGui::CloseCurrentPopup();
        }

        BeginDialogFooter({messages::common::cAdd, messages::common::cCancel}, !IsWaveImportDrawn());

        if (ctx.disableAddWindow || DialogButton(messages::common::cAdd))
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
                    AddCreatedItem(ctx, addedItem);
                    ImGui::CloseCurrentPopup();
                }
            }
        }

        const bool cancelClicked = DialogButton(messages::common::cCancel);

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

    if (BeginDialog(sead::FormatFixedSafeString<64>(ICON_LC_ALERT_TRIANGLE " %s###References", messages::common::cWarning).cstr(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        if (sDeleteItem)
            ImGui::Text(messages::item::cDeleteReferencedFormat,sDeleteItem->getFormattedName().cstr());
        else
            ImGui::Text("%s", messages::item::cDeleteReferencedMany);

        BeginDialogFooter({messages::common::cDelete, messages::common::cCancel});

        if (DialogButton(messages::common::cDelete))
        {
            if (sDeleteItem)
                DeleteRequestedItem(ctx);
            else if (!sDeleteItems.empty())
                DeleteRequestedItems(ctx);

            ImGui::CloseCurrentPopup();
        }

        if (DialogButton(messages::common::cCancel))
        {
            sDeleteItem = nullptr;
            sDeleteItems.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

static std::vector<Item*> CollectReorderedItems(const ItemListContext& ctx)
{
    if (!IsItemMultiSelected(ctx.reorderSource))
    {
        if (ctx.reorderTarget->next() == ctx.reorderSource)
            return {};

        return { ctx.reorderSource };
    }

    if (IsItemMultiSelected(ctx.reorderTarget))
        return {};

    std::vector<Item*> items(sMultiSelectedItems.begin(), sMultiSelectedItems.end());
    std::sort(items.begin(), items.end(), [](Item* a, Item* b) { return a->getId() < b->getId(); });

    return items;
}

static void MoveListItemsAfter(ItemListContext& ctx, const std::vector<Item*>& items)
{
    for (Item* item : items)
        item->erase();

    for (auto it = items.rbegin(); it != items.rend(); ++it)
        ctx.reorderTarget->insertBack(*it);

    RenumberDocumentList(ctx.list);
    SetUnsavedChanges(true);
}

static void ApplyItemReorder(ItemListContext& ctx)
{
    if (!ctx.reorder)
        return;

    const std::vector<Item*> items = CollectReorderedItems(ctx);

    if (items.empty())
        return;

    if (ctx.reorderTarget->getItemType() == Item::ItemType::Sound)
        MoveSoundsHook(ctx.reorderTarget, items);
    else
        MoveListItemsAfter(ctx, items);
}

static void DuplicateSelectedAssets(ItemListContext& ctx)
{
    Item* first = sDuplicateItem ? sDuplicateItem : (sDuplicateItems.empty() ? nullptr : sDuplicateItems.front());

    if (!first || first->list() != &ctx.list || first->getItemType() != Item::ItemType::AudioAsset)
        return;

    AudioResource* resource = static_cast<AudioAsset*>(first)->getResource();
    const std::vector<Item*> sources = sDuplicateItem ? std::vector<Item*>{ sDuplicateItem } : sDuplicateItems;

    sDuplicateItem = nullptr;
    sDuplicateItems.clear();

    std::vector<Item*> copies;
    const char* firstError = nullptr;

    for (Item* source : sources)
    {
        const char* error = nullptr;

        if (AudioAsset* copy = resource->duplicateAsset(static_cast<AudioAsset*>(source), &error))
            copies.push_back(copy);
        else if (!firstError)
            firstError = error;
    }

    if (firstError)
        MessageLog::instance()->addPopup({ firstError, nullptr });

    if (copies.empty())
        return;

    SetUnsavedChanges(true);

    *ctx.selection = copies.back();
    SetSelectionInSubWindow(ctx.isSubWindow);

    sMultiSelectedItems = copies.size() > 1 ? copies : std::vector<Item*>();
    sMultiSelectAnchor = copies.front();
}

static void DuplicateSelectedSound(ItemListContext& ctx)
{
    if (!sDuplicateItem || sDuplicateItem->list() != &ctx.list)
        return;

    Sound* src = static_cast<Sound*>(sDuplicateItem);
    Sound* dup = CloneSound(src);

    sDuplicateItem = nullptr;

    if (!InsertListItem(ctx, src, dup))
    {
        delete dup;
        return;
    }

    *ctx.selection = dup;
    SetSelectionInSubWindow(ctx.isSubWindow);
}

static size_t FindSoundBlockEnd(const ItemListContext& ctx, const std::vector<Sound*>& sources, size_t first)
{
    size_t last = first;

    while (last + 1 < sources.size() && static_cast<Item*>(ctx.list.next(sources[last])) == sources[last + 1])
        ++last;

    return last;
}

static bool DuplicateSoundBlock(ItemListContext& ctx, const std::vector<Sound*>& sources, size_t first, size_t last, std::vector<Item*>* copies)
{
    Item* insertPoint = sources[last];

    for (size_t index = first; index <= last; ++index)
    {
        Sound* dup = CloneSound(sources[index]);

        if (!InsertListItem(ctx, insertPoint, dup))
        {
            delete dup;
            return false;
        }

        insertPoint = dup;
        copies->push_back(dup);
    }

    return true;
}

static void DuplicateSelectedSounds(ItemListContext& ctx)
{
    if (sDuplicateItems.empty())
        return;

    std::vector<Sound*> sources;

    for (auto it = ctx.list.robustBegin(); it != ctx.list.robustEnd(); ++it)
    {
        Item* item = (*it).val();

        if (std::find(sDuplicateItems.begin(), sDuplicateItems.end(), item) != sDuplicateItems.end())
            sources.push_back(static_cast<Sound*>(item));
    }

    if (sources.empty())
        return;

    sDuplicateItems.clear();

    std::vector<Item*> copies;

    for (size_t first = 0; first < sources.size();)
    {
        const size_t last = FindSoundBlockEnd(ctx, sources, first);

        if (!DuplicateSoundBlock(ctx, sources, first, last, &copies))
            break;

        first = last + 1;
    }

    if (copies.empty())
        return;

    RenumberDocumentList(ctx.list);
    SetUnsavedChanges(true);

    *ctx.selection = copies.back();
    sMultiSelectedItems = copies;
    sMultiSelectAnchor = copies.front();
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
    DuplicateSelectedAssets(ctx);
    DuplicateSelectedSound(ctx);
    DuplicateSelectedSounds(ctx);
}
