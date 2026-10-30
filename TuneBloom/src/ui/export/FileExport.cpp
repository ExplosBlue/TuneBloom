#include <ui/export/Export.h>

#include <ui/bank/BankBundle.h>
#include <ui/widgets/Dialogs.h>
#include <ui/export/ExportInternal.h>
#include <ui/file/File.h>
#include <ui/file/FileDialogs.h>
#include <ui/project/FileLists.h>
#include <ui/items/ItemList.h>
#include <messages/Messages.h>
#include <bfsar/model/MessageLog.h>
#include <ui/items/Selection.h>
#include <ui/file/UnsavedChanges.h>
#include <ui/widgets/Widgets.h>

#include <imgui/imgui_internal.h>

#include <aal/AudioAsset.h>
#include <aal/AudioResource.h>
#include <bfsar/files/BankFile.h>
#include <bfsar/io/FileIo.h>
#include <bfsar/player/SoundPlayer.h>
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
static bool sPendingActiveFileWav = false;

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

bool CanExportActiveFileWav()
{
    Document* document = ActiveDocument();

    return document && !document->isPlayerOnly() && document->playableFile().isValid();
}

void RequestExportActiveFileWav()
{
    sPendingActiveFileWav = true;
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
    sPendingActiveFileWav = false;
    CancelMidiExport();
}

void ShowExportResult(const char* message)
{
    sExportResultMessage.copy(message);
    sShowExportResult = true;
}

void ShowWavExportResult(bool exported)
{
    if (!exported)
        MessageLog::instance()->addPopup({ messages::exporting::cWavWriteFailed, nullptr });
}

void ShowWavBatchExportResult(s32, s32 failureCount)
{
    if (failureCount > 0)
        MessageLog::instance()->addPopup({ sead::FormatFixedSafeString<64>(failureCount == 1 ? messages::exporting::cWavsWriteFailedOneFormat : messages::exporting::cWavsWriteFailedFormat, failureCount), nullptr });
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

    const InnerFileFormatInfo& format = GetInnerFileFormat(ActiveSoundArchive().getFormat(), kind);
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
    ExportItemsToFiles(&sPendingWavesToWav, { cWavExtension, messages::filedialog::cFilterWave, messages::common::cWave, messages::exporting::cSelectWavFolder },
        [](WaveFile* wave, const sead::SafeString& path)
        {
            return wave->writeWavFile(path);
        },
        &ShowWavExportResult, &ShowWavBatchExportResult);
}

static bool WritePlayableWav(const PlayableFile& file, const sead::SafeString& path)
{
    if (file.stream.isValid())
        return ExportLooseStreamToWav(*file.stream.stream, path.cstr());

    if (file.wave)
        return file.wave->writeWavFile(path);

    if (!file.asset)
        return false;

    file.asset->getResource()->loadStreamFile(file.asset);

    return file.asset->writeWavFile(path);
}

static void ExportActiveFileWav()
{
    if (!sPendingActiveFileWav)
        return;

    sPendingActiveFileWav = false;

    if (!CanExportActiveFileWav())
        return;

    Document* document = ActiveDocument();
    const PlayableFile file = document->playableFile();

    sead::FixedSafeString<SoundPlayer::cPlaybackErrorLength> playbackError;

    if (file.asset && file.asset->getPlaybackError(&playbackError))
    {
        MessageLog::instance()->addPopup({ playbackError, file.asset });
        return;
    }

    const std::string source = document->filePath().isEmpty() ? document->displayName() : std::string(document->filePath().cstr());
    const std::string defaultPath = MakeSiblingPath(source, cWavExtension);

    sead::FixedSafeString<512> path;

    if (!SaveFileDialogWithExtension(&path, messages::filedialog::cFilterWave, cWavExtension, defaultPath.c_str()))
        return;

    const bool isWritten = WritePlayableWav(file, path);

    ShowWavExportResult(isWritten);

    if (isWritten)
        ShowExportedFiles(path);
}

static void ImportPendingSequenceFile()
{
    if (!sPendingSequenceImport)
        return;

    sPendingSequenceImport = false;

    Bfsar& archive = ActiveSoundArchive();
    const InnerFileFormatInfo& format = GetInnerFileFormat(archive.getFormat(), InnerFileKind::Sequence);
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
    fileio::GetFileStem(&fileName, path);

    SequenceFile* sequence = new SequenceFile();
    sequence->setArchive(&archive);
    sequence->setEnableName(true);
    sequence->getName() = fileName;
    sequence->setFormat(archive.getFormat());
    sequence->setVersion(archive.getVersionForBfseq());

    if (sequence->read(fileData))
    {
        sequence->setFormat(archive.getFormat());
        AddFileToList(archive.getSequenceFileList(), sequence);
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
    ExportActiveFileWav();

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
