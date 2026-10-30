#include <ui/file/NewFileDialog.h>

#include <ui/app/Shortcuts.h>
#include <ui/browser/FilesPanel.h>
#include <ui/file/AudioResourceDocument.h>
#include <ui/file/DocumentMgr.h>
#include <ui/file/File.h>
#include <ui/file/LooseStreamDocument.h>
#include <ui/file/LooseWaveDocument.h>
#include <ui/file/UnsavedChanges.h>
#include <messages/Messages.h>
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
#include <scan/FileProbe.h>

enum class NewFileType
{
    SoundArchive,
    StreamFile,
    WaveFile,
    AudioResource
};

struct NewFileTypeInfo
{
    const char* name;
    BrowserFileKind kind;
};

static const NewFileTypeInfo cNewFileTypes[] = {
    { messages::common::cSoundArchive,   BrowserFileKind::SoundArchive  },
    { messages::sound::cStreamFileLabel, BrowserFileKind::Stream        },
    { messages::common::cWaveFile,       BrowserFileKind::Wave          },
    { messages::common::cAudioResource,  BrowserFileKind::AudioResource }
};

static constexpr s32 cNewFileTypeNum = IM_ARRAYSIZE(cNewFileTypes);

static constexpr ArchivePlatform cNewFileSystems[] = { ArchivePlatform::NX, ArchivePlatform::CAFE, ArchivePlatform::CTR };

static constexpr s32 cNewFileSystemNum     = IM_ARRAYSIZE(cNewFileSystems);
static constexpr s32 cNewResourceSystemNum = 2;
static constexpr s32 cNewFileFormNum       = 2;
static constexpr s32 cNewSwitchOnlyForm    = 1;

static constexpr f32 cNewFilePageWidthEm = 28.0f;
static constexpr f32 cNewFileHeightEm    = 9.5f;
static constexpr f32 cNewFileResultGapEm = 0.8f;

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

static bool sNeedsNewFileFormat = false;
static Document* sNewLooseDocument = nullptr;

static s32 sNewFileType = 0;
static s32 sNewFileSystem = 0;
static s32 sNewFileForm = 0;
static u32 sNewFileVersion = 0;
static bool sNewFileStringTable = true;
static s32 sNewBarsVersion = 0;
static AudioAssetKind sNewBarsAudio = AudioAssetKind::Fwav;
static s32 sNewSarcMeta = 0;
static s32 sNewLooseTargetKey = -1;
static InnerFileTarget sNewLooseTarget;

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

static const ArchiveFormatInfo& GetNewSystemInfo(s32 system)
{
    for (const ArchiveFormatInfo& info : cFormatTable)
    {
        if (info.platform == cNewFileSystems[system])
            return info;
    }

    return cFormatTable[0];
}

static s32 GetNewFileSystemNum(NewFileType type)
{
    return type == NewFileType::AudioResource ? cNewResourceSystemNum : cNewFileSystemNum;
}

static void SetNewFileSystem(s32 system)
{
    sNewFileSystem = system;
    sNewFileVersion = GetNewSystemInfo(system).defaultVersion;
}

static bool IsNewSwitchSystem()
{
    return cNewFileSystems[sNewFileSystem] == ArchivePlatform::NX;
}

static bool HasNewFileForms(NewFileType type)
{
    return (type == NewFileType::StreamFile || type == NewFileType::WaveFile) && IsNewSwitchSystem();
}

static bool IsNewSwitchOnlyForm(NewFileType type)
{
    return HasNewFileForms(type) && sNewFileForm == cNewSwitchOnlyForm;
}

static const InnerFileTarget& GetNewLooseTarget(NewFileType type)
{
    const s32 key = static_cast<s32>(type) * cNewFileSystemNum + sNewFileSystem;

    if (key != sNewLooseTargetKey)
    {
        const ArchiveFormatInfo& info = GetNewSystemInfo(sNewFileSystem);

        sNewLooseTarget = Bfsar::GetNewFileTarget(info.format, info.platform, GetNewFileKind(type));
        sNewLooseTargetKey = key;
    }

    return sNewLooseTarget;
}

static Document* CreateLooseDocument(NewFileType type)
{
    if (type == NewFileType::AudioResource && !IsNewSwitchSystem())
        return AudioResourceDocument::CreateSarc(cNewSarcMetaForms[sNewSarcMeta]);

    if (type == NewFileType::AudioResource)
        return AudioResourceDocument::CreateBars(cNewBarsVersions[sNewBarsVersion], sNewBarsAudio);

    if (type == NewFileType::WaveFile && IsNewSwitchOnlyForm(type))
        return AudioResourceDocument::CreateBwav();

    const InnerFileTarget& target = GetNewLooseTarget(type);

    if (type == NewFileType::WaveFile)
        return LooseWaveDocument::Create(target);

    return LooseStreamDocument::Create(target, IsNewSwitchOnlyForm(type) ? Sound::StreamSoundInfo::StreamType::Opus : Sound::StreamSoundInfo::StreamType::NwStreamBinary);
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
    sNewFileForm = 0;
    sNewFileStringTable = true;
    sNewBarsVersion = 0;
    sNewBarsAudio = AudioAssetKind::Fwav;
    sNewSarcMeta = 0;

    SetNewFileSystem(0);
}

static void DrawNewFileSystemRow(NewFileType type)
{
    const char* labels[cNewFileSystemNum];

    for (s32 i = 0; i < cNewFileSystemNum; i++)
        labels[i] = GetNewSystemInfo(i).systemName;

    s32 system = sNewFileSystem;

    PropertyRow(messages::project::cPlatform);

    if (ComboScroll("##Platform", &system, labels, GetNewFileSystemNum(type)))
        SetNewFileSystem(system);
}

static void DrawNewFileFormRow(NewFileType type)
{
    const ArchiveFormatInfo& info = GetNewSystemInfo(sNewFileSystem);
    const char* switchOnlyName = type == NewFileType::StreamFile ? messages::stream::cTypeOpus : GetResourceFormatInfo(ResourceFormat::Bwav).displayName;
    const char* const labels[cNewFileFormNum] = { GetInnerFileDisplayName(info.format, GetNewFileKind(type)), switchOnlyName };

    DialogRow(messages::common::cFormat);
    ComboScroll("##Form", &sNewFileForm, labels, cNewFileFormNum);
}

static void DrawNewArchiveRows()
{
    DialogRow(messages::common::cVersion);
    DrawVersionUI(&sNewFileVersion, GetNewSystemInfo(sNewFileSystem).format, false);

    DialogRow(messages::project::cStringTable);
    DetailTooltip(messages::project::cStringTableDetail);

    ImGui::Checkbox("##IncludeStringTable", &sNewFileStringTable);
    DetailTooltip(messages::project::cStringTableDetail);
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
    sead::FixedSafeString<16> amtaVersion;
    FormatResourceVersion(&amtaVersion, aal::cAmtaVersion1);

    const sead::FormatFixedSafeString<32> amtaLabel(messages::resource::cMetadataVersionFormat, amtaVersion.cstr());
    const char* const metaLabels[cNewSarcMetaNum] = { amtaLabel.cstr(), messages::common::cNone };

    DialogRow(messages::resource::cMetadataTitle);
    DetailTooltip(messages::resource::cAudioFormatDetail);
    ComboScroll("##SarcMeta", &sNewSarcMeta, metaLabels, cNewSarcMetaDetails, cNewSarcMetaNum);
}

static void FormatNewFileResult(NewFileType type, sead::BufferedSafeString* name, sead::BufferedSafeString* facts)
{
    const ResourceFormatInfo& bars = GetResourceFormatInfo(ResourceFormat::Bars);
    const ResourceFormatInfo& bwav = GetResourceFormatInfo(ResourceFormat::Bwav);

    const char* extension = nullptr;
    const char* format = nullptr;
    sead::FixedSafeString<16> version;

    if (type == NewFileType::SoundArchive)
    {
        const ArchiveFormatInfo& info = GetNewSystemInfo(sNewFileSystem);

        extension = info.extension;
        format = info.fmtName;
        FormatInnerFileVersion(&version, sNewFileVersion, info.format);
    }
    else if (type == NewFileType::AudioResource)
    {
        extension = bars.extension;
        format = IsNewSwitchSystem() ? bars.displayName : messages::resource::cSarcFormat;
        FormatResourceVersion(&version, IsNewSwitchSystem() ? cNewBarsVersions[sNewBarsVersion] : sarc::File().version);
    }
    else if (IsNewSwitchOnlyForm(type) && type == NewFileType::StreamFile)
    {
        extension = cOpusExtension;
        format = messages::stream::cTypeOpus;
    }
    else if (IsNewSwitchOnlyForm(type))
    {
        extension = bwav.extension;
        format = bwav.displayName;
        FormatResourceVersion(&version, aal::Bwav().version);
    }
    else
    {
        const InnerFileTarget& target = GetNewLooseTarget(type);

        extension = GetInnerFileExtension(target.format, GetNewFileKind(type));
        format = GetInnerFileDisplayName(target.format, GetNewFileKind(type));
        FormatInnerFileVersion(&version, target.version, target.format);
    }

    name->format(messages::archive::cUntitledFileNameFormat, extension);

    if (version.isEmpty())
        facts->copy(format);
    else
        facts->format(messages::files::cFormatVersionFormat, format, version.cstr());
}

static void DrawNewFileResultRow(NewFileType type)
{
    sead::FixedSafeString<64> name;
    sead::FixedSafeString<64> facts;
    FormatNewFileResult(type, &name, &facts);

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const f32 gap = ImFloor(ImGui::GetFontSize() * cNewFileResultGapEm);
    const f32 lineY = ImFloor(pos.y + gap * 0.5f);

    ImGui::GetWindowDrawList()->AddLine(ImVec2(pos.x, lineY), ImVec2(pos.x + ImGui::GetContentRegionAvail().x, lineY), ImGui::GetColorU32(ImGuiCol_Separator));

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + gap);
    PropertyRow(messages::archive::cNewFileResult);

    ImGui::TextUnformatted(name.cstr());
    ImGui::SameLine();
    ImGui::TextDisabled("%s", facts.cstr());
}

static f32 CalcNewFileNavWidth()
{
    const f32 iconWidth = ImGui::CalcTextSize(ICON_LC_INFO " ").x;

    f32 nameWidth = 0.0f;

    for (const NewFileTypeInfo& info : cNewFileTypes)
        nameWidth = ImMax(nameWidth, ImGui::CalcTextSize(info.name).x);

    return ImCeil(ImGui::GetStyle().FramePadding.x * 2.0f + iconWidth + nameWidth);
}

static void DrawNewFileTypeNav()
{
    for (s32 i = 0; i < cNewFileTypeNum; i++)
    {
        ImGui::PushID(i);

        if (DialogNavRow(GetFileKindIcon(cNewFileTypes[i].kind), cNewFileTypes[i].name, sNewFileType == i))
            sNewFileType = i;

        ImGui::PopID();
    }

    if (sNewFileSystem >= GetNewFileSystemNum(static_cast<NewFileType>(sNewFileType)))
        SetNewFileSystem(0);
}

static void DrawNewFilePage(NewFileType type)
{
    DrawNewFileSystemRow(type);

    if (type == NewFileType::SoundArchive)
        DrawNewArchiveRows();
    else if (type == NewFileType::AudioResource && IsNewSwitchSystem())
        DrawNewBarsRows();
    else if (type == NewFileType::AudioResource)
        DrawNewSarcRows();
    else if (HasNewFileForms(type))
        DrawNewFileFormRow(type);

    DrawNewFileResultRow(type);
}

static void DrawNewFileBody()
{
    const f32 em = ImGui::GetFontSize();
    const f32 navWidth = CalcNewFileNavWidth();
    const ImVec2 size(navWidth + ImFloor(em * cNewFilePageWidthEm), ImFloor(em * cNewFileHeightEm));

    if (!BeginNavDialog("##NewFile", size, navWidth))
        return;

    DrawNewFileTypeNav();

    if (BeginNavDialogPage(sNewFileType))
        DrawNewFilePage(static_cast<NewFileType>(sNewFileType));

    EndNavDialog();
}

static void CreateNewFile()
{
    const NewFileType type = static_cast<NewFileType>(sNewFileType);

    if (type != NewFileType::SoundArchive)
    {
        CreateLooseFile(type);
        return;
    }

    const ArchiveFormatInfo& info = GetNewSystemInfo(sNewFileSystem);
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

    const bool keysFree = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows | ImGuiFocusedFlags_NoPopupHierarchy) && !ImGui::IsAnyItemActive();

    DrawNewFileBody();

    GroupBoxGap();
    BeginDialogFooter({messages::dialog::cCreate, messages::common::cCancel}, false);

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
