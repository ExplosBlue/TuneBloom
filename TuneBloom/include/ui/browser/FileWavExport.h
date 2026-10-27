#pragma once

#include <ui/browser/FilePreview.h>

#include <core/BackgroundJob.h>

#include <memory>
#include <string>

class FileWavExport
{
public:
    void request(const std::string& path, const std::string& name);
    void update();
    bool isExporting(const std::string& path) const;

private:
    BackgroundJob mJob;
    std::unique_ptr<DecodedAudioFile> mDecoding;
    std::string mOutPath;
};
