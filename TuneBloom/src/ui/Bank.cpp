#include <ui/UI.h>

// Banks

bool Bank::validate(sead::BufferedSafeString& error) const
{
    if (!Item::validateName(error))
    {
        return false;
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
            return false;
    }

    if (!getFileRef().isAttached())
    {
        error = "Invalid Bank File";
        return false;
    }

    return true;
}

InstanciateItemCallback CreateBankFunc(bool clear)
{
    return CreateItemFunc(clear, []() -> Item* { return new Bank(); }, nullptr);
}

void DrawBanksUI()
{
    DrawAllItemsUI("Bank", sBfsar.getBankList(), &CreateBankFunc);
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
        }
    }

    {
        Item* file = bank->getFileRef().getItem();
        if (ItemSelector("Bank File", sBfsar.getBankFileList(), &file))
        {
            bank->getFileRef().attach(file);
        }
    }
}
