#include <ui/browser/FileExportRun.h>

#include <ui/app/Notifications.h>
#include <ui/browser/FilePreview.h>
#include <ui/export/ExportInternal.h>
#include <ui/file/FileDialogs.h>
#include <ui/widgets/Widgets.h>
#include <messages/Messages.h>

#include <aal/AudioResource.h>
#include <scan/FileIndex.h>
#include <bfsar/files/LooseStreamFile.h>
#include <bfsar/files/WaveFile.h>
#include <bfsar/io/FileIo.h>
#include <bfsar/io/PackFile.h>
#include <bfsar/model/ArchiveFormat.h>

#include <imgui/imgui.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static constexpr f64 cProgressDelaySec = 0.15;

static constexpr const char* cProgressPopupId = "###FileExportProgress";

namespace
{

struct ExportSource
{
    std::string path;
    std::string name;
    std::string stem;
    std::string formatName;
    BrowserFileKind kind = BrowserFileKind::Wave;
};

struct FileExportRun
{
    bool active = false;
    FileExportMode mode = FileExportMode::Wav;
    std::vector<ExportSource> sources;
    std::string target;
    bool isSingleFile = false;
    s32 current = 0;
    std::unique_ptr<AudioResource> resource;
    std::vector<AudioAsset*> assets;
    s32 assetIndex = 0;
    std::string assetFolder;
    s32 outputCount = 0;
    s32 failureCount = 0;
    std::vector<std::string> skippedAssets;
    std::vector<std::string> skipReasons;
    std::string formatName;
    f64 startTime = 0.0;
};

}

static FileExportRun sRun;

bool CanExportFileContents(const FileProbe& probe)
{
    return probe.kind == BrowserFileKind::Wave || probe.kind == BrowserFileKind::Stream || probe.kind == BrowserFileKind::AudioResource;
}

bool IsFileExportRunning()
{
    return sRun.active;
}

static bool ReadSourceFile(const std::string& path, std::vector<u8>* out)
{
    if (packfile::IsPackPath(path))
        return packfile::ReadFile(path, out);

    return ReadFileContents(path, out);
}

static std::string GetRawExtension(const std::string& name)
{
    fs::path path(name);

    if (IsZstdPath(name.c_str()))
        path = path.stem();

    const std::string extension = path.extension().string();

    return extension.empty() ? extension : extension.substr(1);
}

static std::string GetOutputExtension(const ExportSource& source, FileExportMode mode)
{
    return mode == FileExportMode::Wav ? std::string(cWavExtension) : GetRawExtension(source.name);
}

static bool ChooseSingleTarget(const ExportSource& source, FileExportMode mode, sead::BufferedSafeString* outPath)
{
    const std::string extension = GetOutputExtension(source, mode);
    const fs::path defaultPath = fs::path(source.path).parent_path() / (source.stem + "." + extension);
    const sead::FormatFixedSafeString<64> filter(messages::filedialog::cFilterFileFormat, source.formatName.c_str(), extension.c_str());

    return SaveFileDialogWithExtension(outPath, mode == FileExportMode::Wav ? messages::filedialog::cFilterWave : filter.cstr(), extension.c_str(), defaultPath.string().c_str());
}

static void NotifySkippedFiles(const std::vector<const BrowserFile*>& skipped)
{
    if (skipped.empty())
        return;

    sead::FormatFixedSafeString<288> title(messages::files::cSkippedFilesFormat, FormatCountText(skipped.size()).cstr());

    if (skipped.size() == 1)
        title.format(messages::files::cSkippedFileFormat, skipped.front()->name.c_str());

    notify::Add(notify::Kind::Info, title.cstr(), messages::files::cSkippedFilesDetail);
}

void StartFileExport(const std::vector<const BrowserFile*>& files, FileExportMode mode)
{
    if (sRun.active)
        return;

    FileExportRun run;
    run.mode = mode;

    std::vector<const BrowserFile*> skipped;

    for (const BrowserFile* file : files)
    {
        if (CanExportFileContents(file->probe))
            run.sources.push_back({ file->path, file->name, file->stem, file->probe.formatName, file->probe.kind });
        else
            skipped.push_back(file);
    }

    if (run.sources.empty())
        return;

    const ExportSource& first = run.sources.front();
    run.isSingleFile = run.sources.size() == 1 && first.kind != BrowserFileKind::AudioResource;

    sead::FixedSafeString<512> target;

    if (run.isSingleFile ? !ChooseSingleTarget(first, mode, &target) : !SelectFolderDialog(&target, mode == FileExportMode::Wav ? messages::exporting::cSelectWavFolder : messages::exporting::cSelectFolder))
        return;

    NormalizePathSlashes(&target);

    run.target = target.cstr();
    run.active = true;
    run.startTime = ImGui::GetTime();

    sRun = std::move(run);

    BeginExportNameBatch();
    NotifySkippedFiles(skipped);
}

static void EndOutput(bool isWritten, const char* formatName)
{
    sRun.outputCount++;

    if (!isWritten)
        sRun.failureCount++;

    if (sRun.formatName.empty())
        sRun.formatName = formatName;
    else if (sRun.formatName != formatName)
        sRun.formatName = messages::files::cFormatAudio;
}

static bool WriteLooseWav(const ExportSource& source, std::vector<u8> bytes, const char* path)
{
    if (source.kind == BrowserFileKind::Stream && LooseStreamFile::IsStreamFile(bytes.data(), static_cast<u32>(bytes.size())))
    {
        LooseStreamFile stream;
        sead::FixedSafeString<256> error;

        if (stream.open(bytes.data(), static_cast<u32>(bytes.size()), sead::FixedSafeString<512>(source.path.c_str()), &error))
            return ExportStreamToWav(stream.getStream(), path, true, 1, 0.0f, 0, true);
    }

    DecodedAudioFile decoded;
    decoded.path = source.path;
    decoded.name = source.stem;
    decoded.bytes = std::move(bytes);

    DecodeAudioBytes(&decoded);

    std::unique_ptr<WaveFile> wave(BuildDecodedWave(decoded));

    return wave && wave->writeWavFile(path);
}

static void ExportLooseFile(const ExportSource& source)
{
    sead::FixedSafeString<512> path(sRun.target.c_str());

    if (!sRun.isSingleFile)
        BuildUniqueExportPath(&path, sRun.target.c_str(), sead::SafeString(source.stem.c_str()), GetOutputExtension(source, sRun.mode).c_str());

    std::vector<u8> bytes;
    bool isWritten = ReadSourceFile(source.path, &bytes);

    if (isWritten && sRun.mode == FileExportMode::Wav)
        isWritten = WriteLooseWav(source, std::move(bytes), path.cstr());
    else if (isWritten)
        isWritten = fileio::WriteWholeFileAtomic(path.cstr(), bytes.data(), bytes.size());

    EndOutput(isWritten, sRun.mode == FileExportMode::Wav ? messages::common::cWav : source.formatName.c_str());
}

static void OpenResource(const ExportSource& source)
{
    std::vector<u8> bytes;
    std::unique_ptr<AudioResource> resource = std::make_unique<AudioResource>();
    const char* error = nullptr;

    if (!ReadSourceFile(source.path, &bytes) || !resource->open(std::move(bytes), sead::FixedSafeString<512>(source.path.c_str()), &error))
    {
        EndOutput(false, source.formatName.c_str());
        sRun.current++;
        return;
    }

    sRun.assets.clear();
    sRun.assetIndex = 0;
    sRun.assetFolder = sRun.target;

    for (Item* asset : resource->getAssetList())
        sRun.assets.push_back(static_cast<AudioAsset*>(asset));

    if (sRun.sources.size() > 1)
    {
        sead::FixedSafeString<256> folderName(source.stem.c_str());
        SanitizeFilenameInPlace(&folderName);

        sRun.assetFolder += '/';
        sRun.assetFolder += folderName.cstr();

        std::error_code createError;
        fs::create_directories(fs::path(sRun.assetFolder), createError);
    }

    sRun.resource = std::move(resource);
}

static bool IsAssetSkipped(const AudioAsset& asset, sead::BufferedSafeString* outReason)
{
    if (sRun.mode == FileExportMode::Wav)
        return asset.getPlaybackError(outReason);

    if (asset.getCompleteFile() && GetAudioAssetKindExtension(asset.getCompleteFileKind()))
        return false;

    outReason->copy(asset.getNoAudioReason());
    return true;
}

static bool SkipAsset(const AudioAsset& asset)
{
    sead::FixedSafeString<256> reason;

    if (!IsAssetSkipped(asset, &reason))
        return false;

    sRun.skippedAssets.push_back(asset.getName().cstr());

    if (std::find(sRun.skipReasons.begin(), sRun.skipReasons.end(), reason.cstr()) == sRun.skipReasons.end())
        sRun.skipReasons.push_back(reason.cstr());

    return true;
}

static void ExportAsset(AudioAsset* asset)
{
    sRun.resource->loadStreamFile(asset);

    if (SkipAsset(*asset))
        return;

    const AudioAssetKind kind = asset->getCompleteFileKind();
    const char* extension = sRun.mode == FileExportMode::Wav ? cWavExtension : GetAudioAssetKindExtension(kind);

    sead::FixedSafeString<256> baseName;
    ResolveExportBaseName(asset, messages::common::cAudioAsset, &baseName);

    sead::FixedSafeString<512> path;
    BuildUniqueExportPath(&path, sRun.assetFolder.c_str(), baseName, extension);

    if (sRun.mode == FileExportMode::Wav)
    {
        EndOutput(asset->writeWavFile(path), messages::common::cWav);
        return;
    }

    const AudioAssetFile& file = asset->getCompleteFile();

    EndOutput(file && fileio::WriteWholeFileAtomic(path.cstr(), file->data(), file->size()), GetAudioAssetKindName(kind));
}

static void CloseResource()
{
    sRun.assets.clear();
    sRun.resource.reset();
    sRun.current++;
}

static void NotifySkippedAssets()
{
    if (sRun.skippedAssets.empty())
        return;

    const sead::FormatFixedSafeString<64> count(messages::files::cAssetCountFormat, FormatCountText(sRun.skippedAssets.size()).cstr());
    const sead::FormatFixedSafeString<288> title(messages::files::cSkippedFileFormat, sRun.skippedAssets.size() == 1 ? sRun.skippedAssets.front().c_str() : count.cstr());

    std::string detail;

    for (const std::string& reason : sRun.skipReasons)
        detail += (detail.empty() ? "" : "\n") + reason;

    notify::Add(notify::Kind::Warning, title.cstr(), detail.c_str());
}

static void FinishFileExport()
{
    NotifySkippedAssets();

    const s32 total = sRun.outputCount;
    const s32 failureCount = sRun.failureCount;
    const char* formatName = sRun.formatName.c_str();

    if (total == 0)
    {
        sRun = FileExportRun();
        return;
    }

    if (sRun.mode == FileExportMode::Wav && total == 1)
        ShowWavExportResult(failureCount == 0);
    else if (sRun.mode == FileExportMode::Wav)
        ShowWavBatchExportResult(total, failureCount);
    else if (total == 1)
        ShowExportResult(sead::FormatFixedSafeString<128>(failureCount == 0 ? messages::exporting::cFileExportedFormat : messages::exporting::cFileFailedFormat, formatName).cstr());
    else
        ShowBatchExportResult(total, failureCount, formatName);

    if (failureCount < total)
        ShowExportedFiles(sead::FixedSafeString<512>(sRun.target.c_str()));

    sRun = FileExportRun();
}

static void StepFileExport()
{
    if (sRun.current >= static_cast<s32>(sRun.sources.size()))
    {
        FinishFileExport();
        return;
    }

    const ExportSource& source = sRun.sources[sRun.current];

    if (source.kind != BrowserFileKind::AudioResource)
    {
        ExportLooseFile(source);
        sRun.current++;
    }
    else if (!sRun.resource)
    {
        OpenResource(source);
    }
    else if (sRun.assetIndex < static_cast<s32>(sRun.assets.size()))
    {
        ExportAsset(sRun.assets[sRun.assetIndex++]);
    }
    else
    {
        CloseResource();
    }
}

static bool DrawProgressRows()
{
    const s32 sourceNum = static_cast<s32>(sRun.sources.size());
    const s32 current = std::min(sRun.current, sourceNum - 1);
    const s32 assetNum = static_cast<s32>(sRun.assets.size());
    const bool isInResource = sRun.resource && assetNum > 0;
    const f32 assetFraction = isInResource ? static_cast<f32>(sRun.assetIndex) / assetNum : 0.0f;
    const f32 fraction = (sRun.current + assetFraction) / sourceNum;

    if (sourceNum == 1 && isInResource)
    {
        const s32 asset = std::min(sRun.assetIndex, assetNum - 1);

        return DrawExportProgressRows(sRun.assets[asset]->getName().cstr(), asset + 1, assetNum, fraction);
    }

    return DrawExportProgressRows(sRun.sources[current].name.c_str(), current + 1, sourceNum, fraction);
}

void DrawFileExportProgress()
{
    if (!sRun.active)
    {
        CloseExportProgressDialog(cProgressPopupId);
        return;
    }

    if (ImGui::GetTime() - sRun.startTime < cProgressDelaySec || !BeginExportProgressDialog(cProgressPopupId))
    {
        StepFileExport();
        return;
    }

    if (DrawProgressRows())
        sRun = FileExportRun();
    else
        StepFileExport();

    if (!sRun.active)
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}
