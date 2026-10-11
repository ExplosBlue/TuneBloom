#define IMGUI_DEFINE_MATH_OPERATORS
#include <ui/project/FileLists.h>

#include <ui/bank/BankBundle.h>
#include <ui/widgets/ContextMenu.h>
#include <ui/export/Export.h>
#include <ui/file/File.h>
#include <ui/file/FileWindow.h>
#include <ui/items/ItemList.h>
#include <ui/widgets/ListFilter.h>
#include <ui/Messages.h>
#include <ui/items/Selection.h>
#include <ui/file/UnsavedChanges.h>
#include <ui/widgets/Widgets.h>

#include <libs/IconsLucide.h>

#include <bfsar/files/BankFile.h>

void AddFileToList(Item::List& list, Item* file)
{
    Item* insertAfter = GetInsertAfterItem();

    if (insertAfter)
        insertAfter->insertBack(file);
    else
        list.pushBack(file);

    ClearInsertAfterItem();
    sBfsar.updateList(list);
    SetUnsavedChanges(true);
    SelectItem(file);
}

InstanciateItemCallback CreateSequenceFileFunc(bool clear)
{
    auto doCreate = []() -> Item*
    {
        SequenceFile* seq = new SequenceFile();
        seq->setEnableName(true);
        seq->getName() = "Sequence";
        return seq;
    };

    return doCreate;
}

static void SequenceFileRowActionFunc(Item* item)
{
    SequenceFile* seq = static_cast<SequenceFile*>(item);

    if (DrawRowOpenButton(seq->getId(), messages::item::cOpenFileTip))
    {
        OpenFileWindow(seq);
        SelectItem(seq);
    }
}

void DrawSequenceFilesUI()
{
    DrawTabFilterBar();

    SetNextItemListAction(&SequenceFileRowActionFunc);

    DrawAllItemsUI("Sequence File", messages::common::cSequenceFile,sBfsar.getSequenceFileList(),
        &CreateSequenceFileFunc, nullptr, &SequenceFileContextMenuFunc, GetItemFilterCallback(), true
    );
}

InstanciateItemCallback CreateBankFileFunc(bool clear)
{
    auto doCreate = []() -> Item*
    {
        BankFile* bank = new BankFile();
        bank->setEnableName(true);
        bank->getName() = "Bank";

        return bank;
    };

    return doCreate;
}

static void BankFileRowActionFunc(Item* item)
{
    BankFile* bank = static_cast<BankFile*>(item);

    if (DrawRowOpenButton(bank->getId(), messages::item::cOpenFileTip))
        OpenFileWindow(bank);
}

void DrawBankFilesUI()
{
    DrawTabFilterBar();
    SetNextItemListAction(&BankFileRowActionFunc);
    DrawAllItemsUI(
        "Bank File",
        messages::common::cBankFile,
        sBfsar.getBankFileList(),
        &CreateBankFileFunc,
        nullptr,
        &BankFileContextMenuFunc, 
        GetItemFilterCallback(), 
        true
    );
}

void SequenceFileContextMenuFunc(Item* item)
{
    ContextMenuSeparator();

    if (ContextMenuItem(ICON_LC_FILE_INPUT, messages::common::cImport))
        RequestImportSequenceFile();

    if (!item)
        return;

    const bool isCompiled = static_cast<SequenceFile*>(item)->isValid();

    if (ContextMenuItem(ICON_LC_FILE_OUTPUT, messages::common::cExport, isCompiled, messages::sequence::cNotCompiled))
        RequestExportSequenceFiles(CollectForAction<SequenceFile>(item));
}

void DrawBankFileMenuItems(bool hasItem, const std::vector<BankFile*>& bankFiles)
{
    const bool canExport = !bankFiles.empty();
    const char* reason = messages::bank::cNoFileAttached;

    ContextMenuSeparator();

    if (ContextMenuItem(ICON_LC_PACKAGE_OPEN, messages::menu::cImportBankBundle))
        RequestImportBankBundle();

    if (!hasItem)
        return;

    if (ContextMenuItem(ICON_LC_PACKAGE, messages::menu::cExportBankBundle, canExport, reason))
        RequestExportBankBundles(bankFiles);

    ContextMenuSeparator();

    if (ContextMenuItem(ICON_LC_FILE_MUSIC, messages::menu::cExportSf2, canExport, reason))
        RequestExportBankSf2(bankFiles);

    if (ContextMenuItem(ICON_LC_FILE_MUSIC, messages::menu::cExportDls, canExport, reason))
        RequestExportBankDls(bankFiles);
}

void BankFileContextMenuFunc(Item* item)
{
    DrawBankFileMenuItems(item != nullptr, CollectForAction<BankFile>(item));
}
