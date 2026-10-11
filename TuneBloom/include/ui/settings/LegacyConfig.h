#pragma once

#include <core/Json.h>

bool HasLegacyConfig();
void MergeLegacyConfig(const char* fileName, json::Value* root);
void RemoveLegacyConfig();
