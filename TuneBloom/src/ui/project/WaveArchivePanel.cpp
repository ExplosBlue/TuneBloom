#include <ui/project/ItemViews.h>

#include <ui/widgets/Dialogs.h>
#include <ui/file/File.h>
#include <ui/items/ItemList.h>
#include <ui/widgets/ListFilter.h>
#include <messages/Messages.h>
#include <ui/widgets/Properties.h>
#include <ui/items/Selection.h>
#include <ui/file/UnsavedChanges.h>
#include <ui/widgets/Widgets.h>

// Wave Archives

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
        DialogRowGap();
        ImGui::Checkbox(messages::wavearchive::cLoadIndividually, &sIsLoadIndividual);
        DetailTooltip(messages::wavearchive::cLoadIndividuallyDetail);
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
    DrawTabFilterBar();
    DrawAllItemsUI("Wave Archive", messages::common::cWaveArchive,ActiveSoundArchive().getWaveArchiveList(),
        &CreateWaveArchiveFunc, nullptr, nullptr, GetItemFilterCallback()
    );
}

void DrawWaveArchivePropertiesUI()
{
    WaveArchive* warc = static_cast<WaveArchive*>(sSelectedItem);

    if (!BeginPropertyBox("WaveArchiveLoad"))
        return;

    bool isLoadIndividual = warc->getIsLoadIndividual();
    if (DrawPanelHeader(messages::wavearchive::cLoadIndividually, &isLoadIndividual, messages::wavearchive::cLoadIndividuallyDetail))
    {
        warc->setIsLoadIndividual(isLoadIndividual);
        SetUnsavedChanges(true);
    }

    EndPropertyBox();
}
