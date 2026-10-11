#pragma once

#include <core/Json.h>

#include <string>

inline constexpr const char* cSettingsFile = "settings.json";
inline constexpr const char* cStateFile    = "state.json";

std::string GetConfigDir();
std::string GetConfigPath(const char* fileName);
bool WriteTextFileAtomic(const std::string& path, const std::string& text);

bool LoadConfigSection(const char* fileName, const char* name, json::Value* section);
bool SaveConfigSection(const char* fileName, const char* name, const json::Value& section);

void MigrateLegacyConfig();
