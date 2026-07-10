#include <ui/UI.h>

// Wave Archives

const Item* WaveArchive::validate(sead::BufferedSafeString& error) const
{
    if (!Item::validateName(error))
    {
        return this;
    }

    return nullptr;
}

static void WaveArchiveCreatePropertiesCallback(bool clear, Item *item, bool *validate)
{
    static bool sIsLoadIndividual = false;

    if (clear)
    {
        sIsLoadIndividual = false;
        return;
    }

    if (!item && !validate)
    {
        ImGui::Checkbox("Load Individually", &sIsLoadIndividual);
    }
    else if (item && !validate)
    {
        WaveArchive *warc = static_cast<WaveArchive *>(item);
        warc->setIsLoadIndividual(sIsLoadIndividual);
    }
}

InstanciateItemCallback CreateWaveArchiveFunc(bool clear)
{
    return CreateItemFunc(clear, []() -> Item* { return new WaveArchive(); }, &WaveArchiveCreatePropertiesCallback);
}

void DrawWaveArchivesUI()
{
    static SortState sSortState;
    
    DrawSortToolbar(sSortState);
    DrawAllItemsUI("Wave Archive", sBfsar.getWaveArchiveList(),
        &CreateWaveArchiveFunc, nullptr, nullptr, GetItemFilterCallback(),
        false, nullptr, sSortState.mode, sSortState.ascending
    );
}

void DrawWaveArchivePropertiesUI()
{
    WaveArchive* warc = static_cast<WaveArchive*>(sSelectedItem);

    {
        bool isLoadIndividual = warc->getIsLoadIndividual();
        if (ImGui::Checkbox("Load Individually", &isLoadIndividual))
        {
            warc->setIsLoadIndividual(isLoadIndividual);
            SetUnsavedChanges(true);
        }
    }
}
