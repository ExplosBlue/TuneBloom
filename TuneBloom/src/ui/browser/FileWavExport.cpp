#include <ui/browser/FileWavExport.h>

#include <ui/export/ExportInternal.h>
#include <ui/file/FileDialogs.h>
#include <ui/Messages.h>

#include <bfsar/files/WaveFile.h>
#include <bfsar/model/ArchiveFormat.h>

#include <filesystem>

namespace fs = std::filesystem;

void FileWavExport::request(const std::string& path, const std::string& name)
{
    if (mDecoding)
        return;

    const fs::path defaultPath = fs::path(path).parent_path() / (name + "." + cWavExtension);
    sead::FixedSafeString<512> outPath;

    if (!SaveFileDialogWithExtension(&outPath, messages::filedialog::cFilterWave, cWavExtension, defaultPath.string().c_str()))
        return;

    mOutPath = outPath.cstr();
    mDecoding = std::make_unique<DecodedAudioFile>();
    mDecoding->path = path;
    mDecoding->name = name;

    DecodedAudioFile* decoding = mDecoding.get();

    mJob.start([decoding]()
    {
        DecodeAudioFileAt(decoding);
    });
}

void FileWavExport::update()
{
    if (!mJob.finish() || !mDecoding)
        return;

    WaveFile* wave = BuildDecodedWave(*mDecoding);
    const bool isWritten = wave && wave->writeWavFile(mOutPath.c_str());

    delete wave;
    mDecoding.reset();
    mOutPath.clear();

    ShowWavExportResult(isWritten);
}

bool FileWavExport::isExporting(const std::string& path) const
{
    return mDecoding && mDecoding->path == path;
}
