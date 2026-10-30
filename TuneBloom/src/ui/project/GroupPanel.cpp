#include <ui/project/ItemViews.h>

#include <ui/widgets/Dialogs.h>
#include <ui/file/File.h>
#include <ui/items/ItemList.h>
#include <ui/items/ItemSelector.h>
#include <messages/Messages.h>
#include <ui/widgets/Properties.h>
#include <ui/items/Selection.h>
#include <ui/file/UnsavedChanges.h>
#include <ui/widgets/Widgets.h>

#include <bfsar/model/Bank.h>

// Groups

static void GroupCreatePropertiesCallback(bool clear, Item *item, bool *validate)
{
    static u32 sOutputType = 0;

    if (clear)
    {
        sOutputType = 0;
        return;
    }

    if (!item && !validate)
    {
        static const char *sOutputTypes[] = {
            messages::group::cOutputEmbed,
            messages::group::cOutputLink,
            messages::group::cOutputExternal};

        DialogRow(messages::group::cOutputTypeLabel);
        ComboScroll("##OutputType", (s32 *)&sOutputType, sOutputTypes, IM_ARRAYSIZE(sOutputTypes));
    }
    else if (item && !validate)
    {
        Group *group = static_cast<Group *>(item);
        group->setOutputType(static_cast<Group::OutputType>(sOutputType));
    }
}

InstanciateItemCallback CreateGroupFunc(bool clear)
{
    return CreateItemFunc(clear, []() -> Item * { return new Group(); }, &GroupCreatePropertiesCallback);
}

void DrawGroupPropertiesUI()
{
    Group* group = static_cast<Group*>(sSelectedItem);

    {
        static const char* sOutputTypes[] = {
            messages::group::cOutputEmbed,
            messages::group::cOutputLink,
            messages::group::cOutputExternal
        };

        static const char* sOutputTypeDetails[] = {
            messages::group::cEmbedDetail,
            messages::group::cLinkDetail,
            messages::group::cExternalDetail
        };

        if (!BeginPropertyBox("GroupOutput"))
            return;

        const bool isExternal = group->getOutputType() == Group::OutputType::External;
        const bool isLocked = isExternal && group->getExternalFileState() == Group::ExternalFileState::Missing;

        ImGui::BeginDisabled(isLocked);

        s32 outputType = static_cast<s32>(group->getOutputType());
        if (DrawPanelHeaderCombo(messages::group::cOutputTypeLabel, "##OutputType", &outputType, sOutputTypes, sOutputTypeDetails, IM_ARRAYSIZE(sOutputTypes), nullptr, isExternal))
        {
            group->setOutputType(static_cast<Group::OutputType>(outputType));
            SetUnsavedChanges(true);
        }

        ImGui::EndDisabled();

        if (isLocked)
            ImGui::SetItemTooltip("%s", messages::group::cOutputTypeLockedTip);

        if (isExternal)
            DrawGroupFileRow(group);

        EndPropertyBox();
    }
}

static bool IsSameLoadItemKind(Item::ItemType itemType, const Item* oldItem, const Item* item)
{
    switch (itemType)
    {
        case Item::ItemType::Sound:
            return static_cast<const Sound*>(oldItem)->getSoundType() == static_cast<const Sound*>(item)->getSoundType();

        case Item::ItemType::SoundSet:
            return static_cast<const SoundSet*>(oldItem)->getSoundSetType() == static_cast<const SoundSet*>(item)->getSoundSetType();

        default:
            return true;
    }
}

void Group::ItemInfo::drawUI()
{
    bool disabled = mIsDisabled;
    PropertyRow(messages::common::cDisabled);
    if (ImGui::Checkbox("##Disable", &disabled))
    {
        mIsDisabled = disabled;
        SetUnsavedChanges(true);
    }

    if (!BeginPropertyBox("GroupItem"))
        return;

    if (disabled)
    {
        ImGui::BeginDisabled();
    }

    {
        s32 itemRefType = static_cast<s32>(mItemRefType) - 1;
        if (DrawPanelHeaderCombo(messages::group::cItemTypeLabel, "##ItemType", &itemRefType, sItemIdTypes, nullptr, IM_ARRAYSIZE(sItemIdTypes)))
        {
            mItemRefType = static_cast<ItemType>(itemRefType + 1);
            mItemRef.detach();

            mLoadItem = 0; // All
            SetUnsavedChanges(true);
        }
    }

    {
        Item* oldItem = mItemRef.getItem();
        Item* item = oldItem;
        PropertyRow(messages::common::cItem);
        if (ItemSelector("##Item", ActiveSoundArchive().getItemList(mItemRefType), &item, false))
        {
            mItemRef.attach(item);
            SetUnsavedChanges(true);

            if (oldItem && item && !IsSameLoadItemKind(mItemRefType, oldItem, item))
                mLoadItem = 0; // All
        }
    }

    static sead::FixedSafeString<256> sError;

    {
        u32 itemCount = 0;
        const char** items = getLoadItems(&itemCount);

        bool enable = mItemRef.isAttached() && items;

        u32 oldLoadItem = mLoadItem;
        u32 loadItem = oldLoadItem;
        if (!enable)
        {
            ImGui::BeginDisabled();

            static const char* all = messages::common::cAll;

            itemCount = 1;
            items = &all;
            oldLoadItem = 0;
            loadItem = 0;
        }

        PropertyRow(messages::group::cLoadItemsLabel);

        if (ComboScroll("##LoadItems", (s32 *)&loadItem, items, itemCount))
        {
            mLoadItem = loadItem;

            if (validate(sError))
            {
                ImGui::OpenPopup("###LoadItem");
                mLoadItem = oldLoadItem;
            }
            else
            {
                SetUnsavedChanges(true);
            }
        }

        if (!enable)
        {
            ImGui::EndDisabled();
        }
    }

    WarningPopup("###LoadItem", sError.cstr());

    if (disabled)
    {
        ImGui::EndDisabled();
    }

    EndPropertyBox();
}

