#include <ui/UI.h>

// Banks

const Item* Bank::validate(sead::BufferedSafeString& error) const
{
    if (!Item::validateName(error))
    {
        return this;
    }

    switch (getWaveArchiveType())
    {
        case WaveArchiveType::AutomaticShared:
        case WaveArchiveType::AutomaticIndividual:
            break;

        case WaveArchiveType::Explicit:
            if (getWaveArchiveRef().isAttached())
            {
                break;
            }

        //! Fallthrough

        default:
            error = "Invalid Wave Archive";
            return this;
    }

    if (!getFileRef().isAttached())
    {
        error = "Invalid Bank File";
        return this;
    }

    return nullptr;
}

static void BankCreatePropertiesCallback(bool clear, Item *item, bool *validate)
{
    static WaveArchiveType sWarcType = WaveArchiveType::AutomaticShared;
    static Item *sWarcItem = nullptr;
    static int sBankFileMode = 0;
    static Item *sBankFileItem = nullptr;

    if (clear)
    {
        sWarcType = WaveArchiveType::AutomaticShared;
        sWarcItem = nullptr;
        sBankFileMode = 0;
        sBankFileItem = nullptr;
        return;
    }

    if (!item && !validate)
    {
        WaveArchiveSelector("Wave Archive", &sWarcType, &sWarcItem, sBfsar.getWaveArchiveList());

        ImGui::SeparatorText("Bank File");

        ImGui::RadioButton("Create New", &sBankFileMode, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Select Existing", &sBankFileMode, 1);

        if (sBankFileMode == 0)
            ImGui::BeginDisabled();

        ItemSelector("Bank File", sBfsar.getBankFileList(), &sBankFileItem, true);

        if (sBankFileMode == 0)
            ImGui::EndDisabled();
    }
    else if (item && !validate)
    {
        Bank *bank = static_cast<Bank *>(item);
        bank->getWaveArchiveRef().attach(sWarcItem);
        bank->setWaveArchiveType(sWarcType);

        if (sBankFileMode == 0)
        {
            BankFile *bankFile = new BankFile();
            bankFile->setEnableName(true);
            bankFile->getName().format("GUESS_%s", bank->getName().cstr());

            sBfsar.getBankFileList().pushBack(bankFile);
            sBfsar.updateList(sBfsar.getBankFileList());

            bank->getFileRef().attach(bankFile);
        }
        else
        {
            bank->getFileRef().attach(sBankFileItem);
        }
    }
}

InstanciateItemCallback CreateBankFunc(bool clear)
{
    return CreateItemFunc(clear, []() -> Item* { return new Bank(); }, &BankCreatePropertiesCallback);
}

const char* BankNamePrefixFunc(Item* item)
{
    Bank* bank = static_cast<Bank*>(item);

    BankFile* bankFile = static_cast<BankFile*>(bank->getFileRef().getItem());
    if (!bankFile)
    {
        ImGui::BeginDisabled();
    }

    if (ImGui::Button(sead::FormatFixedSafeString<32>(ICON_LC_FILE_PEN "###%u", bank->getId()).cstr()))
    {
        OpenFileWindow(bankFile);
    }

    if (!bankFile)
    {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();

    return nullptr;
}

void DrawBanksUI()
{
    static SortState sSortState;
    DrawSortToolbar(sSortState);
    DrawTabFilterBar();

    DrawAllItemsUI("Bank", sBfsar.getBankList(),
                   &CreateBankFunc, &BankNamePrefixFunc, &BankContextMenuFunc, GetItemFilterCallback(),
                   false, nullptr, sSortState.mode, sSortState.ascending);
}

void DrawBankPropertiesUI()
{
    Bank* bank = static_cast<Bank*>(sSelectedItem);

    {
        Item* warc = bank->getWaveArchiveRef().getItem();
        WaveArchiveType warcType = bank->getWaveArchiveType();
        if (WaveArchiveSelector("Wave Archive", &warcType, &warc, sBfsar.getWaveArchiveList()))
        {
            bank->getWaveArchiveRef().attach(warc);
            bank->setWaveArchiveType(warcType);
            SetUnsavedChanges(true);
        }
    }

    {
        Item* file = bank->getFileRef().getItem();
        if (ItemSelector("Bank File", sBfsar.getBankFileList(), &file))
        {
            bank->getFileRef().attach(file);
            SetUnsavedChanges(true);
        }
    }
}
