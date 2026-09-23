#include <ui/UI.h>
#include <ui/Messages.h>

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
            error = messages::validation::cInvalidWaveArchive;
            return this;
    }

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
        WaveArchiveSelector("Wave Archive", &sWarcType, &sWarcItem, sBfsar.getWaveArchiveList());

        ImGui::SeparatorText(messages::bank::cFileLabel);

        ImGui::RadioButton(messages::bank::cFileModeCreateNew, &sBankFileMode, cBankFileModeCreateNew);
        ImGui::SameLine();
        ImGui::RadioButton(messages::bank::cFileModeSelectExisting, &sBankFileMode, cBankFileModeSelectExisting);
        ImGui::SameLine();
        ImGui::RadioButton(messages::bank::cFileModeNone, &sBankFileMode, cBankFileModeNone);

        if (sBankFileMode != cBankFileModeSelectExisting)
            ImGui::BeginDisabled();

        ItemSelector(messages::bank::cFileLabel, sBfsar.getBankFileList(), &sBankFileItem, true);

        if (sBankFileMode != cBankFileModeSelectExisting)
            ImGui::EndDisabled();

        if (sBankFileMode == cBankFileModeNone)
        {
            ImGui::TextDisabled(messages::bank::cWillHaveNoFile);
        }
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

        SetDisabledTooltip(messages::bank::cNoFileAttached);
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
        if (ItemSelector(messages::bank::cFileLabel, sBfsar.getBankFileList(), &file, true))
        {
            bank->getFileRef().attach(file);
            SetUnsavedChanges(true);
        }
    }
}
