#pragma once

#include <basis/seadTypes.h>

struct ImGuiTableColumn;

f32 GetItemColumnUserWidth(const char* key);
void SetItemColumnUserWidth(const char* key, f32 em);
void ApplyItemColumnHidden(ImGuiTableColumn& column, const char* key);
void StoreItemColumnHidden(const ImGuiTableColumn& column, const char* key);
