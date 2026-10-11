#include <ui/export/Export.h>

#include <ui/bank/BankBundle.h>
#include <ui/widgets/Dialogs.h>
#include <ui/export/ExportInternal.h>
#include <ui/file/File.h>
#include <ui/file/FileDialogs.h>
#include <ui/project/FileLists.h>
#include <ui/items/ItemList.h>
#include <ui/Messages.h>
#include <ui/app/PopupMgr.h>
#include <ui/items/Selection.h>
#include <ui/file/UnsavedChanges.h>
#include <ui/widgets/Widgets.h>

#include <imgui/imgui_internal.h>

#include <bfsar/files/BankFile.h>
#include <bfsar/io/FileIo.h>
#include <midi/InstBankExporter.h>

#include <filedevice/seadFileDeviceMgr.h>
#include <stream/seadFileDeviceStream.h>

#include <vector>

static std::vector<SequenceFile*> sPendingSequenceFiles;
static std::vector<WaveFile*> sPendingWaveFiles;
static std::vector<WaveFile*> sPendingWavesToWav;
static std::vector<BankFile*> sPendingBankSf2;
static std::vector<BankFile*> sPendingBankDls;
static bool sPendingSequenceImport = false;

static bool sShowExportResult = false;
static sead::FixedSafeString<128> sExportResultMessage;

void RequestExportSequenceFiles(const std::vector<SequenceFile*>& files)
{
    sPendingSequenceFiles = files;
}

void RequestImportSequenceFile()
{
    sPendingSequenceImport = true;
}

void RequestExportWaveFiles(const std::vector<WaveFile*>& files)
{
    sPendingWaveFiles = files;
}

void RequestExportWaveFilesToWav(const std::vector<WaveFile*>& files)
{
    sPendingWavesToWav = files;
}

void RequestExportBankSf2(const std::vector<BankFile*>& files)
{
    sPendingBankSf2 = files;
}

void RequestExportBankDls(const std::vector<BankFile*>& files)
{
    sPendingBankDls = files;
}

void CancelFileExports()
{
    sPendingSequenceFiles.clear();
    sPendingWaveFiles.clear();
    sPendingWavesToWav.clear();
    sPendingBankSf2.clear();
    sPendingBankDls.clear();
    sPendingSequenceImport = false;
    CancelMidiExport();
}

void ShowExportResult(const char* message)
{
    sExportResultMessage.copy(message);
    sShowExportResult = true;
}

void ShowBatchExportResult(s32 total, s32 failureCount, const char* formatName)
{
    if (failureCount > 0)
        sExportResultMessage.format(messages::exporting::cFilesPartFormat, total - failureCount, total, formatName, failureCount);
    else
        sExportResultMessage.format(messages::exporting::cFilesExportedFormat, total, formatName);

    sShowExportResult = true;
}

void HideExportResult()
{
    sShowExportResult = false;
}

template <typename T>
static bool WriteInnerFile(const T* file, const char* path)
{
    fileio::StagedFile staged;
    if (!staged.open(path))
        return false;

    {
        sead::FileDeviceWriteStream stream(staged.getHandle(), sead::Stream::Modes::eBinary);
        file->write(staged.getHandle(), &stream, sead::Endian::eBig, true);
    }

    return staged.commit();
}

template <typename T>
static void ExportInnerFiles(std::vector<T*>* pending, InnerFileKind kind, const char* fallbackName)
{
    if (pending->empty())
        return;

    const InnerFileFormatInfo& format = GetInnerFileFormat(sBfsar.getFormat(), kind);
    const char* formatName = format.displayName;
    const sead::FormatFixedSafeString<64> filterName(messages::filedialog::cFilterFileFormat, formatName, format.extension);

    ExportItemsToFiles(pending, { format.extension, filterName.cstr(), fallbackName, messages::exporting::cSelectFolder },
        [](T* file, const sead::SafeString& path)
        {
            return WriteInnerFile(file, path.cstr());
        },
        [formatName](bool exported)
        {
            ShowExportResult(sead::FormatFixedSafeString<128>(exported ? messages::exporting::cFileExportedFormat : messages::exporting::cFileFailedFormat, formatName).cstr());
        },
        [formatName](s32 total, s32 failureCount)
        {
            ShowBatchExportResult(total, failureCount, formatName);
        });
}

static void ExportBankModels(std::vector<BankFile*>* pending, const char* ext, const char* formatName, const char* filterDesc, bool (*exportFn)(const sead::SafeString&, const BankFile&))
{
    if (pending->empty())
        return;

    const sead::FormatFixedSafeString<64> filterName("%s (*.%s)", filterDesc, ext);

    ExportItemsToFiles(pending, { ext, filterName.cstr(), messages::common::cBank, messages::exporting::cSelectFolder },
        [exportFn](BankFile* bank, const sead::SafeString& path)
        {
            return exportFn(path, *bank);
        },
        [formatName](bool exported)
        {
            ShowExportResult(sead::FormatFixedSafeString<128>(exported ? messages::exporting::cBankExportedFormat : messages::exporting::cBankFailedFormat, formatName).cstr());
        },
        [formatName](s32 total, s32 failureCount)
        {
            ShowBatchExportResult(total, failureCount, formatName);
        });
}

static void ExportPendingWavesToWav()
{
    ExportItemsToFiles(&sPendingWavesToWav, { "wav", messages::filedialog::cFilterWave, messages::common::cWave, messages::exporting::cSelectWavFolder },
        [](WaveFile* wave, const sead::SafeString& path)
        {
            return wave->writeWavFile(path);
        },
        [](bool exported)
        {
            if (!exported)
                PopupMgr::instance()->addPopup({ messages::exporting::cWavWriteFailed, nullptr });
        },
        [](s32, s32 failureCount)
        {
            if (failureCount > 0)
                PopupMgr::instance()->addPopup({ sead::FormatFixedSafeString<64>(failureCount == 1 ? messages::exporting::cWavsWriteFailedOneFormat : messages::exporting::cWavsWriteFailedFormat, failureCount), nullptr });
        });
}

static void ImportPendingSequenceFile()
{
    if (!sPendingSequenceImport)
        return;

    sPendingSequenceImport = false;

    const InnerFileFormatInfo& format = GetInnerFileFormat(sBfsar.getFormat(), InnerFileKind::Sequence);
    const sead::FormatFixedSafeString<64> filterName(messages::filedialog::cFilterFileFormat, format.displayName, format.extension);

    sead::FixedSafeString<512> path;
    if (!OpenFileDialogWithExtension(&path, filterName.cstr(), format.extension))
        return;

    sead::FileDevice* device = sead::FileDeviceMgr::instance()->findDevice("native");
    if (!device)
        return;

    sead::FileDevice::LoadArg arg;
    arg.path = path;
    u8* fileData = device->tryLoad(arg);

    if (!fileData)
        return;

    sead::FixedSafeString<256> fileName;
    GetFileStem(&fileName, path);

    SequenceFile* sequence = new SequenceFile();
    sequence->setEnableName(true);
    sequence->getName() = fileName;
    sequence->setFormat(sBfsar.getFormat());
    sequence->setVersion(sBfsar.getVersionForBfseq());

    if (sequence->read(fileData))
    {
        sequence->setFormat(sBfsar.getFormat());
        AddFileToList(sBfsar.getSequenceFileList(), sequence);
    }
    else
    {
        delete sequence;
    }

    device->unload(fileData);
}

void DrawFileExportDialogs()
{
    ExportInnerFiles(&sPendingSequenceFiles, InnerFileKind::Sequence, messages::common::cSequence);
    ExportInnerFiles(&sPendingWaveFiles, InnerFileKind::Wave, messages::common::cWave);
    ExportPendingBankBundles();
    ExportBankModels(&sPendingBankSf2, "sf2", messages::exporting::cFormatSf2, messages::exporting::cFormatSoundFont, &exportBankToSf2);
    ExportBankModels(&sPendingBankDls, "dls", messages::exporting::cFormatDls, messages::exporting::cFormatDls, &exportBankToDls);
    ExportPendingInstrument();

    if (!BeginInstrumentImport())
        return;

    DrawInstrumentImportDialog();
    ImportPendingSequenceFile();

    if (!BeginBankBundleImport())
        return;

    if (!DrawBankBundleImportDialog())
        return;

    ExportPendingWavesToWav();
    ExportPendingMidi();
}

void DrawExportResultPopup()
{
    if (!sShowExportResult)
        return;

    ImGui::OpenPopup(messages::exporting::cCompleteTitle);

    CenterNextDialog();

    if (BeginDialog(messages::exporting::cCompleteTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("%s", sExportResultMessage.cstr());

        BeginDialogFooter({messages::dialog::cOk});

        if (DialogButton(messages::dialog::cOk))
        {
            sShowExportResult = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}
