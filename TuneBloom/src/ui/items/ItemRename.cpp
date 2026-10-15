#include <ui/items/ItemListInternal.h>

#include <bfsar/files/BankFile.h>
#include <ui/file/File.h>
#include <ui/Messages.h>
#include <ui/widgets/RowRename.h>
#include <ui/file/UnsavedChanges.h>

#include <cstdlib>

static constexpr ImGuiID cItemListIdField = 1;

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

static s32 FilterDigitChar(ImGuiInputTextCallbackData* data)
{
    return data->EventChar < '0' || data->EventChar > '9';
}

static bool ParseProgramNo(const char* text, s16* outProgramNo)
{
    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);

    if (end == text || *end != '\0')
        return false;

    *outProgramNo = static_cast<s16>(sead::MathCalcCommon<long>::clamp2(BankFile::Instrument::cProgramNoMin, value, BankFile::Instrument::cProgramNoMax));

    return true;
}

bool CanEditItemListId(const Item* item)
{
    return IsItemListIdProgramNo(item->getItemType());
}

void BeginItemListIdEdit(Item* item, ImGuiID table)
{
    sead::FixedSafeString<16> text;
    FormatItemListId(item, &text);

    BeginRowRename(item, table, text.cstr(), cItemListIdField);
}

Item* GetItemListIdEditedItem(ImGuiID table)
{
    return static_cast<Item*>(GetRenamedRow(table, cItemListIdField));
}

void DrawItemListIdInput(Item* item)
{
    sead::FixedSafeString<256> text;
    s16 programNo = 0;

    if (!DrawRowRenameField("", &text, &FilterDigitChar) || !ParseProgramNo(text.cstr(), &programNo))
        return;

    BankFile::Instrument* instrument = static_cast<BankFile::Instrument*>(item);

    if (programNo == instrument->getProgramNo())
        return;

    instrument->setProgramNo(programNo);
    SetUnsavedChanges(true);
}
