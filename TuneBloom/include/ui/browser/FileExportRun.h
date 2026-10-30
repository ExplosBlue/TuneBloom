#pragma once

#include <scan/FileProbe.h>

#include <vector>

struct BrowserFile;

enum class FileExportMode
{
    Files,
    Wav
};

bool CanExportFileContents(const FileProbe& probe);
void StartFileExport(const std::vector<const BrowserFile*>& files, FileExportMode mode);
bool IsFileExportRunning();
void DrawFileExportProgress();
