#include <ui/sound/SoundViews.h>

#include <ui/widgets/Dialogs.h>
#include <ui/file/File.h>
#include <ui/file/FileDialogs.h>
#include <ui/items/ItemList.h>
#include <ui/items/ItemSelector.h>
#include <ui/Messages.h>
#include <ui/widgets/Properties.h>
#include <ui/stream/StreamTrackImport.h>
#include <ui/wave/WaveImportPanel.h>
#include <ui/widgets/Widgets.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_internal.h>

#include <bfsar/model/Sound.h>
#include <bfsar/files/WaveFile.h>

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
static WaveFile::RiffWaveInfo sStreamMainInfo;
static WaveFile::Encoding sStreamMainEncoding = WaveFile::Encoding::DspAdpcm;
static sead::FixedSafeString<512> sStreamMainName;
static bool sStreamMainOpenRequest = false;

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
    GetFileStem(&name, path);

    SuggestCreateItemName(name);
}

static void ClearImportWave()
{
    sImportInfo.clear();
    sImportName.clear();
    sImportEncoding = WaveFile::Encoding::DspAdpcm;
    ResetWaveImport();
}

static void ClearStreamImport()
{
    sStreamMainInfo.clear();
    sStreamMainName.clear();
    sStreamMainEncoding = WaveFile::Encoding::DspAdpcm;
    sStreamMainOpenRequest = false;
    ResetStreamCreateImport();
}

static void ResetSoundCreate()
{
    if (!ListHasItem(sBfsar.getPlayerList(), sPlayer))
        sPlayer = FirstItem(sBfsar.getPlayerList());

    sWaveFile = nullptr;
    sImportedWave = nullptr;
    ClearImportWave();

    sStreamPath.clear();
    sNewStreamPath.clear();
    sNewStreamPathEdited = false;
    ClearStreamImport();

    sSequenceFile = nullptr;
    sBank = nullptr;
}

static void MakeDefaultStreamPath(sead::BufferedSafeString* out, const sead::SafeString& name)
{
    out->clear();

    if (name.isEmpty())
        return;

    sead::FixedSafeString<512> dir(cDefaultStreamDir);

    for (const Item* item : sBfsar.getSoundList())
    {
        const Sound* sound = static_cast<const Sound*>(item);

        if (sound->getSoundType() != Sound::SoundType::Strm || sound->getStreamSoundInfo().getPath().isEmpty())
            continue;

        dir.copy(sound->getStreamSoundInfo().getPath().cstr());
        dir.trim(ImMax(dir.rfindIndex("/"), 0));
        break;
    }

    const char* extension = GetInnerFileFormat(sBfsar.getFormat(), InnerFileKind::Stream).extension;

    if (dir.isEmpty())
        out->format("%s.%s", name.cstr(), extension);
    else
        out->format("%s/%s.%s", dir.cstr(), name.cstr(), extension);
}

static bool IsStreamPathUsed(const sead::SafeString& path)
{
    for (const Item* item : sBfsar.getSoundList())
    {
        const Sound* sound = static_cast<const Sound*>(item);

        if (sound->getSoundType() == Sound::SoundType::Strm && sound->getStreamSoundInfo().getPath() == path)
            return true;
    }

    const sead::SafeString* archivePaths[2] = { &sBfsar.getFilePath(), &sBfsar.getLoadedArchivePath() };

    for (const sead::SafeString* archivePath : archivePaths)
    {
        sead::FixedSafeString<512> dir;

        if (!sead::Path::getDirectoryName(&dir, *archivePath))
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

    ClearImportWave();
    sImportInfo.path = path;

    if (!WaveFile::readRiffWavInfo(&sImportInfo))
    {
        sImportInfo.clear();
        return;
    }

    GetFileStem(&sImportName, sImportInfo.path);

    SuggestCreateItemName(sImportName);
}

static WaveFile* ImportWave(WaveFile::RiffWaveInfo* info, WaveFile::Encoding encoding, const sead::SafeString& name)
{
    std::string tempPath = FinalizeImportInfoForCommit(info);

    if (!info->isLoop)
        info->loopStartFrame = 0;

    WaveFile* wave = new WaveFile();
    wave->setEnableName(true);
    wave->getName() = name;

    const bool success = wave->readWavFile(*info, encoding);

    if (!tempPath.empty())
        ::remove(tempPath.c_str());

    if (success)
        return wave;

    delete wave;

    return nullptr;
}

static WaveFile* ImportPickedWave()
{
    WaveFile* wave = ImportWave(&sImportInfo, sImportEncoding, sImportName);

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

static sead::FormatFixedSafeString<64> FormatSoundTypeLabel(const SoundTypeEntry& entry)
{
    return sead::FormatFixedSafeString<64>("%s %s", entry.icon, entry.label);
}

static f32 CalcSoundTypeWidth()
{
    const ImGuiStyle& style = ImGui::GetStyle();

    f32 width = style.ItemSpacing.x * (IM_ARRAYSIZE(cSoundTypes) - 1);

    for (const SoundTypeEntry& entry : cSoundTypes)
        width += ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + ImGui::CalcTextSize(FormatSoundTypeLabel(entry).cstr()).x;

    return width;
}

static void DrawSoundTypeUI()
{
    for (const SoundTypeEntry& entry : cSoundTypes)
    {
        if (&entry != cSoundTypes)
            ImGui::SameLine();

        if (ImGui::RadioButton(FormatSoundTypeLabel(entry).cstr(), sSoundType == entry.type))
            sSoundType = entry.type;
    }
}

static bool BeginSourceBox(const char* title)
{
    if (sTypeLocked)
        return BeginGroupBox(title);

    const f32 typeWidth = CalcSoundTypeWidth();

    if (!BeginGroupBoxWithTools(messages::common::cType, typeWidth))
        return false;

    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - typeWidth);

    DrawSoundTypeUI();

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
    ImGui::RadioButton(messages::sound::cWaveSourceImport, source, cSourceImport);
    ImGui::SameLine();
    ImGui::RadioButton(messages::source::cSelectExisting, source, cSourceExisting);
    ImGui::SameLine();
    ImGui::RadioButton(messages::common::cNone, source, cSourceNone);
}

static void DrawWaveSourceUI()
{
    if (!BeginSourceBox(messages::common::cWaveFile))
        return;

    DialogRowGap();
    DrawSourceChoiceUI(&sWaveSource);

    if (sWaveSource == cSourceExisting)
    {
        ItemSelectorDialogRow(messages::common::cFile, sBfsar.getWaveFileList(), &sWaveFile, false);
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

    ImGui::SameLine();

    if (sImportName.isEmpty())
        ImGui::TextDisabled("%s", messages::sound::cNoImportFile);
    else
        ImGui::TextUnformatted(sImportName.cstr());

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

    WaveFile::RiffWaveInfo info;
    info.clear();
    info.path.copy(paths[0].c_str());

    if (!WaveFile::readRiffWavInfo(&info))
        return;

    ClearStreamImport();
    ClearImportWave();

    sStreamMainInfo = info;
    GetFileStem(&sStreamMainName, sStreamMainInfo.path);

    paths.erase(paths.begin());
    SetStreamCreateFiles(paths);

    SuggestCreateItemName(sStreamMainName);
    sStreamMainOpenRequest = true;
}

static WaveFile* ImportStreamMainWave()
{
    WaveFile::RiffWaveInfo info = sStreamMainInfo;

    return ImportWave(&info, sStreamMainEncoding, sStreamMainName);
}

static void DrawStreamMainImportDialog()
{
    if (sStreamMainOpenRequest)
    {
        ImGui::OpenPopup("###StreamMainImport");
        sStreamMainOpenRequest = false;
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowContentSize(ImVec2(CalcWaveImportWidth(), 0.0f));

    if (!BeginDialog(sead::FormatFixedSafeString<64>(ICON_LC_AUDIO_LINES " %s###StreamMainImport", messages::import::cWaveRegionTitle).cstr(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    DrawWaveImportInfo(&sStreamMainEncoding, &sStreamMainInfo, sead::FormatFixedSafeString<560>(messages::import::cWaveHeaderFormat, sStreamMainName.cstr()).cstr());

    BeginDialogFooter({messages::common::cImport, messages::common::cCancel}, false);

    const bool importClicked = DialogButton(messages::common::cImport);
    const bool cancelClicked = DialogButton(messages::common::cCancel);

    if (importClicked)
    {
        WaveFile* wave = ImportStreamMainWave();

        if (wave)
        {
            SetStreamCreateMainWave(wave, sStreamMainName);
            ImGui::CloseCurrentPopup();
        }
        else
        {
            ImGui::OpenPopup("###StreamMainFailed");
        }
    }
    else if (WaveImportConfirmCancel(cancelClicked))
    {
        if (!HasStreamCreateMainWave())
            ClearStreamImport();

        ImGui::CloseCurrentPopup();
    }

    WarningPopup("###StreamMainFailed", messages::import::cWaveReadFailed);

    ImGui::EndPopup();
}

static void DrawStreamImportSource(const char* browseLabel)
{
    if (ImGui::Button(browseLabel))
        PickStreamImportFiles();

    ImGui::SameLine();

    const u32 fileCount = GetStreamCreateFileCount() + 1;

    if (sStreamMainName.isEmpty())
        ImGui::TextDisabled("%s", messages::sound::cNoImportFile);
    else if (fileCount == 1)
        ImGui::TextUnformatted(sStreamMainName.cstr());
    else
        ImGui::Text(messages::sound::cImportFileCountFormat, fileCount);
}

static void DrawStreamExistingSource(const char* browseLabel)
{
    const bool hasArchiveDir = sBfsar.hasArchiveDirectory();

    ImGui::BeginDisabled(!hasArchiveDir);

    if (ImGui::Button(browseLabel) && PickStreamFilePath(&sStreamPath, nullptr))
        SuggestNameFromPath(sStreamPath);

    ImGui::EndDisabled();

    if (!hasArchiveDir)
        SetDisabledTooltip(messages::stream::cReloadNoArchiveFolder);

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

    const sead::FormatFixedSafeString<64> relativeDetail(messages::stream::cPathRelativeFormat, GetInnerFileExtension(sBfsar.getFormat(), InnerFileKind::SoundArchive));
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

    DrawStreamMainImportDialog();

    if (TakeStreamCreateEditRequest())
        sStreamMainOpenRequest = true;

    if (sStreamSource != cSourceImport || !HasStreamCreateMainWave())
        return;

    const f32 maxHeight = sead::Mathf::max(cImportPanelMinHeight, ImGui::GetMainViewport()->Size.y - cStreamPanelMargin);

    if (CalcStreamCreateImportHeight() <= maxHeight)
    {
        DrawStreamCreateImport();
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    if (ImGui::BeginChild("###StreamImport", ImVec2(CalcStreamCreateImportWidth() + ImGui::GetStyle().ScrollbarSize, maxHeight)))
        DrawStreamCreateImport();

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

static void DrawSequenceSourceUI()
{
    if (!BeginSourceBox(messages::common::cSequence))
        return;

    ItemSelectorDialogRow(messages::common::cSequenceFile, sBfsar.getSequenceFileList(), &sSequenceFile, true);
    ItemSelectorDialogRow(messages::common::cBank, sBfsar.getBankList(), &sBank, true);

    EndSourceBox(2);
}

static void DrawSoundCreateUI()
{
    ItemSelectorDialogRow(messages::common::cPlayer, sBfsar.getPlayerList(), &sPlayer, false);

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
    sound->getPlayerRef().attach(sPlayer);
    sound->setSoundType(sSoundType);

    switch (sSoundType)
    {
        case Sound::SoundType::Wave:
        {
            Item* wave = sWaveSource == cSourceExisting ? sWaveFile : nullptr;

            if (sImportedWave)
            {
                sBfsar.getWaveFileList().pushBack(sImportedWave);
                sBfsar.updateList(sBfsar.getWaveFileList());

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

                if (sBfsar.hasArchiveDirectory())
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
