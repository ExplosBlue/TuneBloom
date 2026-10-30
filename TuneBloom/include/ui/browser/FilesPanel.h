#pragma once

#include <basis/seadTypes.h>

enum class BrowserFileKind;
struct ImVec4;

const char* GetFileKindIcon(BrowserFileKind kind);
ImVec4 GetFileKindColor(BrowserFileKind kind);

void LoadFilesPanelConfig();
void RequestFilesPanelRoot(const char* path, bool isAnnounced = false);
bool IsFilesPanelDropTarget(f32 x, f32 y);
void RequestFilesPanelFolderDialog();
bool TakeFilesPanelRequests();
void DrawFilesPanel();
