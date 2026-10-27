#pragma once

#include <string>
#include <vector>

std::vector<std::string>& GetRecentFiles();
void LoadRecentFiles();
std::vector<std::string> TakeLegacyPinnedFiles();
void AddRecentFile(const char* path);
void TrimRecentFiles();
void RegisterRecentFilesSettings();
bool DrawRecentFilesList(std::string* outPath);
bool DrawRecentFilesTable(std::string* outPath);
