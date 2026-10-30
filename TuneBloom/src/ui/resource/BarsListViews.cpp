#include <ui/resource/BarsListViews.h>

#include <aal/BarsListFile.h>
#include <scan/FileProbe.h>
#include <ui/browser/FilesPanel.h>
#include <ui/file/BarsListDocument.h>
#include <ui/file/FileDrag.h>
#include <ui/file/LooseFileInfo.h>
#include <ui/file/OpenQueue.h>
#include <ui/file/SystemShell.h>
#include <ui/file/UnsavedChanges.h>
#include <ui/items/ItemColumns.h>
#include <ui/items/ItemList.h>
#include <ui/settings/UiColors.h>
#include <ui/widgets/ContextMenu.h>
#include <ui/widgets/Knob.h>
#include <ui/widgets/ListFilter.h>
#include <ui/widgets/Properties.h>
#include <ui/widgets/Widgets.h>
#include <messages/Messages.h>

#include <bfsar/model/ArchiveFormat.h>
#include <core/FileName.h>
#include <libs/IconsLucide.h>

#include <imgui/imgui.h>

#include <filesystem>
#include <string>

const FoundBars* FindEntryBars(const Item* item)
{
    const BarsListFile* file = FindActiveBarsList();

    return file ? file->findBars(item) : nullptr;
}

static void OpenEntryBars(Item* item)
{
    if (const FoundBars* found = FindEntryBars(item))
        openqueue::Push(found->path.c_str());
}

static void BarsListRowActionFunc(Item* item)
{
    if (DrawRowOpenButton(item->getId(), messages::barslist::cOpenBars, FindEntryBars(item) ? nullptr : messages::barslist::cNotFoundDetail))
        OpenEntryBars(item);
}

static void BarsListContextMenuFunc(Item* item)
{
    if (!item)
        return;

    const FoundBars* found = FindEntryBars(item);

    ContextMenuSeparator();

    if (ContextMenuItem(GetFileKindIcon(BrowserFileKind::AudioResource), messages::common::cOpen, found != nullptr, messages::barslist::cNotFoundDetail))
        OpenEntryBars(item);

    if (ContextMenuItem(ICON_LC_FOLDER_SEARCH, messages::recent::cShowInFileManager, found != nullptr, messages::barslist::cNotFoundDetail))
        ShowInFileManager(found->path);
}

static Item* InstanciateBarsListEntry()
{
    BarsListFile* file = FindActiveBarsList();

    return file ? file->createEntry(GetCreateItemName()) : nullptr;
}

static InstanciateItemCallback CreateBarsListEntryFunc(bool clear)
{
    return CreateItemFunc(clear, &InstanciateBarsListEntry, nullptr);
}

static std::string GetDroppedBarsName(const std::string& path)
{
    std::filesystem::path name = std::filesystem::path(path).filename();

    if (IsZstdPath(name.string().c_str()))
        name = name.stem();

    return filename::HasExactExtension(name.string(), cBarsExtension) ? name.string() : std::string();
}

static bool DropBarsFiles(const FileDrag& drag, bool isDropped)
{
    BarsListFile* file = FindActiveBarsList();
    bool accepts = false;

    for (const std::string& path : drag.paths)
    {
        const sead::FixedSafeString<256> name(GetDroppedBarsName(path).c_str());

        if (!file || !BarsListFile::IsValidEntryName(name) || !file->isUniqueEntryName(name, nullptr))
            continue;

        accepts = true;

        if (isDropped)
            file->getEntryList().pushBack(file->createEntry(name));
    }

    if (isDropped && accepts)
    {
        file->renumber();
        SetUnsavedChanges(true);
    }

    return accepts;
}

LooseFileFormat GetBarsListFormat(const BarsListFile& file)
{
    LooseFileFormat format;
    format.name = messages::barslist::cFormatName;
    format.extension = cBarsListExtension;
    format.version.format("%u", file.getSavedList().version);
    format.endian = file.getSavedList().isBigEndian ? sead::Endian::eBig : sead::Endian::eLittle;

    return format;
}

void DrawBarsListUI(BarsListFile& file)
{
    u32 columnCount = 0;
    const ItemColumn* columns = GetItemColumns(Item::ItemType::BarsListEntry, &columnCount);

    static const sead::FormatFixedSafeString<64> sAddAction(messages::item::cAddDialogTitleFormat, messages::barslist::cEntry);

    SetNextItemListColumns(columns, columnCount);
    SetNextItemListAction(&BarsListRowActionFunc);
    SetNextItemListOpen(&OpenEntryBars);
    SetNextItemListRemoveText(messages::common::cRemoveFromList, messages::barslist::cRemoveFromListFormat, false);
    SetNextItemListFileDrop(&DropBarsFiles, sAddAction.cstr());
    SetNextItemListDeselectable();

    DrawAllItemsUI("BARS File", messages::barslist::cEntry, file.getEntryList(), &CreateBarsListEntryFunc, nullptr, &BarsListContextMenuFunc, GetItemFilterCallback());
}

void DrawBarsListInfoUI(const BarsListFile& file)
{
    if (!BeginPropertyBox("BarsListFile"))
        return;

    RegionHeader(messages::common::cFile);
    DrawLooseFileRows(GetBarsListFormat(file));

    PropertyRow(messages::barslist::cListName, messages::barslist::cListNameDetail);
    ImGui::TextUnformatted(file.getListName().c_str());

    PropertyRow(messages::barslist::cEntries);
    ImGui::Text(messages::barslist::cFoundFormat, file.countFound(), static_cast<u32>(file.getEntryList().size()));

    EndPropertyBox();
}

void DrawBarsListEntryPropertiesUI(BarsListEntry* entry)
{
    DrawItemPropertiesUI(true);

    if (!BeginPropertyBox("BarsListEntry"))
        return;

    const FoundBars* found = FindEntryBars(entry);

    RegionHeader(messages::barslist::cEntry);

    PropertyRow(messages::barslist::cLocation);

    if (found)
        ImGui::TextUnformatted(found->location.c_str());
    else
        ImGui::TextColored(uicolors::Warning(), "%s", messages::common::cNotFound);

    if (found)
    {
        sead::FixedSafeString<32> size;
        FormatItemSize(&size, found->size);

        PropertyRow(messages::common::cSize);
        ImGui::TextUnformatted(size.cstr());
    }

    EndPropertyBox();
}
