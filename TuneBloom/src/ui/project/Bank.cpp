#include <ui/project/ItemViews.h>

#include <ui/widgets/Dialogs.h>
#include <ui/file/File.h>
#include <ui/project/FileLists.h>
#include <ui/file/FileWindow.h>
#include <ui/items/ItemList.h>
#include <ui/items/ItemSelector.h>
#include <ui/widgets/Knob.h>
#include <ui/widgets/ListFilter.h>
#include <ui/Messages.h>
#include <ui/widgets/Properties.h>
#include <ui/items/Selection.h>
#include <ui/file/UnsavedChanges.h>
#include <ui/widgets/Widgets.h>

// Banks

const Item* Bank::validate(sead::BufferedSafeString& error) const
{
    if (!Item::validateName(error))
    {
        return this;
    }

    if (!Item::validateWaveArchive(getWaveArchiveType(), getWaveArchiveRef(), error))
        return this;

    return nullptr;
}

static constexpr s32 cBankFileModeCreateNew = 0;
static constexpr s32 cBankFileModeSelectExisting = 1;
static constexpr s32 cBankFileModeNone = 2;

static void BankCreatePropertiesCallback(bool clear, Item *item, bool *validate)
{
    static WaveArchiveType sWarcType = WaveArchiveType::AutomaticShared;
    static Item *sWarcItem = nullptr;
    static s32 sBankFileMode = cBankFileModeCreateNew;
    static Item *sBankFileItem = nullptr;

    if (clear)
    {
        sWarcType = WaveArchiveType::AutomaticShared;
        sWarcItem = nullptr;
        sBankFileMode = cBankFileModeCreateNew;
        sBankFileItem = nullptr;
        return;
    }

    if (!item && !validate)
    {
        DialogRow(messages::common::cWaveArchive);
        WaveArchiveSelector("##WaveArchive", &sWarcType, &sWarcItem, sBfsar.getWaveArchiveList());

        DialogRowGap();

        if (!BeginGroupBox(messages::common::cBankFile))
            return;

        DialogRowGap();

        ImGui::RadioButton(messages::bank::cFileModeCreateNew, &sBankFileMode, cBankFileModeCreateNew);
        ImGui::SameLine();
        ImGui::RadioButton(messages::source::cSelectExisting, &sBankFileMode, cBankFileModeSelectExisting);
        ImGui::SameLine();
        ImGui::RadioButton(messages::common::cNone, &sBankFileMode, cBankFileModeNone);

        if (sBankFileMode == cBankFileModeNone)
        {
            DialogRowGap();
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", messages::bank::cWillHaveNoFile);
        }
        else
        {
            ImGui::BeginDisabled(sBankFileMode != cBankFileModeSelectExisting);
            ItemSelectorDialogRow(messages::common::cFile,sBfsar.getBankFileList(), &sBankFileItem, true);
            ImGui::EndDisabled();
        }

        EndGroupBox();
    }
    else if (item && !validate)
    {
        Bank *bank = static_cast<Bank *>(item);
        bank->getWaveArchiveRef().attach(sWarcItem);
        bank->setWaveArchiveType(sWarcType);

        if (sBankFileMode == cBankFileModeCreateNew)
        {
            BankFile *bankFile = new BankFile();
            bankFile->setEnableName(true);
            bankFile->getName().format("GUESS_%s", bank->getName().cstr());

            sBfsar.getBankFileList().pushBack(bankFile);
            sBfsar.updateList(sBfsar.getBankFileList());

            bank->getFileRef().attach(bankFile);
        }
        else if (sBankFileMode == cBankFileModeSelectExisting)
        {
            bank->getFileRef().attach(sBankFileItem);
        }
        else
        {
            bank->getFileRef().attach(nullptr);
        }
    }
}

InstanciateItemCallback CreateBankFunc(bool clear)
{
    return CreateItemFunc(clear, []() -> Item* { return new Bank(); }, &BankCreatePropertiesCallback);
}

static void BankRowActionFunc(Item* item)
{
    Bank* bank = static_cast<Bank*>(item);

    BankFile* bankFile = static_cast<BankFile*>(bank->getFileRef().getItem());
    if (DrawRowOpenButton(bank->getId(), messages::bank::cOpenFileTip, bankFile ? nullptr : messages::bank::cNoFileAttached))
        OpenFileWindow(bankFile);
}

void DrawBanksUI()
{
    DrawTabFilterBar();

    SetNextItemListAction(&BankRowActionFunc);

    DrawAllItemsUI("Bank", messages::common::cBank,sBfsar.getBankList(),
                   &CreateBankFunc, nullptr, &BankContextMenuFunc, GetItemFilterCallback());
}

void DrawBankPropertiesUI()
{
    Bank* bank = static_cast<Bank*>(sSelectedItem);

    if (!BeginPropertyBox("BankFiles"))
        return;

    RegionHeader(messages::common::cFiles);

    {
        Item* file = bank->getFileRef().getItem();
        PropertyRow(messages::common::cBankFile);
        if (ItemSelector("##BankFile", sBfsar.getBankFileList(), &file, true))
        {
            bank->getFileRef().attach(file);
            SetUnsavedChanges(true);
        }
    }

    {
        Item* warc = bank->getWaveArchiveRef().getItem();
        WaveArchiveType warcType = bank->getWaveArchiveType();
        PropertyRow(messages::common::cWaveArchive);
        if (WaveArchiveSelector("##WaveArchive", &warcType, &warc, sBfsar.getWaveArchiveList()))
        {
            bank->getWaveArchiveRef().attach(warc);
            bank->setWaveArchiveType(warcType);
            SetUnsavedChanges(true);
        }
    }

    EndPropertyBox();
}

std::vector<BankFile*> CollectBankFilesFromBanks(Item* item)
{
    std::vector<BankFile*> result;

    for (Bank* bank : CollectForAction<Bank>(item))
    {
        BankFile* bankFile = static_cast<BankFile*>(bank->getFileRef().getItem());

        if (bankFile)
            result.push_back(bankFile);
    }

    return result;
}

void BankContextMenuFunc(Item* item)
{
    DrawBankFileMenuItems(item != nullptr, CollectBankFilesFromBanks(item));
}
