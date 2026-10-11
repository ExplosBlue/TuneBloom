#include <ui/export/Export.h>

#include <ui/widgets/Dialogs.h>
#include <ui/export/ExportInternal.h>
#include <ui/file/FileDialogs.h>
#include <ui/file/LoadProgress.h>
#include <ui/Messages.h>
#include <ui/player/PlayerViews.h>
#include <ui/file/SystemShell.h>
#include <ui/widgets/Widgets.h>

#include <libs/IconsLucide.h>
#include <imgui/imgui_internal.h>

#include <bfsar/model/Sound.h>
#include <bfsar/audio/WavWriter.h>
#include <bfsar/files/WaveFile.h>
#include <midi/SeqMidiExporter.h>

#include <framework/glfw/seadGameFrameworkBaseGlfw.h>

#include <app/Utilll.h>

#include <vector>

static constexpr f64 cStepBudgetSec   = 0.012;
static constexpr f32 cProgressWidthEm = 24.0f;

static constexpr const char* cProgressPopupId = "###ExportProgress";

namespace
{

struct ExportTask
{
    Sound* sound = nullptr;
    s32 soloTrack = -1;
    sead::FixedSafeString<512> path;
};

struct SoundExportRun
{
    bool active = false;
    bool rendering = false;
    std::vector<ExportTask> tasks;
    s32 current = 0;
    s32 failureCount = 0;
    SoundExportOptions options;
    sead::FixedSafeString<512> showPath;
};

}

static SoundExportRun sRun;
static bool sShowExportedFiles = false;
static f32 sPreviewFraction = -1.0f;

static bool sExportTitleSet = false;
static sead::FixedSafeString<256> sExportOriginalTitle;

void DrawShowExportedFilesCheck()
{
    ImGui::Checkbox(messages::exporting::cShowFiles, &sShowExportedFiles);
}

void ShowExportedFiles(const sead::SafeString& path)
{
    if (sShowExportedFiles)
        ShowInFileManager(path.cstr());
}

bool IsSoundExportRunning()
{
    return sRun.active;
}

static void AddSoundTasks(Sound* sound, const sead::SafeString& path)
{
    ExportTask task;
    task.sound = sound;
    task.path = path;

    u16 trackFlags = 0;

    if (sRun.options.splitSeqTracks && sound->getSoundType() == Sound::SoundType::Seq)
    {
        for (const SeqMidiPart& part : collectSeqMidiContents(*sound).parts)
            trackFlags |= 1 << part.trackNo;
    }

    if ((trackFlags & (trackFlags - 1)) == 0)
    {
        sRun.tasks.push_back(task);
        return;
    }

    for (u32 trackNo = 0; trackNo < cSeqMidiTrackNum; trackNo++)
    {
        if (!(trackFlags & (1 << trackNo)))
            continue;

        task.soloTrack = static_cast<s32>(trackNo);
        BuildTrackExportPath(&task.path, path.cstr(), trackNo);
        sRun.tasks.push_back(task);
    }
}

bool StartSoundExport(const std::vector<Sound*>& sounds, const SoundExportOptions& options)
{
    const bool single = sounds.size() == 1;

    sead::FixedSafeString<512> target;

    if (single)
    {
        sead::FixedSafeString<512> defaultPath;
        BuildDefaultExportPath(&defaultPath, sounds[0], "wav");

        if (!SaveFileDialogWithExtension(&target, messages::filedialog::cFilterWave, "wav", defaultPath.cstr()))
            return false;
    }
    else if (!SelectFolderDialog(&target, messages::exporting::cSelectWavFolder))
        return false;

    NormalizePathSlashes(&target);

    sRun = SoundExportRun();
    sRun.options = options;
    sRun.showPath = target;

    BeginExportNameBatch();

    for (Sound* sound : sounds)
    {
        sead::FixedSafeString<512> path(target);

        if (!single)
            BuildExportPathFromDir(&path, target.cstr(), sound, "wav");

        AddSoundTasks(sound, path);
    }

    sSoundPlayer.stopAllPlayers();
    sSoundPlayer.stopAllVoices();

    sRun.active = true;

    return true;
}

void CancelSoundExportRun()
{
    if (sRun.rendering)
        sSoundPlayer.endSeqExport(nullptr);

    sRun = SoundExportRun();
}

static const char* GetSingleResultText(const Sound* sound, bool exported)
{
    switch (sound->getSoundType())
    {
        case Sound::SoundType::Seq:
            return exported ? messages::exporting::cSequenceExported : messages::exporting::cSequenceFailed;

        case Sound::SoundType::Wave:
            return exported ? messages::exporting::cWaveSoundExported : messages::exporting::cWaveSoundFailed;

        default:
            return exported ? messages::exporting::cStreamExported : messages::exporting::cStreamFailed;
    }
}

static void FinishSoundExport()
{
    const s32 taskNum = static_cast<s32>(sRun.tasks.size());

    if (taskNum == 1)
        ShowExportResult(GetSingleResultText(sRun.tasks[0].sound, sRun.failureCount == 0));
    else
        ShowBatchExportResult(taskNum, sRun.failureCount, messages::exporting::cFormatWav);

    if (sRun.failureCount < taskNum)
        ShowExportedFiles(sRun.showPath);

    sRun = SoundExportRun();
}

static void EndExportTask(bool exported)
{
    if (!exported)
        sRun.failureCount++;

    sRun.current++;
}

static bool ExportTaskAtOnce(const ExportTask& task)
{
    const SoundExportOptions& options = sRun.options;

    if (task.sound->getSoundType() == Sound::SoundType::Strm)
        return ExportStreamSoundToWav(task.sound, task.path.cstr(), options.multiChannel, options.loopCount, options.fadeSec, options.sampleRate, options.loopMarkers);

    if (task.sound->getSoundType() != Sound::SoundType::Wave)
        return false;

    Item* waveItem = task.sound->getWaveSoundInfo().getWaveFileRef().getItem();

    return waveItem && static_cast<WaveFile*>(waveItem)->writeWavFile(task.path);
}

static void StepSeqExportTask(const ExportTask& task)
{
    if (!sRun.rendering)
    {
        SoundPlayer::SeqExportSettings settings;
        settings.loopCount = sRun.options.loopCount;
        settings.fadeSec = sRun.options.fadeSec;
        settings.noEndSecs = sRun.options.noEndSecs;
        settings.sampleRate = sRun.options.sampleRate;
        settings.soloTrack = task.soloTrack;

        if (sSoundPlayer.beginSeqExport(task.sound, settings))
            sRun.rendering = true;
        else
            EndExportTask(false);

        return;
    }

    if (!sSoundPlayer.stepSeqExport(cStepBudgetSec))
        return;

    Pcm16Wav wav;
    sSoundPlayer.endSeqExport(&wav);
    sRun.rendering = false;

    if (!sRun.options.loopMarkers)
        wav.loop.enabled = false;

    EndExportTask(WritePcm16Wav(task.path, wav));
}

static void StepSoundExport()
{
    if (sRun.current >= static_cast<s32>(sRun.tasks.size()))
    {
        FinishSoundExport();
        return;
    }

    const ExportTask& task = sRun.tasks[sRun.current];

    if (task.sound->getSoundType() == Sound::SoundType::Seq)
        StepSeqExportTask(task);
    else
        EndExportTask(ExportTaskAtOnce(task));
}

static void SetExportWindowTitle()
{
    sead::GameFrameworkBaseGlfw* fw = sead::DynamicCast<sead::GameFrameworkBaseGlfw>(util::getFramework());

    if (!fw || sExportTitleSet)
        return;

    const char* current = glfwGetWindowTitle(fw->getWindowHandle());
    sExportOriginalTitle = current ? current : "TuneBloom";

    sead::FixedSafeString<512> exportTitle;
    exportTitle.format(messages::exporting::cWindowTitleFormat, sExportOriginalTitle.cstr());
    glfwSetWindowTitle(fw->getWindowHandle(), exportTitle.cstr());
    sExportTitleSet = true;
}

static void RestoreWindowTitle()
{
    sead::GameFrameworkBaseGlfw* fw = sead::DynamicCast<sead::GameFrameworkBaseGlfw>(util::getFramework());

    if (!fw || !sExportTitleSet)
        return;

    glfwSetWindowTitle(fw->getWindowHandle(), sExportOriginalTitle.cstr());
    sExportTitleSet = false;
}

static bool BeginExportProgressDialog()
{
    ImGui::OpenPopup(cProgressPopupId);

    CenterNextDialog();
    ImGui::SetNextWindowSize(ImVec2(ImFloor(ImGui::GetFontSize() * cProgressWidthEm), 0.0f));

    return BeginDialog(cProgressPopupId, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
}

static bool DrawExportProgressRows(const char* fileName, s32 fileNo, s32 fileNum, f32 fraction)
{
    const sead::FormatFixedSafeString<576> title(messages::exporting::cProgressFileFormat, fileName);

    sead::FixedSafeString<64> stage(messages::exporting::cProgressRendering);
    if (fileNum > 1)
        stage.format(messages::exporting::cProgressFileNumFormat, fileNo, fileNum);

    loadprogress::DrawRows(ICON_LC_FILE_AUDIO, title.cstr(), stage.cstr(), fraction);

    BeginDialogFooter({messages::common::cCancel});

    return DialogButton(messages::common::cCancel);
}

void DrawSoundExportProgressPreview(f32 fraction)
{
    sPreviewFraction = fraction;
}

static void DrawExportProgressPreview()
{
    if (!BeginExportProgressDialog())
        return;

    DrawExportProgressRows("BGM_LP_BOSS1_track3", 3, 8, sPreviewFraction);

    ImGui::EndPopup();
}

static void CloseExportProgressDialog()
{
    if (!ImGui::IsPopupOpen(cProgressPopupId))
        return;

    if (BeginExportProgressDialog())
    {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void DrawSoundExportProgress()
{
    if (!sRun.active && sPreviewFraction >= 0.0f)
    {
        DrawExportProgressPreview();
        return;
    }

    if (!sRun.active)
    {
        CloseExportProgressDialog();
        RestoreWindowTitle();
        return;
    }

    SetExportWindowTitle();

    if (!BeginExportProgressDialog())
        return;

    const s32 taskNum = static_cast<s32>(sRun.tasks.size());
    const s32 current = ImMin(sRun.current, taskNum - 1);
    const f32 taskFraction = sRun.rendering ? sSoundPlayer.getSeqExportProgress() : 0.0f;

    sead::FixedSafeString<256> fileName;
    GetFileStem(&fileName, sRun.tasks[current].path);

    if (DrawExportProgressRows(fileName.cstr(), current + 1, taskNum, (sRun.current + taskFraction) / taskNum))
        CancelSoundExportRun();
    else
        StepSoundExport();

    if (!sRun.active)
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}
