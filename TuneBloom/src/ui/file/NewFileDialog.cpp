#include <ui/file/NewFileDialog.h>

#include <ui/app/Shortcuts.h>
#include <ui/file/AudioResourceDocument.h>
#include <ui/file/DocumentMgr.h>
#include <ui/file/File.h>
#include <ui/file/LooseStreamDocument.h>
#include <ui/file/LooseWaveDocument.h>
#include <ui/file/UnsavedChanges.h>
#include <ui/Messages.h>
#include <ui/project/FileLists.h>
#include <ui/resource/ResourceViews.h>
#include <ui/stream/StreamTrackImport.h>
#include <ui/widgets/Dialogs.h>
#include <ui/widgets/Properties.h>
#include <ui/widgets/Widgets.h>

#include <libs/IconsLucide.h>

#include <aal/Amta.h>
#include <aal/AudioAsset.h>
#include <aal/Bars.h>
#include <aal/Bwav.h>
#include <core/Sarc.h>

enum class NewFileType
{
    SoundArchive,
    StreamFile,
    WaveFile,
    AudioResource
};

static const char* const cNewFileTypeLabels[] = {
    messages::common::cSoundArchive,
    messages::sound::cStreamFileLabel,
    messages::common::cWaveFile,
    messages::common::cAudioResource
};

static constexpr s32 cNewFileTypeNum = IM_ARRAYSIZE(cNewFileTypeLabels);

static constexpr u16 cNewBarsVersions[] = { aal::cBarsVersion12, aal::cBarsVersion11 };

static const char* const cNewBarsVersionDetails[] = {
    messages::resource::cBarsVersion12Detail,
    messages::resource::cBarsVersion11Detail
};

static constexpr s32 cNewBarsVersionNum = IM_ARRAYSIZE(cNewBarsVersions);

static constexpr bool cNewSarcMetaForms[] = { true, false };

static const char* const cNewSarcMetaDetails[] = {
    messages::resource::cSarcMetaDetail,
    messages::resource::cSarcNoMetaDetail
};

static constexpr s32 cNewSarcMetaNum = IM_ARRAYSIZE(cNewSarcMetaForms);
static constexpr s32 cNewSwitchPlatform = cFormatTableSize;
static constexpr s32 cNewSarcPlatform = cFormatTableSize + 1;
static constexpr s32 cNewPlatformNumMax = cFormatTableSize + 2;

static bool sNeedsNewFileFormat = false;
static Document* sNewLooseDocument = nullptr;

static s32 sNewFileType = 0;
static s32 sNewFilePlatform = 0;
static u32 sNewFileVersion = 0;
static bool sNewFileStringTable = true;
static s32 sNewBarsVersion = 0;
static AudioAssetKind sNewBarsAudio = AudioAssetKind::Fwav;
static s32 sNewSarcMeta = 0;
static s32 sNewLooseVersionKey = -1;
static sead::FixedSafeString<32> sNewLooseVersion;

void OpenNewFileDialog()
{
    sNeedsNewFileFormat = true;
}

void ForgetNewFileDocument(const Document* document)
{
    if (sNewLooseDocument == document)
        sNewLooseDocument = nullptr;
}

static InnerFileKind GetNewFileKind(NewFileType type)
{
    return type == NewFileType::StreamFile ? InnerFileKind::Stream : InnerFileKind::Wave;
}

static s32 GetNewFilePlatformNum(NewFileType type)
{
    if (type == NewFileType::SoundArchive)
        return cFormatTableSize;

    return type == NewFileType::AudioResource ? cNewPlatformNumMax : cNewSwitchPlatform + 1;
}

static bool IsNewSwitchFormat()
{
    return sNewFilePlatform == cNewSwitchPlatform;
}

static bool IsNewSarcFormat()
{
    return sNewFilePlatform == cNewSarcPlatform;
}

static const ArchiveFormatInfo& GetNewFilePlatformInfo()
{
    if (IsNewSwitchFormat())
        return *getFormatInfo(ArchiveFormat::BFSAR, ArchivePlatform::NX);

    return cFormatTable[sNewFilePlatform];
}

static s32 GetNewFilePlatformFirst(NewFileType type)
{
    return type == NewFileType::AudioResource ? cNewSwitchPlatform : 0;
}

static const char* GetNewSwitchFormatName(NewFileType type)
{
    if (type == NewFileType::StreamFile)
        return messages::stream::cTypeOpus;

    return GetResourceFormatInfo(type == NewFileType::AudioResource ? ResourceFormat::Bars : ResourceFormat::Bwav).displayName;
}

static Document* CreateLooseDocument(NewFileType type)
{
    if (type == NewFileType::AudioResource && IsNewSarcFormat())
        return AudioResourceDocument::CreateSarc(cNewSarcMetaForms[sNewSarcMeta]);

    if (type == NewFileType::AudioResource)
        return AudioResourceDocument::CreateBars(cNewBarsVersions[sNewBarsVersion], sNewBarsAudio);

    if (type == NewFileType::WaveFile && IsNewSwitchFormat())
        return AudioResourceDocument::CreateBwav();

    const ArchiveFormatInfo& info = GetNewFilePlatformInfo();
    const InnerFileTarget target = Bfsar::GetNewFileTarget(info.format, info.platform, GetNewFileKind(type));

    if (type == NewFileType::WaveFile)
        return LooseWaveDocument::Create(target);

    return LooseStreamDocument::Create(target, IsNewSwitchFormat() ? Sound::StreamSoundInfo::StreamType::Opus : Sound::StreamSoundInfo::StreamType::NwStreamBinary);
}

static void CreateLooseFile(NewFileType type)
{
    Document* document = CreateLooseDocument(type);

    AddNewDocument(document);

    const PlayableFile file = document->playableFile();

    if (file.wave)
        RequestWaveFileReplace(file.wave);
    else if (file.asset)
        RequestAudioAssetReplace(file.asset);
    else if (file.stream.isValid())
        RequestStreamTrackImport(file.stream);
    else
        return;

    sNewLooseDocument = document;
}

static bool IsLooseFileEmpty(const PlayableFile& file)
{
    if (file.wave)
        return file.wave->getChannels().isEmpty();

    if (file.asset)
        return file.asset->getInfo().sampleCount == 0;

    return file.stream.isValid() && file.stream.stream->getTrackList().isEmpty();
}

static void UpdateNewLooseFile()
{
    Document* document = sNewLooseDocument;

    if (!document)
        return;

    const PlayableFile file = document->playableFile();

    if (IsWaveFileReplacePending(file.wave) || IsAudioAssetReplacePending(file.asset) || IsStreamTrackImportPending(file.stream))
        return;

    sNewLooseDocument = nullptr;

    if (IsLooseFileEmpty(file))
        CloseDocument(document);
}

static void ResetNewFileDialog()
{
    sNewFileType = 0;
    sNewFilePlatform = 0;
    sNewFileVersion = cFormatTable[sNewFilePlatform].defaultVersion;
    sNewFileStringTable = true;
    sNewBarsVersion = 0;
    sNewBarsAudio = AudioAssetKind::Fwav;
    sNewSarcMeta = 0;
}

static void DrawNewFilePlatformRow(NewFileType type)
{
    sead::FixedSafeString<64> formatLabels[cNewPlatformNumMax];
    const char* labels[cNewPlatformNumMax];

    for (s32 i = 0; i < cFormatTableSize; i++)
    {
        const char* formatName = type == NewFileType::SoundArchive ? cFormatTable[i].fmtName : GetInnerFileDisplayName(cFormatTable[i].format, GetNewFileKind(type));

        formatLabels[i].format("%s (%s)", formatName, cFormatTable[i].systemName);
        labels[i] = formatLabels[i].cstr();
    }

    if (type != NewFileType::SoundArchive)
    {
        formatLabels[cNewSwitchPlatform].format("%s (%s)", GetNewSwitchFormatName(type), getFormatInfo(ArchiveFormat::BFSAR, ArchivePlatform::NX)->systemName);
        labels[cNewSwitchPlatform] = formatLabels[cNewSwitchPlatform].cstr();
    }

    if (type == NewFileType::AudioResource)
    {
        formatLabels[cNewSarcPlatform].format("%s (%s)", GetResourceFormatInfo(ResourceFormat::Bars).displayName, getFormatInfo(ArchiveFormat::BFSAR, ArchivePlatform::CAFE)->systemName);
        labels[cNewSarcPlatform] = formatLabels[cNewSarcPlatform].cstr();
    }

    if (sNewFilePlatform >= GetNewFilePlatformNum(type))
    {
        sNewFilePlatform = static_cast<s32>(getFormatInfo(ArchiveFormat::BFSAR, ArchivePlatform::NX) - cFormatTable);
        sNewFileVersion = cFormatTable[sNewFilePlatform].defaultVersion;
    }

    const s32 first = GetNewFilePlatformFirst(type);
    sNewFilePlatform = ImMax(sNewFilePlatform, first);

    s32 shownPlatform = sNewFilePlatform - first;

    DialogRow(messages::project::cPlatformTitle);

    if (!ComboScroll("##Platform", &shownPlatform, labels + first, GetNewFilePlatformNum(type) - first))
        return;

    sNewFilePlatform = shownPlatform + first;

    if (sNewFilePlatform < cFormatTableSize)
        sNewFileVersion = cFormatTable[sNewFilePlatform].defaultVersion;
}

static void DrawNewArchiveRows()
{
    DialogRow(messages::common::cVersion);
    DrawVersionUI(&sNewFileVersion, cFormatTable[sNewFilePlatform].format, false);

    DialogRow(messages::project::cStringTable);
    DetailTooltip(messages::project::cStringTableDetail);

    ImGui::Checkbox("##IncludeStringTable", &sNewFileStringTable);
    DetailTooltip(messages::project::cStringTableDetail);
}

static void FormatNewLooseFileVersion(NewFileType type)
{
    if (IsNewSwitchFormat() && type == NewFileType::StreamFile)
    {
        sNewLooseVersion = messages::common::cNoValue;
        return;
    }

    if (IsNewSwitchFormat())
    {
        FormatResourceVersion(&sNewLooseVersion, aal::Bwav().version);
        return;
    }

    const ArchiveFormatInfo& info = GetNewFilePlatformInfo();
    const InnerFileTarget target = Bfsar::GetNewFileTarget(info.format, info.platform, GetNewFileKind(type));

    FormatInnerFileVersion(&sNewLooseVersion, target.version, target.format);
}

static const sead::SafeString& GetNewLooseFileVersion(NewFileType type)
{
    const s32 key = static_cast<s32>(type) * cNewPlatformNumMax + sNewFilePlatform;

    if (key != sNewLooseVersionKey)
    {
        FormatNewLooseFileVersion(type);
        sNewLooseVersionKey = key;
    }

    return sNewLooseVersion;
}

static void DrawNewLooseFileRows(NewFileType type)
{
    bool stringTable = false;

    ImGui::BeginDisabled();

    DialogRow(messages::common::cVersion);
    ImGui::TextUnformatted(GetNewLooseFileVersion(type).cstr());

    DialogRow(messages::project::cStringTable);
    ImGui::Checkbox("##IncludeStringTable", &stringTable);

    ImGui::EndDisabled();
}

static void DrawNewBarsRows()
{
    sead::FixedSafeString<16> versionTexts[cNewBarsVersionNum];
    const char* versionLabels[cNewBarsVersionNum];

    for (s32 i = 0; i < cNewBarsVersionNum; i++)
    {
        FormatResourceVersion(&versionTexts[i], cNewBarsVersions[i]);
        versionLabels[i] = versionTexts[i].cstr();
    }

    DialogRow(messages::common::cVersion);
    ComboScroll("##BarsVersion", &sNewBarsVersion, versionLabels, cNewBarsVersionDetails, cNewBarsVersionNum);

    const bool isAudioFixed = cNewBarsVersions[sNewBarsVersion] != aal::cBarsVersion11;

    DrawBarsAudioFormatRow(&sNewBarsAudio, isAudioFixed ? messages::resource::cAudioFormatFixedDetail : nullptr, nullptr);
}

static void DrawNewSarcRows()
{
    sead::FixedSafeString<16> sarcVersion;
    FormatResourceVersion(&sarcVersion, sarc::File().version);

    sead::FixedSafeString<16> amtaVersion;
    FormatResourceVersion(&amtaVersion, aal::cAmtaVersion1);

    const sead::FormatFixedSafeString<32> amtaLabel(messages::resource::cMetadataVersionFormat, amtaVersion.cstr());
    const char* const metaLabels[cNewSarcMetaNum] = { amtaLabel.cstr(), messages::common::cNone };

    ImGui::BeginDisabled();
    DialogRow(messages::common::cVersion);
    ImGui::TextUnformatted(sarcVersion.cstr());
    ImGui::EndDisabled();

    DialogRow(messages::resource::cMetadataTitle);
    DetailTooltip(messages::resource::cAudioFormatDetail);
    ComboScroll("##SarcMeta", &sNewSarcMeta, metaLabels, cNewSarcMetaDetails, cNewSarcMetaNum);
}

static void DrawNewFileTypeBox()
{
    const f32 formWidth = ImFloor(ImGui::GetFontSize() * cDialogFormWidthEm);

    ImGui::Dummy(ImVec2(ImMax(formWidth, CalcGroupBoxWidth(CalcChoiceRowWidth(cNewFileTypeLabels, cNewFileTypeNum))), 0.0f));
    DialogRowGap();

    if (!BeginGroupBox(messages::common::cType))
        return;

    DialogRowGap();
    DrawChoiceRow(&sNewFileType, cNewFileTypeLabels, cNewFileTypeNum);

    const NewFileType type = static_cast<NewFileType>(sNewFileType);

    DrawNewFilePlatformRow(type);

    if (type == NewFileType::SoundArchive)
        DrawNewArchiveRows();
    else if (type == NewFileType::AudioResource && IsNewSarcFormat())
        DrawNewSarcRows();
    else if (type == NewFileType::AudioResource)
        DrawNewBarsRows();
    else
        DrawNewLooseFileRows(type);

    EndGroupBox();
}

static void CreateNewFile()
{
    const NewFileType type = static_cast<NewFileType>(sNewFileType);

    if (type != NewFileType::SoundArchive)
    {
        CreateLooseFile(type);
        return;
    }

    const ArchiveFormatInfo& info = cFormatTable[sNewFilePlatform];
    NewFile(info.format, info.platform);

    Bfsar& archive = ActiveSoundArchive();
    archive.setVersion(sNewFileVersion);
    archive.setIncludeStringTable(sNewFileStringTable);
    SetUnsavedChanges(true);
}

static void DrawNewFileFormatDialog()
{
    if (sNeedsNewFileFormat)
    {
        ImGui::OpenPopup("###NewFileFormat");
        sNeedsNewFileFormat = false;
    }

    CenterNextDialog();

    if (!BeginDialog(sead::FormatFixedSafeString<64>(ICON_LC_FILE " %s###NewFileFormat", messages::common::cNewFile).cstr(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    if (ImGui::IsWindowAppearing())
        ResetNewFileDialog();

    const bool keysFree = ImGui::IsWindowFocused() && !ImGui::IsAnyItemActive();

    DrawNewFileTypeBox();

    BeginDialogFooter({messages::dialog::cCreate, messages::common::cCancel});

    const bool create = DialogButton(messages::dialog::cCreate) || (keysFree && shortcuts::Pressed(shortcuts::Action::ConfirmSelection));
    const bool cancel = DialogButton(messages::common::cCancel) || (keysFree && shortcuts::Pressed(shortcuts::Action::CloseDialog));

    if (create || cancel)
        ImGui::CloseCurrentPopup();

    if (create)
        CreateNewFile();

    ImGui::EndPopup();
}

void DrawNewFileDialog()
{
    DrawNewFileFormatDialog();
    UpdateNewLooseFile();
}
