#define IMGUI_DEFINE_MATH_OPERATORS
#include <ui/project/ItemViews.h>
#include <ui/project/References.h>

#include <ui/widgets/ContextMenu.h>
#include <ui/file/File.h>
#include <ui/items/ItemBrowser.h>
#include <ui/items/ItemColumns.h>
#include <ui/items/ItemList.h>
#include <ui/widgets/ListFilter.h>
#include <ui/Messages.h>
#include <ui/items/Selection.h>
#include <ui/file/UnsavedChanges.h>
#include <ui/app/Workspace.h>

#include <libs/IconsLucide.h>

namespace {

struct GroupAddType
{
    Item::ItemType type;
    const char* icon;
    const char* label;
};

}

static const GroupAddType cGroupAddTypes[] = {
    { Item::ItemType::Sound,       ICON_LC_LIST_MUSIC, messages::item::cTypeSound },
    { Item::ItemType::SoundSet,    ICON_LC_MUSIC_4,    messages::item::cTypeSoundSet },
    { Item::ItemType::Bank,        ICON_LC_PIANO,      messages::item::cTypeBank },
    { Item::ItemType::WaveArchive, ICON_LC_ARCHIVE,    messages::item::cTypeWaveArchive },
};

static Item::ItemType sGroupAddType = Item::ItemType::Invalid;
static Item* sGroupAddAfter = nullptr;
static bool sGroupAddOpen = false;
static sead::FixedSafeString<256> sGroupAddError;

static void DrawGroupAddMenu(const char* icon, const char* label, Item* after)
{
    if (!BeginContextSubMenu(icon, label))
        return;

    for (const GroupAddType& entry : cGroupAddTypes)
    {
        const bool hasItems = sBfsar.getItemList(entry.type).front() != nullptr;

        if (!ContextMenuItem(entry.icon, entry.label, hasItems, messages::group::cAddNoItems))
            continue;

        sGroupAddType = entry.type;
        sGroupAddAfter = after;
        sGroupAddOpen = true;
    }

    EndContextSubMenu();
}

static void GroupItemContextMenuFunc(Item* item)
{
    DrawGroupAddMenu(ICON_LC_PLUS, messages::group::cAddItem, nullptr);

    if (item)
        DrawGroupAddMenu(ICON_LC_LIST_PLUS, messages::menu::cInsertAfter, item);
}

static void AddGroupItem(Group* group, Item* item)
{
    Group::ItemInfo* info = new Group::ItemInfo(group);
    info->setItemRefType_(sGroupAddType);
    info->getItemRef().attach(item);
    info->setLoadItem_(0);

    if (info->validate(sGroupAddError))
    {
        ImGui::OpenPopup("###GroupItemAdd");
        delete info;
        return;
    }

    Item::List& list = group->getItemInfoList();

    if (sGroupAddAfter)
        sGroupAddAfter->insertBack(info);
    else
        list.pushBack(info);

    sBfsar.updateList(list);
    SetUnsavedChanges(true);
    SelectSubItem(info);
}

static void DrawGroupItemAdd(Group* group)
{
    static constexpr const char* cBrowserId = "##GroupItemAdd";

    WarningPopup("###GroupItemAdd", sGroupAddError.cstr());

    if (sGroupAddType == Item::ItemType::Invalid)
        return;

    const Item::List& list = sBfsar.getItemList(sGroupAddType);

    if (sGroupAddOpen)
        OpenItemBrowser(cBrowserId, list, nullptr);

    sGroupAddOpen = false;

    const sead::FormatFixedSafeString<64> title(messages::group::cAddTitleFormat, GetItemTypeName(sGroupAddType));

    Item* item = nullptr;
    if (DrawItemBrowser(cBrowserId, list, &item, false, title.cstr(), messages::dialog::cAdd) && item)
        AddGroupItem(group, item);
}

const char* GroupItemPrefixFunc(Item* item)
{
    Group::ItemInfo* groupItem = (Group::ItemInfo*)item;

    const char* icon = ICON_LC_FILE_QUESTION " ";

    if (!groupItem->getIsDisabled())
    {
        switch (groupItem->getItemRefType())
        {
            case Item::ItemType::Sound:
                if (groupItem->getItemRef().isAttached())
                    icon = GetSoundIcon(static_cast<const Sound*>(groupItem->getItemRef().getItem()));

                break;

            case Item::ItemType::SoundSet:
                if (groupItem->getItemRef().isAttached())
                {
                    SoundSet* soundSet = (SoundSet*)groupItem->getItemRef().getItem();
                    switch (soundSet->getSoundSetType())
                    {
                        case SoundSet::SoundSetType::Wave:
                            icon = ICON_LC_AUDIO_WAVEFORM " ";
                            break;

                        case SoundSet::SoundSetType::Seq:
                            icon = ICON_LC_MUSIC_2 " ";
                            break;

                        default:
                            break;
                    }
                }

                break;

            case Item::ItemType::Bank:
                icon = ICON_LC_PIANO " ";
                break;

            case Item::ItemType::WaveArchive:
                icon = ICON_LC_ARCHIVE " ";
                break;

            default:
                break;
        }
    }

    return icon;
}

void DrawGroupsUI()
{
    DrawSplitView(
        "Groups", 0.0f,
        [&]()
        {
            DrawTabFilterBar();
            DrawAllItemsUI("Group", messages::item::cTypeGroup, sBfsar.getGroupList(), &CreateGroupFunc, nullptr, &DrawGroupContextMenu, GetItemFilterCallback());
            HandleGroupFileRequests();
        },
        []()
        {
            static sead::FixedSafeString<320> sNote;

            if (!sSelectedItem || sSelectedItem->getItemType() != Item::ItemType::Group)
            {
                DrawEmptyDetailPane(messages::group::cNoneSelected);
                return;
            }

            Group *group = static_cast<Group *>(sSelectedItem);

            DrawDetailTabFilterBar();

            sNote.format(messages::item::cCountInFormat, group->getFormattedName().cstr());
            SetNextItemListNote(sNote.cstr());
            SetNextItemListRemoveText(messages::menu::cRemoveFromGroup, messages::menu::cRemoveItemsFromGroupFormat, false);

            DrawAllItemsUI("Item", messages::item::cTypeItem, group->getItemInfoList(), nullptr, &GroupItemPrefixFunc, &GroupItemContextMenuFunc, GetDetailFilterCallback(), false, nullptr, nullptr, true);
            DrawGroupItemAdd(group);
        });
}
