#include <ui/UI.h>

// Wave Archives

bool WaveArchive::validate(sead::BufferedSafeString& error) const
{
    if (!Item::validateName(error))
    {
        return false;
    }

    return true;
}

InstanciateItemCallback CreateWaveArchiveFunc(bool clear)
{
    return CreateItemFunc(clear, []() -> Item* { return new WaveArchive(); }, nullptr);
}

void DrawWaveArchivesUI()
{
    DrawAllItemsUI("Wave Archive", sBfsar.getWaveArchiveList(), &CreateWaveArchiveFunc);
}

void DrawWaveArchivePropertiesUI()
{
    WaveArchive* warc = static_cast<WaveArchive*>(sSelectedItem);

    {
        bool isLoadIndividual = warc->getIsLoadIndividual();
        if (ImGui::Checkbox("Load Individually", &isLoadIndividual))
        {
            warc->setIsLoadIndividual(isLoadIndividual);
        }
    }
}
