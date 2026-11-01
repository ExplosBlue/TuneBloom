#include <ui/sound/SoundViews.h>

#include <ui/widgets/Dialogs.h>
#include <ui/file/File.h>
#include <ui/file/FileDialogs.h>
#include <ui/items/ItemList.h>
#include <ui/items/ItemSelector.h>
#include <messages/Messages.h>
#include <ui/widgets/Properties.h>
#include <ui/stream/StreamTrackImport.h>
#include <ui/settings/UiColors.h>
#include <ui/wave/WaveImportPanel.h>
#include <ui/widgets/Widgets.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_internal.h>

#include <bfsar/model/Sound.h>
#include <bfsar/files/WaveFile.h>
#include <bfsar/io/FileIo.h>

#include <filedevice/seadPath.h>

#include <cstdio>
#include <filesystem>
#include <string>

static constexpr s32 cSourceImport   = 0;
static constexpr s32 cSourceExisting = 1;
static constexpr s32 cSourceNone     = 2;

static constexpr s32 cSourceBoxRowNum = 3;

static constexpr const char* cDefaultStreamDir = "stream";

static constexpr f32 cImportPanelMinHeight = 300.0f;
static constexpr f32 cImportPanelMargin    = 320.0f;
static constexpr f32 cStreamPanelMargin    = 420.0f;

static bool sTypeLocked = false;
static Sound::SoundType sSoundType = Sound::SoundType::Wave;
static Item* sPlayer = nullptr;
static sead::FixedSafeString<1024> sWarning;

static s32 sWaveSource = cSourceImport;
static Item* sWaveFile = nullptr;
static WaveFile* sImportedWave = nullptr;
static WaveFile::RiffWaveInfo sImportInfo;
static WaveFile::Encoding sImportEncoding = WaveFile::Encoding::DspAdpcm;
static sead::FixedSafeString<512> sImportName;

static s32 sStreamSource = cSourceImport;
static sead::FixedSafeString<512> sStreamPath;
static sead::FixedSafeString<512> sNewStreamPath;
static bool sNewStreamPathEdited = false;

static Item* sSequenceFile = nullptr;
static Item* sBank = nullptr;

static bool ListHasItem(const Item::List& list, const Item* target)
{
    for (auto it = list.robustBegin(); it != list.robustEnd(); ++it)
    {
        if (it->val() == target)
            return true;
    }

    return false;
}

static Item* FirstItem(const Item::List& list)
{
    for (auto it = list.robustBegin(); it != list.robustEnd(); ++it)
        return it->val();

    return nullptr;
}

static void SuggestNameFromPath(const sead::SafeString& path)
{
    sead::FixedSafeString<512> name;
    fileio::GetFileStem(&name, path);

    SuggestCreateItemName(name);
}

static void ClearImportWave()
{
    sImportInfo.clear();
    sImportName.clear();
    sImportEncoding = WaveFile::Encoding::DspAdpcm;
    ResetWaveImport();
}

static void ResetSoundCreate()
{
    Bfsar& archive = ActiveSoundArchive();

    if (!ListHasItem(archive.getPlayerList(), sPlayer))
        sPlayer = FirstItem(archive.getPlayerList());

    sWaveFile = nullptr;
    sImportedWave = nullptr;
    ClearImportWave();

    sStreamPath.clear();
    sNewStreamPath.clear();
    sNewStreamPathEdited = false;
    ResetStreamCreateImport();

    sSequenceFile = nullptr;
    sBank = nullptr;
}

static void MakeDefaultStreamPath(sead::BufferedSafeString* out, const sead::SafeString& name)
{
    Bfsar& archive = ActiveSoundArchive();

    out->clear();

    if (name.isEmpty())
        return;

    sead::FixedSafeString<512> dir(cDefaultStreamDir);

    for (const Item* item : archive.getSoundList())
    {
        const Sound* sound = static_cast<const Sound*>(item);

        if (sound->getSoundType() != Sound::SoundType::Strm || sound->getStreamSoundInfo().getPath().isEmpty())
            continue;

        dir.copy(sound->getStreamSoundInfo().getPath().cstr());
        dir.trim(ImMax(dir.rfindIndex("/"), 0));
        break;
    }

    const char* extension = GetInnerFileFormat(archive.getFormat(), InnerFileKind::Stream).extension;

    if (dir.isEmpty())
        out->format("%s.%s", name.cstr(), extension);
    else
        out->format("%s/%s.%s", dir.cstr(), name.cstr(), extension);
}

static bool IsStreamPathUsed(const sead::SafeString& path)
{
    Bfsar& archive = ActiveSoundArchive();

    for (const Item* item : archive.getSoundList())
    {
        const Sound* sound = static_cast<const Sound*>(item);

        if (sound->getSoundType() == Sound::SoundType::Strm && sound->getStreamSoundInfo().getPath() == path)
            return true;
    }

    const sead::SafeString* archivePaths[2] = { &archive.getFilePath(), &archive.getLoadedArchivePath() };

    for (const sead::SafeString* archivePath : archivePaths)
    {
        sead::FixedSafeString<512> dir;

        if (!Bfsar::GetStreamFolder(*archivePath, &dir))
            continue;

        sead::FormatFixedSafeString<1024> candidate("%s/%s", dir.cstr(), path.cstr());
        std::error_code error;

        if (std::filesystem::exists(candidate.cstr(), error) && !error)
            return true;
    }

    return false;
}

static void PickImportWave()
{
    FileFilter filters[] = {
        { messages::filedialog::cFilterWave, "*.wav" }
    };

    sead::FixedSafeString<512> path;

    if (!OpenFileDialog(&path, nullptr, 1, filters))
        return;

    WaveFile::RiffWaveInfo info;
    info.path = path;

    if (!WaveFile::readRiffWavInfo(&info))
        return;

    ClearImportWave();
    sImportInfo = info;
    fileio::GetFileStem(&sImportName, sImportInfo.path);

    SuggestCreateItemName(sImportName);
}

static WaveFile* ImportPickedWave()
{
    WaveFile* wave = CreateImportedArchiveWave(&sImportInfo, sImportEncoding, sImportName);

    if (!wave)
        ClearImportWave();

    return wave;
}

struct SoundTypeEntry
{
    Sound::SoundType type;
    const char* icon;
    const char* label;
};

static const SoundTypeEntry cSoundTypes[] = {
    { Sound::SoundType::Wave, ICON_LC_AUDIO_LINES, messages::common::cWave },
    { Sound::SoundType::Strm, ICON_LC_DISC_3, messages::common::cStream },
    { Sound::SoundType::Seq, ICON_LC_MUSIC_3, messages::common::cSequence },
};

static constexpr s32 cSoundTypeNum = IM_ARRAYSIZE(cSoundTypes);

static bool BeginSourceBox(const char* title)
{
    if (sTypeLocked)
        return BeginGroupBox(title);

    sead::FixedSafeString<64> typeLabels[cSoundTypeNum];
    const char* labels[cSoundTypeNum];
    s32 selected = 0;

    for (s32 i = 0; i < cSoundTypeNum; i++)
    {
        typeLabels[i].format("%s %s", cSoundTypes[i].icon, cSoundTypes[i].label);
        labels[i] = typeLabels[i].cstr();

        if (cSoundTypes[i].type == sSoundType)
            selected = i;
    }

    if (!BeginGroupBoxWithChoice(messages::common::cType, &selected, labels, cSoundTypeNum))
        return false;

    sSoundType = cSoundTypes[selected].type;

    return true;
}

static void EndSourceBox(s32 rowNum)
{
    for (s32 i = rowNum; i < cSourceBoxRowNum && !sTypeLocked; i++)
    {
        DialogRowGap();
        ImGui::Dummy(ImVec2(0.0f, ImGui::GetFrameHeight()));
    }

    EndGroupBox();
}

static void DrawSourceChoiceUI(s32* source)
{
    const char* labels[] = { messages::sound::cWaveSourceImport, messages::source::cSelectExisting, messages::common::cNone };

    DrawChoiceRow(source, labels, IM_ARRAYSIZE(labels));
}

static void DrawWaveSourceUI()
{
    if (!BeginSourceBox(messages::common::cWaveFile))
        return;

    DialogRowGap();
    DrawSourceChoiceUI(&sWaveSource);

    if (sWaveSource == cSourceExisting)
    {
        ItemSelectorDialogRow(messages::common::cFile, ActiveSoundArchive().getWaveFileList(), &sWaveFile, false);
        EndSourceBox(2);
        return;
    }

    DialogRowGap();

    if (sWaveSource == cSourceNone)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", messages::sound::cWillHaveNoWaveFile);
        EndSourceBox(2);
        return;
    }

    if (ImGui::Button(sead::FormatFixedSafeString<64>("%s %s", ICON_LC_FOLDER_OPEN, messages::sound::cBrowseLabel).cstr()))
        PickImportWave();

    if (sImportName.isEmpty())
    {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", messages::sound::cNoImportFile);
    }

    EndSourceBox(2);

    if (sImportName.isEmpty())
        return;

    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 panelHeight = sead::Mathf::max(cImportPanelMinHeight, CalcWaveImportHeight() + style.WindowPadding.y * 2.0f);
    const f32 height = ImClamp(ImGui::GetMainViewport()->Size.y - cImportPanelMargin, cImportPanelMinHeight, panelHeight);
    const f32 width = CalcWaveImportWidth() + style.WindowPadding.x * 2.0f + (height < panelHeight ? style.ScrollbarSize : 0.0f);

    if (ImGui::BeginChild("###WaveImport", ImVec2(width, height), ImGuiChildFlags_Border))
        DrawWaveImportInfo(&sImportEncoding, &sImportInfo);

    ImGui::EndChild();
}

static void PickStreamImportFiles()
{
    FileFilter filters[] = {
        { messages::filedialog::cFilterWave, "*.wav" }
    };

    std::vector<std::string> paths;

    if (!OpenFilesDialog(&paths, nullptr, 1, filters))
        return;

    if (!StartStreamCreateImport(paths))
        return;

    ClearImportWave();
    SuggestCreateItemName(GetStreamCreateMainName());
}

static void DrawStreamImportSource(const char* browseLabel)
{
    if (TextButton(browseLabel))
        PickStreamImportFiles();

    const sead::SafeString& mainName = GetStreamCreateMainName();
    const u32 fileCount = GetStreamCreateFileCount() + 1;

    ImGui::SameLine();
    if (mainName.isEmpty())
        ImGui::TextDisabled("%s", messages::sound::cNoImportFile);
    else if (fileCount == 1)
        ImGui::TextUnformatted(mainName.cstr());
    else
        ImGui::Text(messages::sound::cImportFileCountFormat, fileCount);
}

static void DrawStreamExistingSource(const char* browseLabel)
{
    const bool hasArchiveDir = ActiveSoundArchive().hasArchiveDirectory();

    ImGui::BeginDisabled(!hasArchiveDir);

    if (TextButton(browseLabel) && PickStreamFilePath(&sStreamPath, nullptr))
        SuggestNameFromPath(sStreamPath);

    ImGui::EndDisabled();

    if (!hasArchiveDir)
        ImGui::SetItemTooltip("%s", messages::stream::cReloadNoArchiveFolder);

    ImGui::SameLine();
    if (sStreamPath.isEmpty())
    {
        ImGui::TextDisabled("%s", messages::sound::cNoStreamFile);
        return;
    }

    sead::FixedSafeString<512> fileName;
    sead::Path::getFileName(&fileName, sStreamPath);

    ImGui::TextUnformatted(fileName.cstr());
}

static void DrawStreamPathRow()
{
    const bool isExisting = sStreamSource == cSourceExisting;

    if (!isExisting && !sNewStreamPathEdited)
        MakeDefaultStreamPath(&sNewStreamPath, GetCreateItemName());

    const sead::FormatFixedSafeString<64> relativeDetail(messages::stream::cPathRelativeFormat, GetInnerFileExtension(ActiveSoundArchive().getFormat(), InnerFileKind::SoundArchive));
    const char* detail = isExisting ? relativeDetail.cstr() : messages::sound::cStreamNewPathDetail;

    DialogRow(messages::stream::cPathLabel);
    DetailTooltip(detail);

    ImGui::SetNextItemWidth(-FLT_MIN);

    if (isExisting)
    {
        ImGui::BeginDisabled();
        ImGui::InputText("###StreamPath", sStreamPath.getBuffer(), sStreamPath.getBufferSize(), ImGuiInputTextFlags_ReadOnly);
        ImGui::EndDisabled();
    }
    else if (ImGui::InputText("###NewStreamPath", sNewStreamPath.getBuffer(), sNewStreamPath.getBufferSize()))
    {
        sNewStreamPathEdited = !sNewStreamPath.isEmpty();
    }

    DetailTooltip(detail);
}

static void DrawStreamSourceUI()
{
    if (!BeginSourceBox(messages::sound::cStreamFileLabel))
        return;

    DialogRowGap();
    DrawSourceChoiceUI(&sStreamSource);

    DialogRowGap();

    const sead::FormatFixedSafeString<64> browseLabel("%s %s", ICON_LC_FOLDER_OPEN, messages::sound::cBrowseLabel);

    switch (sStreamSource)
    {
        case cSourceImport:
            DrawStreamImportSource(browseLabel.cstr());
            break;

        case cSourceExisting:
            DrawStreamExistingSource(browseLabel.cstr());
            break;

        default:
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", messages::sound::cWillHaveNoTracks);
            break;
    }

    DrawStreamPathRow();

    EndSourceBox(3);

    DrawStreamCreateMainEditor();

    if (sStreamSource != cSourceImport || !HasStreamCreateMainWave())
        return;

    const f32 maxHeight = sead::Mathf::max(cImportPanelMinHeight, ImGui::GetMainViewport()->Size.y - cStreamPanelMargin);

    if (CalcStreamCreateImportHeight() <= maxHeight)
    {
        DrawStreamCreateImport();
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, uicolors::cTransparent);

    if (ImGui::BeginChild("###StreamImport", ImVec2(CalcStreamCreateImportWidth() + ImGui::GetStyle().ScrollbarSize, maxHeight)))
        DrawStreamCreateImport();

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

static void DrawSequenceSourceUI()
{
    Bfsar& archive = ActiveSoundArchive();

    if (!BeginSourceBox(messages::common::cSequence))
        return;

    ItemSelectorDialogRow(messages::common::cSequenceFile, archive.getSequenceFileList(), &sSequenceFile, true);
    ItemSelectorDialogRow(messages::common::cBank, archive.getBankList(), &sBank, true);

    EndSourceBox(2);
}

static void DrawSoundCreateUI()
{
    ItemSelectorDialogRow(messages::common::cPlayer, ActiveSoundArchive().getPlayerList(), &sPlayer, false);

    DialogRowGap();

    switch (sSoundType)
    {
        case Sound::SoundType::Wave:
            DrawWaveSourceUI();
            break;

        case Sound::SoundType::Strm:
            DrawStreamSourceUI();
            break;

        case Sound::SoundType::Seq:
            DrawSequenceSourceUI();
            break;

        default:
            break;
    }

    WarningPopup("###SoundCreate", sWarning.cstr());
}

static void FindStreamCreateWarning(sead::BufferedSafeString* warning)
{
    if (sStreamSource == cSourceExisting)
    {
        if (sStreamPath.isEmpty())
            warning->copy(messages::sound::cCreateNoStreamFile);

        return;
    }

    if (sStreamSource == cSourceImport && !HasStreamCreateMainWave())
        warning->copy(messages::sound::cCreateNoImportFile);
    else if (sNewStreamPath.isEmpty())
        warning->copy(messages::sound::cCreateNoStreamPath);
    else if (IsStreamPathUsed(sNewStreamPath))
        warning->format(messages::sound::cCreateStreamPathUsedFormat, sNewStreamPath.cstr());
}

static void FindSoundCreateWarning(sead::BufferedSafeString* warning)
{
    warning->clear();

    if (!sPlayer)
    {
        warning->copy(messages::sound::cCreateNoPlayer);
        return;
    }

    if (sSoundType == Sound::SoundType::Strm)
    {
        FindStreamCreateWarning(warning);
        return;
    }

    if (sSoundType != Sound::SoundType::Wave)
        return;

    if (sWaveSource == cSourceImport && sImportName.isEmpty())
        warning->copy(messages::sound::cCreateNoImportFile);
    else if (sWaveSource == cSourceExisting && !sWaveFile)
        warning->copy(messages::sound::cCreateNoWaveFile);
}

static bool ValidateSoundCreate()
{
    FindSoundCreateWarning(&sWarning);

    if (sWarning.isEmpty() && sSoundType == Sound::SoundType::Wave && sWaveSource == cSourceImport)
    {
        sImportedWave = ImportPickedWave();

        if (!sImportedWave)
            sWarning.copy(messages::import::cWaveReadFailed);
    }

    if (sWarning.isEmpty() && sSoundType == Sound::SoundType::Strm && sStreamSource == cSourceImport)
        BuildStreamCreateWaves(&sWarning);

    if (sWarning.isEmpty())
        return true;

    ImGui::OpenPopup("###SoundCreate");

    return false;
}

static void ApplySoundCreate(Sound* sound)
{
    Bfsar& archive = ActiveSoundArchive();

    sound->getPlayerRef().attach(sPlayer);
    sound->setSoundType(sSoundType);

    switch (sSoundType)
    {
        case Sound::SoundType::Wave:
        {
            Item* wave = sWaveSource == cSourceExisting ? sWaveFile : nullptr;

            if (sImportedWave)
            {
                archive.getWaveFileList().pushBack(sImportedWave);
                archive.updateList(archive.getWaveFileList());

                wave = sImportedWave;
                sImportedWave = nullptr;
            }

            sound->getWaveSoundInfo().getWaveFileRef().attach(wave);
            break;
        }

        case Sound::SoundType::Strm:
        {
            sound->setPanMode(snd::PanMode::Balance);

            if (sStreamSource == cSourceExisting)
            {
                sound->getStreamSoundInfo().getPath() = sStreamPath;

                if (archive.hasArchiveDirectory())
                    ReloadStreamFile(sound);

                break;
            }

            sound->getStreamSoundInfo().getPath() = sNewStreamPath;

            if (sStreamSource == cSourceImport)
                AttachStreamCreateWaves(sound);

            break;
        }

        case Sound::SoundType::Seq:
        {
            Sound::SequenceSoundInfo& seqSoundInfo = sound->getSequenceSoundInfo();

            seqSoundInfo.getSequenceFileRef().attach(sSequenceFile);
            seqSoundInfo.getBankRef(0).attach(sBank);
            break;
        }

        default:
            break;
    }
}

static void SoundCreatePropertiesCallback(bool clear, Item* item, bool* validate)
{
    if (clear)
        ResetSoundCreate();

    if (validate)
        *validate = ValidateSoundCreate();
    else if (item)
        ApplySoundCreate(static_cast<Sound*>(item));
    else
        DrawSoundCreateUI();
}

static InstanciateItemCallback CreateSoundOfType(bool clear, bool typeLocked, Sound::SoundType type)
{
    sTypeLocked = typeLocked;

    if (typeLocked)
        sSoundType = type;
    else if (sSoundType == Sound::SoundType::Invalid)
        sSoundType = Sound::SoundType::Wave;

    return CreateItemFunc(clear, []() -> Item* { return new Sound(); }, &SoundCreatePropertiesCallback);
}

InstanciateItemCallback CreateSoundFunc(bool clear)
{
    return CreateSoundOfType(clear, false, Sound::SoundType::Invalid);
}

InstanciateItemCallback CreateStreamSoundFunc(bool clear)
{
    return CreateSoundOfType(clear, true, Sound::SoundType::Strm);
}

InstanciateItemCallback CreateWaveSoundFunc(bool clear)
{
    return CreateSoundOfType(clear, true, Sound::SoundType::Wave);
}

InstanciateItemCallback CreateSequenceSoundFunc(bool clear)
{
    return CreateSoundOfType(clear, true, Sound::SoundType::Seq);
}
