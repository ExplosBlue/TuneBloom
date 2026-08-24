#pragma once

#include <string>
#include <vector>

namespace macos {

bool openFileDialog(const std::string& title, const std::vector<std::string>& filters, std::string& outPath);
bool selectFolderDialog(const std::string& title, std::string& outPath);
bool saveFileDialog(const std::string& title, const std::string& defaultPath, std::string& outPath);

} // namespace macos
