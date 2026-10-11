#include <ui/items/ItemListInternal.h>

#include <ui/file/File.h>
#include <ui/Messages.h>
#include <ui/widgets/RowRename.h>
#include <ui/file/UnsavedChanges.h>

static bool IsItemNameOptional(const Item* item)
{
    switch (item->getItemType())
    {
        case Item::ItemType::WaveFile:
        case Item::ItemType::SequenceFile:
        case Item::ItemType::BankFile:
        case Item::ItemType::BankFileInstrument:
            return true;

        default:
            return false;
    }
}

bool CanRenameItem(const Item* item)
{
    switch (item->getItemType())
    {
        case Item::ItemType::Sound:
        case Item::ItemType::SoundSet:
        case Item::ItemType::Bank:
        case Item::ItemType::WaveArchive:
        case Item::ItemType::Group:
        case Item::ItemType::Player:
            return item->isEnableName();

        default:
            return IsItemNameOptional(item);
    }
}

ItemRenameResult RenameItem(Item* item, const sead::FixedSafeString<256>& name)
{
    if (IsItemNameOptional(item))
    {
        const sead::FixedSafeString<256> shown(item->isEnableName() ? item->getName().cstr() : "");

        if (name == shown)
            return ItemRenameResult::None;

        item->setEnableName(!name.isEmpty());

        if (name.isEmpty())
            item->getName().clear();
        else
            item->getName() = name;

        SetUnsavedChanges(true);
        return ItemRenameResult::Renamed;
    }

    if (name == item->getName())
        return ItemRenameResult::None;

    if (name.isEmpty())
        return ItemRenameResult::Empty;

    if (!sBfsar.validName(name))
        return ItemRenameResult::Invalid;

    if (!sBfsar.validateName(name))
        return ItemRenameResult::Duplicate;

    item->getName() = name;
    SetUnsavedChanges(true);

    return ItemRenameResult::Renamed;
}

void OpenRenamePopup(ItemRenameResult result)
{
    switch (result)
    {
        case ItemRenameResult::Empty:
            ImGui::OpenPopup("###EmptyName");
            break;

        case ItemRenameResult::Invalid:
            ImGui::OpenPopup("###InvalidName");
            break;

        case ItemRenameResult::Duplicate:
            ImGui::OpenPopup("###Dupe");
            break;

        default:
            break;
    }
}

void DrawRenamePopups()
{
    WarningPopup("###InvalidName", messages::item::cNameInvalidDetail);
    WarningPopup("###EmptyName", messages::item::cNameEmpty);
    DupeNamePopup();
}

void BeginItemRename(Item* item, ImGuiID table)
{
    BeginRowRename(item, table, IsItemNameOptional(item) && !item->isEnableName() ? "" : item->getName().cstr());
}

Item* GetRenamedItem(ImGuiID table)
{
    return static_cast<Item*>(GetRenamedRow(table));
}

ItemRenameResult DrawItemRenameField(Item* item)
{
    sead::FixedSafeString<256> name;

    if (!DrawRowRenameField(IsItemNameOptional(item) ? messages::innerfile::cNoNameHint : "", &name))
        return ItemRenameResult::None;

    return RenameItem(item, name);
}
