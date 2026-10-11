#pragma once

#include <filesystem>
#include <string>

void ShowInFileManager(const std::string& path);
void OpenURL(const char* url);

std::filesystem::path GetExecutablePath();
void UseExecutableFolder();
