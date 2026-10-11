#include <ui/app/FileTransferMenu.h>

#include <ui/bank/BankBundle.h>
#include <ui/export/Export.h>
#include <ui/file/File.h>
#include <ui/project/ItemViews.h>
#include <ui/app/MenuBar.h>
#include <ui/Messages.h>
#include <ui/items/Selection.h>
#include <ui/sound/SoundViews.h>

#include <libs/IconsLucide.h>

#include <bfsar/files/BankFile.h>
#include <bfsar/files/SequenceFile.h>
#include <bfsar/model/Sound.h>
#include <bfsar/model/SoundSet.h>
#include <bfsar/files/WaveFile.h>

using InnerFileLabel = sead::FormatFixedSafeString<64>;

static InnerFileLabel GetInnerFileLabel(InnerFileKind kind)
{
    return InnerFileLabel(messages::mainmenu::cInnerFileFormat, GetInnerFileFormat(sBfsar.getFormat(), kind).displayName);
}

static void DrawSoundExportItems(Item* item)
{
    const std::vector<Sound*> sounds = CollectForAction<Sound>(item);
    const SoundExportCheck check = CheckSoundExport(sounds);

    if (MenuBarIconItem(ICON_LC_FILE_AUDIO, messages::mainmenu::cExportWav, nullptr, check.canExport, check.reason.cstr()))
        RequestExportSounds(sounds);

    if (check.onlySequence && MenuBarIconItem(ICON_LC_FILE_MUSIC, messages::mainmenu::cExportMidi, nullptr, check.canExport, check.reason.cstr()))
        RequestExportMidi(sounds);
}

static void DrawExportHint()
{
    MenuBarItem(messages::mainmenu::cExportNoSelection, nullptr, false, false);
}

static void DrawSoundSetExportItems(Item* item)
{
    if (static_cast<SoundSet*>(item)->getSoundSetType() != SoundSet::SoundSetType::Seq)
    {
        DrawExportHint();
        return;
    }

    const std::vector<SoundSet*> soundSets = CollectForAction<SoundSet>(item);

    if (MenuBarIconItem(ICON_LC_FILE_MUSIC, messages::mainmenu::cExportMidi, nullptr, CanExportSoundSetMidi(soundSets), messages::exporting::cSoundSetMidiNeedsSequences))
        RequestExportMidi(soundSets);
}

static void DrawWaveFileExportItems(Item* item)
{
    const std::vector<WaveFile*> waves = CollectForAction<WaveFile>(item);

    if (MenuBarIconItem(ICON_LC_FILE_OUTPUT, GetInnerFileLabel(InnerFileKind::Wave).cstr()))
        RequestExportWaveFiles(waves);

    if (MenuBarIconItem(ICON_LC_FILE_AUDIO, messages::mainmenu::cExportWav))
        RequestExportWaveFilesToWav(waves);
}

static void DrawSequenceFileExportItems(Item* item)
{
    const bool isCompiled = static_cast<SequenceFile*>(item)->isValid();

    if (MenuBarIconItem(ICON_LC_FILE_OUTPUT, GetInnerFileLabel(InnerFileKind::Sequence).cstr(), nullptr, isCompiled, messages::sequence::cNotCompiled))
        RequestExportSequenceFiles(CollectForAction<SequenceFile>(item));
}

static void DrawBankFileExportItems(const std::vector<BankFile*>& bankFiles)
{
    const bool canExport = !bankFiles.empty();
    const char* reason = messages::bank::cNoFileAttached;

    if (MenuBarIconItem(ICON_LC_PACKAGE, messages::mainmenu::cBankBundle, nullptr, canExport, reason))
        RequestExportBankBundles(bankFiles);

    if (MenuBarIconItem(ICON_LC_FILE_MUSIC, messages::mainmenu::cExportSf2, nullptr, canExport, reason))
        RequestExportBankSf2(bankFiles);

    if (MenuBarIconItem(ICON_LC_FILE_MUSIC, messages::mainmenu::cExportDls, nullptr, canExport, reason))
        RequestExportBankDls(bankFiles);
}

void DrawImportMenu()
{
    if (!BeginSubMenu(sead::FormatFixedSafeString<64>(ICON_LC_FILE_INPUT " %s", messages::mainmenu::cImport).cstr()))
        return;

    if (MenuBarIconItem(ICON_LC_FILE_INPUT, GetInnerFileLabel(InnerFileKind::Sequence).cstr()))
        RequestImportSequenceFile();

    if (MenuBarIconItem(ICON_LC_PACKAGE_OPEN, messages::mainmenu::cBankBundle))
        RequestImportBankBundle();

    ImGui::EndMenu();
}

void DrawExportMenu()
{
    if (!BeginSubMenu(sead::FormatFixedSafeString<64>(ICON_LC_FILE_OUTPUT " %s", messages::mainmenu::cExport).cstr()))
        return;

    Item* item = sBfsar.isOpen() ? GetActiveSelection() : nullptr;

    switch (item ? item->getItemType() : Item::ItemType::Invalid)
    {
        case Item::ItemType::Sound:
            DrawSoundExportItems(item);
            break;

        case Item::ItemType::SoundSet:
            DrawSoundSetExportItems(item);
            break;

        case Item::ItemType::WaveFile:
            DrawWaveFileExportItems(item);
            break;

        case Item::ItemType::SequenceFile:
            DrawSequenceFileExportItems(item);
            break;

        case Item::ItemType::BankFile:
            DrawBankFileExportItems(CollectForAction<BankFile>(item));
            break;

        case Item::ItemType::Bank:
            DrawBankFileExportItems(CollectBankFilesFromBanks(item));
            break;

        default:
            DrawExportHint();
            break;
    }

    ImGui::EndMenu();
}
